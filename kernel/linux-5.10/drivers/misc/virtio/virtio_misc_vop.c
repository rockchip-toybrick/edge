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

#include "virtio_priv.h"

static const char vvop_driver_name[] = "vvop";

static long vvop_ioctl_comp(struct virtio_misc_device *vmd,
		unsigned char __user *args)
{
	size_t bytes;
	struct vvop_plane planes;

	bytes = copy_from_user(&planes, args, sizeof(planes));
        if (bytes)
                return -EFAULT;

	return (long)virtio_vop_cmd_comp(vmd, planes);
}

static long vvop_ioctl_create_layer(struct virtio_misc_device *vmd,
		unsigned char __user *args)
{
	size_t bytes;
	struct vvop_layer layer;

	bytes = copy_from_user(&layer, args, sizeof(layer));
        if (bytes)
                return -EFAULT;

	return (long)virtio_vop_cmd_create_layer(vmd, layer);
}

static long vvop_ioctl_destroy_layer(struct virtio_misc_device *vmd,
		unsigned char __user *args)
{
	size_t bytes;
	struct vvop_layer layer;

	bytes = copy_from_user(&layer, args, sizeof(layer));
        if (bytes)
                return -EFAULT;

	return (long)virtio_vop_cmd_destroy_layer(vmd, layer);
}

static long vvop_ioctl(struct file *file,
		       unsigned int cmd, unsigned long arg)
{
	struct virtio_misc_device *vmd = miscdev_to_vmd(file->private_data);

	pr_debug("vvop: ioctl file=0x%lx, cmd=0x%x, arg=0x%lx\n",
                 (unsigned long)file, cmd, arg);

	switch (cmd) {
	case VIRTIO_VOP_IOCTL_COMP:
		return vvop_ioctl_comp(vmd, (unsigned char __user *)arg);
	case VIRTIO_VOP_IOCTL_CREATE_LAYER:
		return vvop_ioctl_create_layer(vmd, (unsigned char __user *)arg);
	case VIRTIO_VOP_IOCTL_DESTROY_LAYER:
		return vvop_ioctl_destroy_layer(vmd, (unsigned char __user *)arg);
	default:
		pr_warn("vvop: unknown ioctl 0x%x\n", cmd);
		return -EINVAL;
	};
}

static int vvop_open(struct inode *inode, struct file *file)
{
	return 0;
}

static int vvop_release(struct inode *inode, struct file *file)
{
	return 0;
}

static const struct file_operations vvop_fops = {
	.owner          = THIS_MODULE,
	.unlocked_ioctl = vvop_ioctl,
	.open           = vvop_open,
	.release        = vvop_release,
};

int virtio_misc_vop_init(struct virtio_misc_device *vmd)
{
	vmd->mdev.name = vvop_driver_name;
	vmd->mdev.minor = MISC_DYNAMIC_MINOR;
	vmd->mdev.fops = &vvop_fops;
	vmd->mdev.mode = 0666;

	return 0;
};
