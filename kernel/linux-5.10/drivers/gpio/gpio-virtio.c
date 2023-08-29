// SPDX-License-Identifier: GPL-2.0+
/*
 * GPIO driver for virtio-based virtual GPIO controllers
 *
 * Copyright (C) 2021 metux IT consult
 * Enrico Weigelt, metux IT consult <info@metux.net>
 *
 * Copyright (C) 2021 Linaro.
 * Viresh Kumar <viresh.kumar@linaro.org>
 */

#include <linux/completion.h>
#include <linux/err.h>
#include <linux/gpio/driver.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/virtio_config.h>
#include <uapi/linux/virtio_gpio.h>
#include <uapi/linux/virtio_ids.h>
#include <linux/kthread.h>
#include <linux/delay.h>

#define EVENTS_MAX	64

struct virtio_gpio_line {
	struct mutex lock; /* Protects line operation */
	struct completion completion;
	struct virtio_gpio_request req ____cacheline_aligned;
	struct virtio_gpio_response res ____cacheline_aligned;
	unsigned int rxlen;
};

struct virtio_gpio {
	struct virtio_device *vdev;
	struct mutex lock; /* Protects virtqueue operation */
	struct gpio_chip gc;
	struct virtio_gpio_config config;
	struct virtio_gpio_line *lines;
	struct virtqueue *request_vq;
	struct virtqueue *event_vq;
	struct irq_domain *domain;
	struct virtio_gpio_event *events;
	unsigned int *virqs;
	unsigned int en;
};

static int _virtio_gpio_req(struct virtio_gpio *vgpio, u16 type, u16 gpio,
			    u8 txvalue, u8 *rxvalue, void *response, u32 rxlen)
{
	struct virtio_gpio_line *line = &vgpio->lines[gpio];
	struct virtio_gpio_request *req = &line->req;
	struct virtio_gpio_response *res = response;
	struct scatterlist *sgs[2], req_sg, res_sg;
	struct device *dev = &vgpio->vdev->dev;
	int ret;

	/*
	 * Prevent concurrent requests for the same line since we have
	 * pre-allocated request/response buffers for each GPIO line. Moreover
	 * Linux always accesses a GPIO line sequentially, so this locking shall
	 * always go through without any delays.
	 */
	mutex_lock(&line->lock);

	req->type = cpu_to_le16(type);
	req->gpio = cpu_to_le16(gpio);
	req->value = txvalue;

	sg_init_one(&req_sg, req, sizeof(*req));
	sg_init_one(&res_sg, res, rxlen);
	sgs[0] = &req_sg;
	sgs[1] = &res_sg;

	line->rxlen = 0;
	reinit_completion(&line->completion);

	/*
	 * Virtqueue callers need to ensure they don't call its APIs with other
	 * virtqueue operations at the same time.
	 */
	mutex_lock(&vgpio->lock);
	ret = virtqueue_add_sgs(vgpio->request_vq, sgs, 1, 1, line, GFP_KERNEL);
	if (ret) {
		dev_err(dev, "failed to add request to vq\n");
		mutex_unlock(&vgpio->lock);
		goto out;
	}

	virtqueue_kick(vgpio->request_vq);
	mutex_unlock(&vgpio->lock);

	if (!wait_for_completion_timeout(&line->completion, HZ)) {
		dev_err(dev, "GPIO operation timed out\n");
		ret = -ETIMEDOUT;
		goto out;
	}

	if (unlikely(res->status != VIRTIO_GPIO_STATUS_OK)) {
		dev_err(dev, "GPIO request failed: %d\n", gpio);
		ret = -EINVAL;
		goto out;
	}

	if (unlikely(line->rxlen != rxlen)) {
		dev_err(dev, "GPIO operation returned incorrect len (%u : %u)\n",
			rxlen, line->rxlen);
		ret = -EINVAL;
		goto out;
	}

	if (rxvalue)
		*rxvalue = res->value;

out:
	mutex_unlock(&line->lock);

	return ret;
}

static int virtio_gpio_req(struct virtio_gpio *vgpio, u16 type, u16 gpio,
			   u8 txvalue, u8 *rxvalue)
{
	struct virtio_gpio_line *line = &vgpio->lines[gpio];
	struct virtio_gpio_response *res = &line->res;

	return _virtio_gpio_req(vgpio, type, gpio, txvalue, rxvalue, res,
				sizeof(*res));
}

