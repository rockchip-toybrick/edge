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

void virtio_misc_handle_error(struct virtio_misc_device *vmd)
{
	MISC_LOGD(vmd, "enter\n");
}

int virtio_misc_device_init(struct virtio_misc_device *vmd)
{
	int ret;

	switch (vmd->type) {
	case VIRTIO_MISC_DEVICE_VOP:
		virtio_misc_vop_init(vmd);
		break;
	case VIRTIO_MISC_DEVICE_RGA:
		virtio_misc_rga_init(vmd);
		break;
	}

	ret = misc_register(&vmd->mdev);
	if (ret < 0) {
		MISC_LOGE(vmd, "misc_register failed\n");
		return ret;
	}

	return 0;
}

void virtio_misc_device_deinit(struct virtio_misc_device *vmd)
{
	misc_deregister(&vmd->mdev);
}
