// SPDX-License-Identifier: GPL-2.0+
/* Driver for virtio misc device.
 *
 * Copyright (C) 2023 Rockchip Electronics Co., Ltd.
 *
 * Based on drivers drivers/video/virtio/virtio_video
 * Copyright 2020 OpenSynergy GmbH.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>

#include "virtio_priv.h"

static int virtio_misc_probe(struct virtio_device *vdev)
{
	int ret = 0;
	const char *device_type;
	struct virtio_misc_device *vmd;
	struct virtqueue *vqs[2];
	struct device *dev = &vdev->dev;
	static const char * const names[] = { "commandq", "eventq" };
	static vq_callback_t *callbacks[] = {
		virtio_misc_cmd_cb,
		virtio_misc_event_cb
        };

	if (!virtio_has_feature(vdev, VIRTIO_MISC_F_RESOURCE_GUEST_PAGES)) {
		dev_err(dev, "device must support guest allocated buffers\n");
		return -ENODEV;
	}

	ret = dma_coerce_mask_and_coherent(dev, DMA_BIT_MASK(64));
	if (ret) {
		dev_err(dev, "dma_coerce_mask_and_coherent failed, ret %d\n", ret);
		return ret;
	}

	vmd = devm_kzalloc(dev, sizeof(*vmd), GFP_KERNEL);
	if (!vmd)
		return -ENOMEM;

	switch (vdev->id.device) {
	case VIRTIO_ID_MISC_VOP:
		vmd->type = VIRTIO_MISC_DEVICE_VOP;
		strcpy(vmd->device_type, "vvop");
		break;
	case VIRTIO_ID_MISC_RGA:
		vmd->type = VIRTIO_MISC_DEVICE_RGA;
		strcpy(vmd->device_type, "vrga");
		break;
	default:
		vmd->type = VIRTIO_MISC_DEVICE_VOP;
		break;
	}
	vmd->vdev = vdev;
	vmd->dev = dev;
	vdev->priv = vmd;

	spin_lock_init(&vmd->pending_buf_list_lock);
	spin_lock_init(&vmd->resource_idr_lock);
	idr_init(&vmd->resource_idr);

	if (virtio_has_feature(vdev, VIRTIO_MISC_F_RESOURCE_NON_CONTIG))
		vmd->supp_non_contig = true;

	spin_lock_init(&vmd->commandq.qlock);
 	init_waitqueue_head(&vmd->commandq.reclaim_queue);

	INIT_WORK(&vmd->eventq.work, virtio_misc_process_events);

	INIT_LIST_HEAD(&vmd->pending_vbuf_list);

	ret = virtio_find_vqs(vdev, 2, vqs, callbacks, names, NULL);
 	if (ret < 0) {
		MISC_LOGE(vmd, "failed to find virt queues\n");
		goto err_vqs;
	}

	vmd->commandq.vq = vqs[0];
	vmd->eventq.vq = vqs[1];

	ret = virtio_misc_alloc_vbufs(vmd);
	if (ret < 0) {
		MISC_LOGE(vmd, "failed to alloc vbufs\n");
		goto err_vbufs;
	}

	virtio_cread(vdev, struct virtio_misc_config, max_resp_length,
		     &vmd->max_resp_len);
	if (!vmd->max_resp_len) {
		MISC_LOGE(vmd, "max_resp_len is zero\n");
		ret = -EINVAL;
		goto err_config;
	}

	ret = virtio_misc_alloc_events(vmd);
	if (ret < 0)
		goto err_events;

	virtio_device_ready(vdev);
	vmd->commandq.ready = true;
	vmd->eventq.ready = true;

	ret = virtio_misc_device_init(vmd);
	if (ret < 0) {
		MISC_LOGE(vmd, "fail to init virtio misc\n");
		goto err_init;
	}

	MISC_LOGD(vmd, "%s initialize successfully, max_resp_len %d\n",
			device_type, vmd->max_resp_len);
	return 0;

err_init:
err_events:
err_config:
	virtio_misc_free_vbufs(vmd);
err_vbufs:
	vdev->config->del_vqs(vdev);
err_vqs:
	devm_kfree(&vdev->dev, vmd);
	return ret;
}

static void virtio_misc_remove(struct virtio_device *vdev)
{
	struct virtio_misc_device *vmd = vdev->priv;

	virtio_misc_device_deinit(vmd);
	devm_kfree(&vdev->dev, vmd);
}

static struct virtio_device_id id_table[] = {
        { VIRTIO_ID_MISC_VOP, VIRTIO_DEV_ANY_ID },
        { VIRTIO_ID_MISC_RGA, VIRTIO_DEV_ANY_ID },
        { 0 },
};

static unsigned int features[] = {
        VIRTIO_MISC_F_RESOURCE_GUEST_PAGES,
        VIRTIO_MISC_F_RESOURCE_NON_CONTIG,
};

static struct virtio_driver virtio_misc_driver = {
        .feature_table = features,
        .feature_table_size = ARRAY_SIZE(features),
        .driver.name = DRIVER_NAME,
        .driver.owner = THIS_MODULE,
        .id_table = id_table,
        .probe = virtio_misc_probe,
        .remove = virtio_misc_remove,
};

module_virtio_driver(virtio_misc_driver);

MODULE_DEVICE_TABLE(virtio, id_table);
MODULE_DESCRIPTION("virtio misc driver");
MODULE_AUTHOR("Addy Ke <addy.ke@rock-chips.com>");
MODULE_LICENSE("GPL");