static void virtio_gpio_free(struct gpio_chip *gc, unsigned int gpio)
{
	struct virtio_gpio *vgpio = gpiochip_get_data(gc);

	virtio_gpio_req(vgpio, VIRTIO_GPIO_MSG_SET_DIRECTION, gpio,
			VIRTIO_GPIO_DIRECTION_NONE, NULL);
}

static int virtio_gpio_get_direction(struct gpio_chip *gc, unsigned int gpio)
{
	struct virtio_gpio *vgpio = gpiochip_get_data(gc);
	u8 direction;
	int ret;

	ret = virtio_gpio_req(vgpio, VIRTIO_GPIO_MSG_GET_DIRECTION, gpio, 0,
			      &direction);
	if (ret)
		return ret;

	switch (direction) {
	case VIRTIO_GPIO_DIRECTION_IN:
		return GPIO_LINE_DIRECTION_IN;
	case VIRTIO_GPIO_DIRECTION_OUT:
		return GPIO_LINE_DIRECTION_OUT;
	default:
		return -EINVAL;
	}
}

static int virtio_gpio_direction_input(struct gpio_chip *gc, unsigned int gpio)
{
	struct virtio_gpio *vgpio = gpiochip_get_data(gc);

	return virtio_gpio_req(vgpio, VIRTIO_GPIO_MSG_SET_DIRECTION, gpio,
			       VIRTIO_GPIO_DIRECTION_IN, NULL);
}

static int virtio_gpio_direction_output(struct gpio_chip *gc, unsigned int gpio,
					int value)
{
	struct virtio_gpio *vgpio = gpiochip_get_data(gc);
	int ret;

	ret = virtio_gpio_req(vgpio, VIRTIO_GPIO_MSG_SET_VALUE, gpio, value, NULL);
	if (ret)
		return ret;

	return virtio_gpio_req(vgpio, VIRTIO_GPIO_MSG_SET_DIRECTION, gpio,
			       VIRTIO_GPIO_DIRECTION_OUT, NULL);
}

static int virtio_gpio_get(struct gpio_chip *gc, unsigned int gpio)
{
	struct virtio_gpio *vgpio = gpiochip_get_data(gc);
	u8 value;
	int ret;

	ret = virtio_gpio_req(vgpio, VIRTIO_GPIO_MSG_GET_VALUE, gpio, 0, &value);
	return ret ? ret : value;
}

static void virtio_gpio_set(struct gpio_chip *gc, unsigned int gpio, int value)
{
	struct virtio_gpio *vgpio = gpiochip_get_data(gc);

	virtio_gpio_req(vgpio, VIRTIO_GPIO_MSG_SET_VALUE, gpio, value, NULL);
}

static int virtio_gpio_irq_map(struct virtio_gpio *vgpio, unsigned int gpio)
{
	u8 value;
	int ret;

	ret = virtio_gpio_req(vgpio, VIRTIO_GPIO_MSG_IRQ_MAP, gpio, 0, &value);
	return ret ? ret : value;
}

static void virtio_gpio_request_vq(struct virtqueue *vq)
{
	struct virtio_gpio_line *line;
	unsigned int len;

	do {
		line = virtqueue_get_buf(vq, &len);
		if (!line)
			return;

		line->rxlen = len;

		complete(&line->completion);
	} while (1);
}

static void virtio_gpio_free_vqs(struct virtio_device *vdev)
{
	vdev->config->reset(vdev);
	vdev->config->del_vqs(vdev);
}

static void virtinput_queue_evtbuf(struct virtqueue *vq,
				   struct virtio_gpio_event *evtbuf)
{
	struct scatterlist sg[1];

	sg_init_one(sg, evtbuf, sizeof(*evtbuf));
	virtqueue_add_inbuf(vq, sg, 1, evtbuf, GFP_ATOMIC);
}

static void virtio_gpio_event_vq(struct virtqueue *vq)
{
	struct virtio_gpio *vgpio = vq->vdev->priv;
	struct virtio_gpio_event *event;
	unsigned int len;

	while ((event = virtqueue_get_buf(vq, &len)) != NULL) {
		int virq = vgpio->virqs[event->value];
		if (virq)
			generic_handle_irq(virq);
		virtinput_queue_evtbuf(vq, event);
	}
	virtqueue_kick(vq);
}

