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
#ifndef _VIRTIO_PRIV_H
#define _VIRTIO_PRIV_H

#include <linux/virtio.h>
#include <linux/virtio_ids.h>
#include <linux/virtio_config.h>
#include <linux/list.h>
#include <linux/completion.h>
#include <linux/fcntl.h>
#include <linux/slab.h>
#include <linux/poll.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/delay.h>
#include <linux/uaccess.h>
#include <linux/io.h>
#include <linux/miscdevice.h>
#include <linux/dma-buf.h>
#include <drm/drm_prime.h>

#include "virtio_misc.h"

#define MISC_DEBUG

#ifdef MISC_DEBUG
#define MISC_LOGD(vmd, fmt, ...) \
        dev_info(vmd->dev, "%s: %s - " fmt, vmd->device_type, __FUNCTION__, ##__VA_ARGS__)
#else
#define MISC_LOGD(vmd, fmt, ...) \
        dev_debug(vmd->dev, "%s: %s - " fmt, vmd->device_type, __FUNCTION__, ##__VA_ARGS__)
#endif
#define MISC_LOGI(vmd, fmt, ...) \
        dev_info(vmd->dev, "%s: %s - " fmt, vmd->device_type, __FUNCTION__, ##__VA_ARGS__)
#define MISC_LOGW(vmd, fmt, ...) \
        dev_warn(vmd->dev, "%s: %s - " fmt, vmd->device_type, __FUNCTION__, ##__VA_ARGS__)
#define MISC_LOGE(vmd, fmt, ...) \
        dev_err(vmd->dev, "%s: %s - " fmt, vmd->device_type, __FUNCTION__, ##__VA_ARGS__)

#define miscdev_to_vmd(d) container_of(d, struct virtio_misc_device, mdev)

struct virtio_misc_device;
struct virtio_misc_vbuffer;
typedef void (*virtio_misc_resp_cb)(struct virtio_misc_device *vmd,
				    struct virtio_misc_vbuffer *vbuf);

struct virtio_misc_vbuffer {
	char *buf;
	int size;
	uint32_t id;

	void *src_buf;
	uint32_t src_size;

	void *dst_buf;
	uint32_t dst_size;

	void *pat_buf;
	uint32_t pat_size;

	char *resp_buf;
	int resp_size;

	void *priv;
	virtio_misc_resp_cb resp_cb;

	bool is_sync;
	struct completion reclaimed;

	struct list_head pending_list_entry;
};

struct virtio_misc_cmd_queue {
	struct virtqueue *vq;
	bool ready;
	spinlock_t qlock;
	wait_queue_head_t reclaim_queue;
};

struct virtio_misc_event_queue {
	struct virtqueue *vq;
	bool ready;
	struct work_struct work;
};

struct virtio_misc_map_buffer {
	enum dma_data_direction dir;
	struct dma_buf *dma_buf;
        struct dma_buf_attachment *attach;
        struct sg_table *sgt;
};

struct virtio_misc_device {
        struct virtio_device *vdev;
	struct miscdevice mdev;
	struct device *dev;
	struct virtio_misc_cmd_queue commandq;
	struct virtio_misc_event_queue eventq;

	struct kmem_cache *vbufs;
	struct virtio_misc_event *evts;

	struct idr resource_idr;
	spinlock_t resource_idr_lock;

	spinlock_t pending_buf_list_lock;
	uint32_t vbufs_sent;
	struct list_head pending_vbuf_list;

	uint32_t max_resp_len;

	bool supp_non_contig;
	uint32_t type;

	char device_type[16];
};

void virtio_misc_process_events(struct work_struct *work);
int virtio_misc_alloc_events(struct virtio_misc_device *vmd);
void virtio_misc_cmd_cb(struct virtqueue *vq);
void virtio_misc_event_cb(struct virtqueue *vq);

int virtio_misc_alloc_vbufs(struct virtio_misc_device *vmd);
void virtio_misc_free_vbufs(struct virtio_misc_device *vmd);
int virtio_misc_alloc_events(struct virtio_misc_device *vmd);

void virtio_misc_handle_error(struct virtio_misc_device *vmd);
int virtio_misc_device_init(struct virtio_misc_device *vmd);
void virtio_misc_device_deinit(struct virtio_misc_device *vmd);

int virtio_misc_vop_init(struct virtio_misc_device *vmd);
int virtio_misc_rga_init(struct virtio_misc_device *vmd);

int virtio_vop_cmd_comp(struct virtio_misc_device *vmd,
                        struct vvop_plane args);
int virtio_vop_cmd_create_layer(struct virtio_misc_device *vmd,
                        struct vvop_layer args);
int virtio_vop_cmd_destroy_layer(struct virtio_misc_device *vmd,
                        struct vvop_layer args);

int virtio_rga_cmd_blit(struct virtio_misc_device *vmd,
			struct rga_req args, bool sync);
int virtio_rga_cmd_get_driver_version(struct virtio_misc_device *vmd,
				      struct virtio_rga_driver_version_resp *resp);
int virtio_rga_cmd_get_hw_version(struct virtio_misc_device *vmd,
				  struct virtio_rga_hw_version_resp * resp);
#endif
