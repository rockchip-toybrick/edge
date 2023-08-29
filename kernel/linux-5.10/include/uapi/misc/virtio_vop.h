// SPDX-License-Identifier: GPL-2.0+ WITH Linux-syscall-note
/* Driver for virtio vop device.
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
#ifndef VIRTIO_VOP_H
#define VIRTIO_VOP_H

#ifdef __KERNEL__
#include <asm/ioctl.h>
#else
#include <sys/ioctl.h>
#endif

#if defined(__cplusplus)
extern "C" {
#endif

typedef struct {
	float left;
	float top;
	float right;
	float bottom;
} vvop_frect_t;

typedef struct {
	int left;
	int top;
	int right;
	int bottom;
} vvop_rect_t;

typedef struct {
	uint32_t buf_fd;
	uint32_t plane_id;
	uint32_t layer_id;
	vvop_rect_t source_crop;
	vvop_rect_t display_frame;
	uint64_t rotation;
	uint64_t alpha;
	uint64_t blend;
	uint32_t zorder;
	uint32_t format;
} vvop_plane_t;

#define VIRTVOP_MAX_PLANE	8
struct vvop_plane {
	int num_plane;
	vvop_plane_t planes[VIRTVOP_MAX_PLANE];	
};

struct vvop_layer {
	uint32_t layer_id;
};

/*
 * Virtio ioctl
 */
#define VIRTIO_VOP_IOCTL_BASE			'v'

#define VIRTIO_VOP_IOCTL_COMP			\
	_IOWR(VIRTIO_VOP_IOCTL_BASE, 0x100, struct vvop_plane)

#define VIRTIO_VOP_IOCTL_CREATE_LAYER		\
	_IOWR(VIRTIO_VOP_IOCTL_BASE, 0x101, struct vvop_layer)

#define VIRTIO_VOP_IOCTL_DESTROY_LAYER		\
	_IOWR(VIRTIO_VOP_IOCTL_BASE, 0x102, struct vvop_layer)

#if defined(__cplusplus)
}
#endif

#endif