static int virtio_gpio_alloc_vqs(struct virtio_gpio *vgpio,
				 struct virtio_device *vdev)
{
	const char * const names[] = { "requestq", "eventq" };
	vq_callback_t *cbs[] = {
		virtio_gpio_request_vq, virtio_gpio_event_vq
	};
	struct virtqueue *vqs[2] = { NULL, NULL };
	int ret;
	int size;
	int i;

	ret = virtio_find_vqs(vdev, 2, vqs, cbs, names, NULL);
	if (ret) {
		dev_err(&vdev->dev, "failed to find vqs: %d\n", ret);
		return ret;
	}

	if (!vqs[0]) {
		dev_err(&vdev->dev, "failed to find requestq vq\n");
		return -ENODEV;
	}

	if (!vqs[1]) {
		dev_err(&vdev->dev, "failed to find eventq vq\n");
		return -ENODEV;
	}

	vgpio->request_vq = vqs[0];
	vgpio->event_vq = vqs[1];

	size = virtqueue_get_vring_size(vgpio->event_vq);
	if (size > EVENTS_MAX)
		size = EVENTS_MAX;
	printk("%s size = %d\n", __func__, size);
	for (i = 0; i < size; i++)
		virtinput_queue_evtbuf(vgpio->event_vq, &vgpio->events[i]);
	virtqueue_kick(vgpio->event_vq);

	return 0;
}

static const char **virtio_gpio_get_names(struct virtio_gpio *vgpio)
{
	struct virtio_gpio_config *config = &vgpio->config;
	struct virtio_gpio_response_get_names *res;
	struct device *dev = &vgpio->vdev->dev;
	u8 *gpio_names, *str;
	const char **names;
	int i, ret, len;

	if (!config->gpio_names_size)
		return NULL;

	len = sizeof(*res) + config->gpio_names_size;
	res = devm_kzalloc(dev, len, GFP_KERNEL);
	if (!res)
		return NULL;
	gpio_names = res->value;

	ret = _virtio_gpio_req(vgpio, VIRTIO_GPIO_MSG_GET_NAMES, 0, 0, NULL,
			       res, len);
	if (ret) {
		dev_err(dev, "Failed to get GPIO names: %d\n", ret);
		return NULL;
	}

	names = devm_kcalloc(dev, config->ngpio, sizeof(names), GFP_KERNEL);
	if (!names)
		return NULL;

	/* NULL terminate the string instead of checking it */
	gpio_names[config->gpio_names_size - 1] = '\0';

	for (i = 0, str = gpio_names; i < config->ngpio; i++) {
		names[i] = str;
		str += strlen(str) + 1; /* zero-length strings are allowed */

		if (str > gpio_names + config->gpio_names_size) {
			dev_err(dev, "gpio_names block is too short (%d)\n", i);
			return NULL;
		}
	}

	return names;
}

static int virtio_gpio_to_irq(struct gpio_chip *gc, unsigned int offset)
{
	struct virtio_gpio *vgpio = gpiochip_get_data(gc);
	unsigned int virq = 0;

	if (!vgpio->domain)
		return -ENXIO;

	virq = irq_create_mapping(vgpio->domain, offset);
	return (virq) ? : -ENXIO;
}

static void virtio_irq_irq_unmask(struct irq_data *d)
{
	unsigned int value;
	struct irq_chip_generic *gc = irq_data_get_irq_chip_data(d);
	struct virtio_gpio *vgpio = gc->private;

	value = VIRTIO_GPIO_CMD_IRQ_UNMASK << 24 | d->hwirq << 16;
	virtio_cwrite32(vgpio->vdev, 0, value);
}

static void virtio_irq_irq_mask(struct irq_data *d)
{
	unsigned int value;
	struct irq_chip_generic *gc = irq_data_get_irq_chip_data(d);
	struct virtio_gpio *vgpio = gc->private;

	value = VIRTIO_GPIO_CMD_IRQ_MASK << 24 | d->hwirq << 16;
	virtio_cwrite32(vgpio->vdev, 0, value);
}


static int virtio_irq_set_type(struct irq_data *d, unsigned int type)
{
	unsigned int value;
	struct irq_chip_generic *gc = irq_data_get_irq_chip_data(d);
	struct virtio_gpio *vgpio = gc->private;
	int ret = 0;

	value = VIRTIO_GPIO_CMD_IRQ_SET_TYPE << 24 | d->hwirq << 16 | type;
	virtio_cwrite32(vgpio->vdev, 0, value);

	return ret;
}

