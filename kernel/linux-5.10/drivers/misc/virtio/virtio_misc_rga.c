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

static const char vrga_driver_name[] = "vrga";

static long vrga_ioctl_blit(struct virtio_misc_device *vmd,
			    unsigned char __user *args,
			    bool sync)
{
	size_t bytes;
	struct rga_req req;

	bytes = copy_from_user(&req, args, sizeof(req));
        if (bytes)
                return -EFAULT;

	return virtio_rga_cmd_blit(vmd, req, sync);
}

static long vrga_ioctl_get_driver_version(struct virtio_misc_device *vmd,
			    unsigned char __user *args)
{
	size_t bytes;
	struct virtio_rga_driver_version_resp *resp;
	long ret;
	
	resp = kzalloc(sizeof(struct virtio_rga_driver_version_resp), GFP_KERNEL);
	if (!resp) {
		MISC_LOGE(vmd, "no memory for resp\n");
		return -EINVAL;
	}
	ret = virtio_rga_cmd_get_driver_version(vmd, resp);
	if (ret < 0)
		goto exit;

	bytes = copy_to_user(args, &resp->ver, sizeof(resp->ver));
        if (bytes) {
                ret = -EFAULT;
		goto exit;
	}

exit:
	kfree(resp);
	return ret;
}

static long vrga_ioctl_get_hw_version(struct virtio_misc_device *vmd,
			    unsigned char __user *args)
{
	size_t bytes;
	struct virtio_rga_hw_version_resp *resp;
	long ret;

	resp = kzalloc(sizeof(struct virtio_rga_hw_version_resp), GFP_KERNEL);
	if (!resp) {
		MISC_LOGE(vmd, "no memory for resp\n");
		return -EINVAL;
	}
	ret = virtio_rga_cmd_get_hw_version(vmd, resp);
	if (ret < 0)
		goto exit;

	bytes = copy_to_user(args, &resp->ver, sizeof(resp->ver));
        if (bytes) {
                ret = -EFAULT;
		goto exit;
	}
exit:
	kfree(resp);
	return ret;
}

static long vrga_ioctl(struct file *file,
		       unsigned int cmd, unsigned long arg)
{
	struct virtio_misc_device *vmd = miscdev_to_vmd(file->private_data);

	pr_debug("vrga: ioctl file=0x%lx, cmd=0x%x, arg=0x%lx\n",
                 (unsigned long)file, cmd, arg);

	switch (cmd) {
	case RGA_BLIT_SYNC:
		return vrga_ioctl_blit(vmd, (unsigned char __user *)arg, true);
	case RGA_BLIT_ASYNC:
		return vrga_ioctl_blit(vmd, (unsigned char __user *)arg, false);
	case RGA_IOC_GET_DRVIER_VERSION:
		return vrga_ioctl_get_driver_version(vmd, (unsigned char __user *)arg);
	case RGA_IOC_GET_HW_VERSION:
		return vrga_ioctl_get_hw_version(vmd, (unsigned char __user *)arg);
	default:
		pr_warn("vrga: unknown ioctl 0x%x\n", cmd);
		return -EINVAL;
	};
}

static int vrga_open(struct inode *inode, struct file *file)
{
	return 0;
}

static int vrga_release(struct inode *inode, struct file *file)
{
	return 0;
}

static const struct file_operations vrga_fops = {
	.owner          = THIS_MODULE,
	.unlocked_ioctl = vrga_ioctl,
	.open           = vrga_open,
	.release        = vrga_release,
};

int virtio_misc_rga_init(struct virtio_misc_device *vmd)
{
	vmd->mdev.name = vrga_driver_name;
	vmd->mdev.minor = MISC_DYNAMIC_MINOR;
	vmd->mdev.fops = &vrga_fops;
	vmd->mdev.mode = 0666;

	return 0;
};
