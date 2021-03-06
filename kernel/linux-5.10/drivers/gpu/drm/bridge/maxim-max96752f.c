// SPDX-License-Identifier: GPL-2.0
/*
 * Maxim MAX96752F GMSL2 Deserializer with Dual LVDS (OLDI) Output
 *
 * Copyright (c) 2022 Rockchip Electronics Co. Ltd.
 */

#include <drm/drm_atomic_helper.h>
#include <drm/drm_bridge.h>
#include <drm/drm_connector.h>
#include <drm/drm_of.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_print.h>
#include <drm/drm_panel.h>

#include <linux/platform_device.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/regmap.h>
#include <linux/mfd/max96752f.h>

struct max96752f_bridge {
	struct drm_bridge bridge;
	struct drm_connector connector;
	struct drm_panel *panel;

	struct device *dev;
	struct max96752f *parent;
	struct regmap *regmap;
};

#define to_max96752f_bridge(x)	container_of(x, struct max96752f_bridge, x)

static int max96752f_bridge_connector_get_modes(struct drm_connector *connector)
{
	struct max96752f_bridge *des = to_max96752f_bridge(connector);

	return drm_panel_get_modes(des->panel, connector);
}

static const struct drm_connector_helper_funcs
max96752f_bridge_connector_helper_funcs = {
	.get_modes = max96752f_bridge_connector_get_modes,
};

static enum drm_connector_status
max96752f_bridge_connector_detect(struct drm_connector *connector, bool force)
{
	struct max96752f_bridge *des = to_max96752f_bridge(connector);

	return drm_bridge_detect(&des->bridge);
}

