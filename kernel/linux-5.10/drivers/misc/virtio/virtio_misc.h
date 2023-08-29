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
#ifndef _VIRTIO_MISC_H
#define _VIRTIO_MISC_H

#ifdef __KERNEL__
#include <uapi/misc/virtio_vop.h>
#include <uapi/misc/virtio_rga.h>
#else
#include "virtio_vop.h"
#include "virtio_rga.h"
#endif

#define DRIVER_NAME		"virtio-misc"

#define VIRTIO_ID_MISC_VOP    103
#define VIRTIO_ID_MISC_RGA    104

/*
 * Device type
 */
enum virtio_misc_device_type {
	VIRTIO_MISC_DEVICE_VOP = 0x0100,
	VIRTIO_MISC_DEVICE_RGA,
};

/*
 * Feature bits
 */
enum {
	/* Guest pages can be used for misc buffers. */
	VIRTIO_MISC_F_RESOURCE_GUEST_PAGES = 0,
	/* The host can process buffers even if they are non-contiguous memory
	   such as scatter-gather lists. */
	VIRTIO_MISC_F_RESOURCE_NON_CONTIG = 1,
	/* Support of vendor virtqueues */
	VIRTIO_MISC_F_VENDOR = 2
};

/*
 * Config
 */
struct virtio_misc_config {
	uint32_t version;
	uint32_t max_resp_length;
};

/*
 * SG list
 */
struct virtio_misc_sg_entry {
	uint64_t addr;
	uint32_t length;
	uint8_t padding[4];
};

struct virtio_misc_sg_list {
	uint32_t num_entries;
	uint8_t padding[4];
	struct virtio_misc_sg_entry entries[];
};

#define VIRTIO_MISC_SG_SIZE(n) \
	offsetof(struct virtio_misc_sg_list, entries[n])

/*
 * Rect
 */
typedef struct {
	uint32_t x;
	uint32_t y;
	uint32_t w;
	uint32_t h;
} vmisc_rect_t;

/*
 * Commands
 */
enum virtio_misc_cmd_type {
	/* Command */
	VIRTIO_VOP_CMD_COMP = 0x0100,
	VIRTIO_VOP_CMD_CREATE_LAYER,
	VIRTIO_VOP_CMD_DESTROY_LAYER,
	VIRTIO_RGA_CMD_BLIT = 0x200,
	VIRTIO_RGA_CMD_GET_HW_VERSION,
	VIRTIO_RGA_CMD_GET_DRIVER_VERSION,
};

struct virtio_misc_cmd_hdr {
	uint32_t type; /* One of enum virtio_misc_cmd_type */
};

/*
 * Events
 */
enum virtio_misc_event_type {
	/* For all devices */
	VIRTIO_MISC_EVENT_ERROR = 0x0100,
};

struct virtio_misc_event {
	uint32_t event_type; /* One of VIRTIO_MISC_EVENT_* types */
};

/*
 * Virtio VOP
 */
struct virtio_vop_comp {
	struct virtio_misc_cmd_hdr hdr;
	struct vvop_plane plane;
};

struct virtio_vop_layer {
	struct virtio_misc_cmd_hdr hdr;
	struct vvop_layer layer;
};

/*
 * Virtio RGA
 */
struct virtio_rga_req {
	struct virtio_misc_cmd_hdr hdr;
	struct rga_req req;
};

struct virtio_rga_driver_version {
	struct virtio_misc_cmd_hdr hdr;
};

struct virtio_rga_hw_version {
	struct virtio_misc_cmd_hdr hdr;
};

struct virtio_rga_driver_version_resp {
	struct virtio_misc_cmd_hdr hdr;
	struct rga_version_t ver;
};

struct virtio_rga_hw_version_resp {
	struct virtio_misc_cmd_hdr hdr;
	struct rga_hw_versions_t ver;
};

#endif