static void virtio_irq_enable(struct irq_data *d)
{
	unsigned int value;
	struct irq_chip_generic *gc = irq_data_get_irq_chip_data(d);
	struct virtio_gpio *vgpio = gc->private;

	value = VIRTIO_GPIO_CMD_IRQ_SW << 24 | d->hwirq << 16 | 1;
	virtio_cwrite32(vgpio->vdev, 0, value);
}

static void virtio_irq_disable(struct irq_data *d)
{
	unsigned int value;
	struct irq_chip_generic *gc = irq_data_get_irq_chip_data(d);
	struct virtio_gpio *vgpio = gc->private;
	
	value = VIRTIO_GPIO_CMD_IRQ_SW << 24 | d->hwirq << 16 | 0;
	virtio_cwrite32(vgpio->vdev, 0, value);
}

static int virtio_gpio_irq_domain_map(struct irq_domain *d, unsigned int virq,
                                 irq_hw_number_t hw_irq)
{
	struct irq_chip_generic *gc;
	struct virtio_gpio *vgpio;
	struct irq_data *data = irq_domain_get_irq_data(d, virq);

	irq_generic_chip_ops.map(d, virq, hw_irq);
	gc = irq_data_get_irq_chip_data(data);
	vgpio = gc->private;
	if (virtio_gpio_irq_map(vgpio, hw_irq) == 1) {
		vgpio->virqs[hw_irq] = virq;
		return 0;
	}

	return -ENOTSUPP;
}

static void virtio_gpio_irq_domain_unmap(struct irq_domain *d, unsigned int virq)
{
	struct irq_data *data = irq_domain_get_irq_data(d, virq);
	struct irq_chip_generic *gc = irq_data_get_irq_chip_data(data);
	struct virtio_gpio *vgpio = gc->private;
	unsigned int hw_irq = data->hwirq;

	vgpio->virqs[hw_irq] = 0;
	irq_generic_chip_ops.unmap(d, virq);
}

static const struct irq_domain_ops virtio_gpio_irq_domain_ops = {
	.map    = virtio_gpio_irq_domain_map,
	.unmap  = virtio_gpio_irq_domain_unmap,
	.xlate  = irq_domain_xlate_onetwocell,
};

static int virtio_gpio_interrupts_register(struct virtio_gpio *vgpio)
{
	struct device *dev = &vgpio->vdev->dev;
	unsigned int clr = IRQ_NOREQUEST | IRQ_NOPROBE | IRQ_NOAUTOEN;
	struct irq_chip_generic *gc;
	int ret;
	vgpio->virqs = devm_kcalloc(&vgpio->vdev->dev, vgpio->config.ngpio, sizeof(*vgpio->virqs), GFP_KERNEL);
	if (!vgpio->virqs) {
		dev_err(dev, "Malloc virqs fail\n");
		return -EINVAL;
	}
	vgpio->domain = irq_domain_add_linear(vgpio->gc.of_node, vgpio->config.ngpio,
					&virtio_gpio_irq_domain_ops, NULL);
	if (!vgpio->domain) {
		dev_err(dev, "Could not init irq domain\n");
		return -EINVAL;
	}
	ret = irq_alloc_domain_generic_chips(vgpio->domain, 32, 1,
					     "virtio_gpio_irq",
					     handle_level_irq,
					     clr, 0, 0);
	if (ret) {
		dev_err(dev, "Could not alloc generic chips\n");
		irq_domain_remove(vgpio->domain);
		return -EINVAL;
	}
	gc = irq_get_domain_generic_chip(vgpio->domain, 0);

	gc->private = vgpio;

	gc->chip_types[0].chip.irq_mask = virtio_irq_irq_mask;
	gc->chip_types[0].chip.irq_unmask = virtio_irq_irq_unmask;
	gc->chip_types[0].chip.irq_enable = virtio_irq_enable;
	gc->chip_types[0].chip.irq_disable = virtio_irq_disable;
	gc->chip_types[0].chip.irq_set_type = virtio_irq_set_type;
	gc->chip_types[0].chip.flags = IRQCHIP_SKIP_SET_WAKE;
	gc->mask_cache = 0xffffffff;

	return 0;
}

static void virtio_gpio_config_changed(struct virtio_device *vdev)
{
	__u32 pend; 
	struct virtio_gpio *vgpio = vdev->priv;

	virtio_cread_bytes(vdev, 4, &pend, sizeof(__u32));

	while (pend) {
		unsigned int irq, virq;
		irq = __ffs(pend);
		pend &= ~BIT(irq);
		virq = irq_find_mapping(vgpio->domain, irq);
		if (virq)
			generic_handle_irq(virq);
		else 
			dev_err(&vdev->dev, "unmapped irq %d\n", irq);
	}
}