static const struct drm_connector_funcs max96752f_bridge_connector_funcs = {
	.reset = drm_atomic_helper_connector_reset,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.detect = max96752f_bridge_connector_detect,
	.destroy = drm_connector_cleanup,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

static void
max96752f_bridge_atomic_pre_enable(struct drm_bridge *bridge,
				   struct drm_bridge_state *old_bridge_state)
{
	struct max96752f_bridge *des = to_max96752f_bridge(bridge);
	struct drm_atomic_state *state = old_bridge_state->base.state;
	const struct drm_bridge_state *bridge_state;
	bool oldi_format;

	max96752f_regcache_sync(des->parent);

	bridge_state = drm_atomic_get_new_bridge_state(state, bridge);
	switch (bridge_state->output_bus_cfg.format) {
	case MEDIA_BUS_FMT_RGB888_1X7X4_JEIDA:
	case MEDIA_BUS_FMT_RGB666_1X7X3_SPWG:
		oldi_format = 0x0;
		break;
	case MEDIA_BUS_FMT_RGB888_1X7X4_SPWG:
		oldi_format = 0x1;
		break;
	default:
		oldi_format = 0x1;
		dev_warn(des->dev,
			 "unsupported LVDS bus format 0x%04x, using VESA\n",
			 bridge_state->output_bus_cfg.format);
		break;
	}

	regmap_update_bits(des->regmap, OLDI_REG(1), OLDI_FORMAT,
			   FIELD_PREP(OLDI_FORMAT, oldi_format));

	drm_panel_prepare(des->panel);
}

static void
max96752f_bridge_atomic_enable(struct drm_bridge *bridge,
			       struct drm_bridge_state *old_bridge_state)
{
	struct max96752f_bridge *des = to_max96752f_bridge(bridge);

	regmap_update_bits(des->regmap, 0x0002, VID_EN,
			   FIELD_PREP(VID_EN, 1));

	drm_panel_enable(des->panel);
}

static void
max96752f_bridge_atomic_disable(struct drm_bridge *bridge,
				struct drm_bridge_state *old_bridge_state)
{
	struct max96752f_bridge *des = to_max96752f_bridge(bridge);

	drm_panel_disable(des->panel);

	regmap_update_bits(des->regmap, 0x0002, VID_EN,
			   FIELD_PREP(VID_EN, 0));
}

static void
max96752f_bridge_atomic_post_disable(struct drm_bridge *bridge,
				     struct drm_bridge_state *old_bridge_state)
{
	struct max96752f_bridge *des = to_max96752f_bridge(bridge);

	drm_panel_unprepare(des->panel);
}

static u32 *
max96752f_bridge_atomic_get_output_bus_fmts(struct drm_bridge *bridge,
					    struct drm_bridge_state *bridge_state,
					    struct drm_crtc_state *crtc_state,
					    struct drm_connector_state *conn_state,
					    unsigned int *num_output_fmts)
{
	struct drm_connector *connector = conn_state->connector;
	u32 *out_bus_fmts;

	out_bus_fmts = kzalloc(sizeof(*out_bus_fmts), GFP_KERNEL);
	if (!out_bus_fmts) {
		*num_output_fmts = 0;
		return NULL;
	}

	*num_output_fmts = 1;

	if (connector->display_info.num_bus_formats && connector->display_info.bus_formats)
		out_bus_fmts[0] = connector->display_info.bus_formats[0];
	else
		out_bus_fmts[0] = MEDIA_BUS_FMT_RGB888_1X7X4_SPWG;

	return out_bus_fmts;
}

static int max96752f_bridge_attach(struct drm_bridge *bridge,
				   enum drm_bridge_attach_flags flags)
{
	struct max96752f_bridge *des = to_max96752f_bridge(bridge);
	struct drm_connector *connector = &des->connector;
	int ret;

	ret = drm_of_find_panel_or_bridge(bridge->of_node, 1, -1, &des->panel,
					  NULL);
	if (ret)
		return ret;

	connector->polled = DRM_CONNECTOR_POLL_CONNECT |
			    DRM_CONNECTOR_POLL_DISCONNECT;

	drm_connector_helper_add(connector,
				 &max96752f_bridge_connector_helper_funcs);

	ret = drm_connector_init(bridge->dev, connector,
				 &max96752f_bridge_connector_funcs,
				 bridge->type);
	if (ret) {
		DRM_ERROR("Failed to initialize connector\n");
		return ret;
	}

	drm_connector_attach_encoder(connector, bridge->encoder);

	return 0;
}

static enum drm_connector_status
max96752f_bridge_detect(struct drm_bridge *bridge)
{
	struct max96752f_bridge *des = to_max96752f_bridge(bridge);
	struct drm_bridge *prev_bridge = drm_bridge_get_prev_bridge(bridge);

	if (prev_bridge) {
		if (prev_bridge->ops & DRM_BRIDGE_OP_DETECT) {
			if (drm_bridge_detect(prev_bridge) != connector_status_connected) {
				regcache_cache_only(des->regmap, true);
				return connector_status_disconnected;
			}
		}
	}

	return connector_status_connected;
}

static const struct drm_bridge_funcs max96752f_bridge_funcs = {
	.attach = max96752f_bridge_attach,
	.detect = max96752f_bridge_detect,
	.atomic_pre_enable = max96752f_bridge_atomic_pre_enable,
	.atomic_post_disable = max96752f_bridge_atomic_post_disable,
	.atomic_enable = max96752f_bridge_atomic_enable,
	.atomic_disable = max96752f_bridge_atomic_disable,
	.atomic_get_input_bus_fmts = drm_atomic_helper_bridge_propagate_bus_fmt,
	.atomic_get_output_bus_fmts = max96752f_bridge_atomic_get_output_bus_fmts,
	.atomic_duplicate_state = drm_atomic_helper_bridge_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_bridge_destroy_state,
	.atomic_reset = drm_atomic_helper_bridge_reset,
};

static int max96752f_bridge_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct max96752f_bridge *des;

	des = devm_kzalloc(dev, sizeof(*des), GFP_KERNEL);
	if (!des)
		return -ENOMEM;

	des->dev = dev;
	des->parent = dev_get_drvdata(dev->parent);
	platform_set_drvdata(pdev, des);

	des->regmap = dev_get_regmap(dev->parent, NULL);
	if (!des->regmap)
		return dev_err_probe(dev, -ENODEV, "failed to get regmap\n");

	des->bridge.funcs = &max96752f_bridge_funcs;
	des->bridge.of_node = dev->of_node;
	des->bridge.ops = DRM_BRIDGE_OP_DETECT;
	des->bridge.type = DRM_MODE_CONNECTOR_LVDS;

	drm_bridge_add(&des->bridge);

	return 0;
}

static int max96752f_bridge_remove(struct platform_device *pdev)
{
	struct max96752f_bridge *des = platform_get_drvdata(pdev);

	drm_bridge_remove(&des->bridge);

	return 0;
}

static const struct of_device_id max96752f_bridge_of_match[] = {
	{ .compatible = "maxim,max96752f-bridge" },
	{}
};
MODULE_DEVICE_TABLE(of, max96752f_bridge_of_match);

static struct platform_driver max96752f_bridge_driver = {
	.driver = {
		.name = "max96752f-bridge",
		.of_match_table = max96752f_bridge_of_match,
	},
	.probe = max96752f_bridge_probe,
	.remove = max96752f_bridge_remove,
};

module_platform_driver(max96752f_bridge_driver);

MODULE_AUTHOR("Wyon Bi <bivvy.bi@rock-chips.com>");
MODULE_DESCRIPTION("Maxim MAX96752F GMSL2 Deserializer with Dual LVDS (OLDI) Output");
MODULE_LICENSE("GPL");