static int virtio_gpio_probe(struct virtio_device *vdev)
{
	struct virtio_gpio_config *config;
	struct device *dev = &vdev->dev;
	struct virtio_gpio *vgpio;
	int ret, i;

	vgpio = devm_kzalloc(dev, sizeof(*vgpio), GFP_KERNEL);
	if (!vgpio)
		return -ENOMEM;

	config = &vgpio->config;

	/* Read configuration */
	virtio_cread_bytes(vdev, 0, config, sizeof(*config));
	config->gpio_names_size = le32_to_cpu(config->gpio_names_size);
	config->ngpio = le16_to_cpu(config->ngpio);
	if (!config->ngpio) {
		dev_err(dev, "Number of GPIOs can't be zero\n");
		return -EINVAL;
	}

	vgpio->lines = devm_kcalloc(dev, config->ngpio, sizeof(*vgpio->lines), GFP_KERNEL);
	if (!vgpio->lines)
		return -ENOMEM;

	vgpio->events = devm_kcalloc(dev, 64, sizeof(*vgpio->events), GFP_KERNEL);
	if (!vgpio->events)
		return -ENOMEM;

	for (i = 0; i < config->ngpio; i++) {
		mutex_init(&vgpio->lines[i].lock);
		init_completion(&vgpio->lines[i].completion);
	}

	mutex_init(&vgpio->lock);
	vdev->priv = vgpio;

	vgpio->vdev			= vdev;
	vgpio->gc.free			= virtio_gpio_free;
	vgpio->gc.get_direction		= virtio_gpio_get_direction;
	vgpio->gc.direction_input	= virtio_gpio_direction_input;
	vgpio->gc.direction_output	= virtio_gpio_direction_output;
	vgpio->gc.to_irq		= virtio_gpio_to_irq,
	vgpio->gc.get			= virtio_gpio_get;
	vgpio->gc.set			= virtio_gpio_set;
	vgpio->gc.ngpio			= config->ngpio;
	vgpio->gc.base			= -1; /* Allocate base dynamically */
	vgpio->gc.label			= dev_name(dev);
	vgpio->gc.parent		= dev;
	vgpio->gc.owner			= THIS_MODULE;
	vgpio->gc.can_sleep		= false;

#ifdef CONFIG_OF_GPIO
	vgpio->gc.of_node = of_node_get(dev->parent->of_node);
#endif

	ret = virtio_gpio_alloc_vqs(vgpio, vdev);
	if (ret)
		return ret;

	/* Mark the device ready to perform operations from within probe() */
	virtio_device_ready(vdev);

	vgpio->gc.names = virtio_gpio_get_names(vgpio);

	ret = gpiochip_add_data(&vgpio->gc, vgpio);
	if (ret) {
		virtio_gpio_free_vqs(vdev);
		dev_err(dev, "Failed to add virtio-gpio controller\n");
	}
	virtio_gpio_interrupts_register(vgpio);

	return ret;
}

static void virtio_gpio_remove(struct virtio_device *vdev)
{
	struct virtio_gpio *vgpio = vdev->priv;

	gpiochip_remove(&vgpio->gc);
	virtio_gpio_free_vqs(vdev);
}

static const struct virtio_device_id id_table[] = {
	{ VIRTIO_ID_GPIO, VIRTIO_DEV_ANY_ID },
	{},
};
MODULE_DEVICE_TABLE(virtio, id_table);

static struct virtio_driver virtio_gpio_driver = {
	.id_table		= id_table,
	.probe			= virtio_gpio_probe,
	.remove			= virtio_gpio_remove,
	.driver			= {
		.name		= KBUILD_MODNAME,
		.owner		= THIS_MODULE,
	},
	.config_changed	= virtio_gpio_config_changed,
};
module_virtio_driver(virtio_gpio_driver);

MODULE_AUTHOR("Enrico Weigelt, metux IT consult <info@metux.net>");
MODULE_AUTHOR("Viresh Kumar <viresh.kumar@linaro.org>");
MODULE_AUTHOR("Jinkun Hong <jinkun.hong@rock-chips.com>");
MODULE_DESCRIPTION("VirtIO GPIO driver");
MODULE_LICENSE("GPL");
