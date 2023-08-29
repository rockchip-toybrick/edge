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

#define MAX_INLINE_CMD_SIZE   712
#define MAX_INLINE_RESP_SIZE  298
#define VBUFFER_SIZE		(sizeof(struct virtio_misc_vbuffer) \
				+ MAX_INLINE_CMD_SIZE               \
				+ MAX_INLINE_RESP_SIZE)

static bool vbuf_is_pending(struct virtio_misc_device *vmd,
			    struct virtio_misc_vbuffer *vbuf)
{
	struct virtio_misc_vbuffer *entry;

	list_for_each_entry(entry, &vmd->pending_vbuf_list, pending_list_entry)
	{
		if (entry == vbuf && entry->id == vbuf->id)
			return true;
	}

	return false;
}

static void free_vbuf(struct virtio_misc_device *vmd,
		      struct virtio_misc_vbuffer *vbuf)
{
	list_del(&vbuf->pending_list_entry);
	if (vbuf->src_buf)
		kfree(vbuf->src_buf);
	if (vbuf->dst_buf)
		kfree(vbuf->dst_buf);
	if (vbuf->pat_buf)
		kfree(vbuf->pat_buf);
	kmem_cache_free(vmd->vbufs, vbuf);
}

static int virtio_misc_queue_event_buffer(struct virtio_misc_device *vmd,
					   struct virtio_misc_event *evt)
{
	int ret;
	struct scatterlist sg;
	struct virtqueue *vq = vmd->eventq.vq;

	memset(evt, 0, sizeof(struct virtio_misc_event));
	sg_init_one(&sg, evt, sizeof(struct virtio_misc_event));

	ret = virtqueue_add_inbuf(vq, &sg, 1, evt, GFP_KERNEL);
	if (ret) {
		MISC_LOGE(vmd, "failed to queue event buffer\n");
		return ret;
	}

	virtqueue_kick(vq);

	return 0;
}

static void virtio_misc_handle_event(struct virtio_misc_device *vmd,
				     struct virtio_misc_event *evt)
{
	switch (le32_to_cpu(evt->event_type)) {
	case VIRTIO_MISC_EVENT_ERROR:
		MISC_LOGE(vmd, "error event\n");
		virtio_misc_handle_error(vmd);
		break;
	default:
		MISC_LOGW(vmd, "unknown event\n");
		break;
	}
}

static struct virtio_misc_vbuffer *
virtio_misc_get_vbuf(struct virtio_misc_device *vmd, int size, int resp_size,
		      void *resp_buf, virtio_misc_resp_cb resp_cb)
{
	struct virtio_misc_vbuffer *vbuf;

	vbuf = kmem_cache_alloc(vmd->vbufs, GFP_KERNEL);
	if (!vbuf)
		return ERR_PTR(-ENOMEM);
	memset(vbuf, 0, VBUFFER_SIZE);

	BUG_ON(size > MAX_INLINE_CMD_SIZE);
	vbuf->buf = (void *)vbuf + sizeof(*vbuf);
	vbuf->size = size;

	vbuf->resp_cb = resp_cb;
	vbuf->resp_size = resp_size;
	if (resp_size <= MAX_INLINE_RESP_SIZE && !resp_buf)
		vbuf->resp_buf = (void *)vbuf->buf + size;
	else
		vbuf->resp_buf = resp_buf;
	BUG_ON(!vbuf->resp_buf);

	return vbuf;
}

int virtio_misc_alloc_vbufs(struct virtio_misc_device *vmd)
{
	vmd->vbufs =
		kmem_cache_create("virtio-misc-vbufs", VBUFFER_SIZE,
				  __alignof__(struct virtio_misc_vbuffer), 0,
				  NULL);
	if (!vmd->vbufs)
		return -ENOMEM;

	return 0;
}

void virtio_misc_free_vbufs(struct virtio_misc_device *vmd)
{
	struct virtio_misc_vbuffer *vbuf;

	/* Release command buffers. Operation on vbufs here is lock safe,
           since before device was deinitialized and queues was stopped
           (in not ready state) */
	while ((vbuf = virtqueue_detach_unused_buf(vmd->commandq.vq))) {
		if (vbuf_is_pending(vmd, vbuf))
			free_vbuf(vmd, vbuf);
	}

	kmem_cache_destroy(vmd->vbufs);
	vmd->vbufs = NULL;

	/* Release event buffers */
	while (virtqueue_detach_unused_buf(vmd->eventq.vq));

	kfree(vmd->evts);
	vmd->evts = NULL;
}

static int
virtio_misc_queue_cmd_buffer(struct virtio_misc_device *vmd,
			     struct virtio_misc_vbuffer *vbuf)
{
	unsigned long flags;
	struct virtqueue *vq = vmd->commandq.vq;
	struct scatterlist *sgs[5], vreq, vsrc, vdst, vpat, vresp;
	int outcnt = 0, incnt = 0;
	int ret;

	if (!vmd->commandq.ready)
		return -ENODEV;

	spin_lock_irqsave(&vmd->commandq.qlock, flags);

	vbuf->id = vmd->vbufs_sent++;
	list_add_tail(&vbuf->pending_list_entry, &vmd->pending_vbuf_list);

	sg_init_one(&vreq, vbuf->buf, vbuf->size);
	sgs[outcnt + incnt] = &vreq;
	outcnt++;

	if (vbuf->src_size) {
		sg_init_one(&vsrc, vbuf->src_buf, vbuf->src_size);
		sgs[outcnt + incnt] = &vsrc;
		outcnt++;
	}

	if (vbuf->dst_size) {
		sg_init_one(&vdst, vbuf->dst_buf, vbuf->dst_size);
		sgs[outcnt + incnt] = &vdst;
		outcnt++;
	}

	if (vbuf->pat_size) {
		sg_init_one(&vpat, vbuf->pat_buf, vbuf->pat_size);
		sgs[outcnt + incnt] = &vpat;
		outcnt++;
	}

	if (vbuf->resp_size) {
		sg_init_one(&vresp, vbuf->resp_buf, vbuf->resp_size);
		sgs[outcnt + incnt] = &vresp;
		incnt++;
	}

retry:
	ret = virtqueue_add_sgs(vq, sgs, outcnt, incnt, vbuf, GFP_ATOMIC);
	if (ret == -ENOSPC) {
		spin_unlock_irqrestore(&vmd->commandq.qlock, flags);
		wait_event(vmd->commandq.reclaim_queue, vq->num_free);
		spin_lock_irqsave(&vmd->commandq.qlock, flags);
		goto retry;
	} else {
		virtqueue_kick(vq);
	}

	spin_unlock_irqrestore(&vmd->commandq.qlock, flags);

	return ret;
}

static int
virtio_misc_queue_cmd_buffer_sync(struct virtio_misc_device *vmd,
				   struct virtio_misc_vbuffer *vbuf)
{
	int ret;
	unsigned long rem;
	unsigned long flags;

	vbuf->is_sync = true;
	init_completion(&vbuf->reclaimed);

	ret = virtio_misc_queue_cmd_buffer(vmd, vbuf);
	if (ret)
		return ret;

	rem = wait_for_completion_timeout(&vbuf->reclaimed, 5 * HZ);
	if (rem == 0)
		ret = -ETIMEDOUT;

	spin_lock_irqsave(&vmd->commandq.qlock, flags);
	if (vbuf_is_pending(vmd, vbuf))
		free_vbuf(vmd, vbuf);
	spin_unlock_irqrestore(&vmd->commandq.qlock, flags);

	return ret;
}

static void *virtio_misc_alloc_req(struct virtio_misc_device *vmd,
				    struct virtio_misc_vbuffer **vbuffer_p,
				    int size)
{
	struct virtio_misc_vbuffer *vbuf;

	vbuf = virtio_misc_get_vbuf(vmd, size,
				     sizeof(struct virtio_misc_cmd_hdr),
				     NULL, NULL);
	if (IS_ERR(vbuf)) {
		*vbuffer_p = NULL;
		return ERR_CAST(vbuf);
	}
	*vbuffer_p = vbuf;

	return vbuf->buf;
}

static void *
virtio_misc_alloc_req_resp(struct virtio_misc_device *vmd,
			   virtio_misc_resp_cb cb,
			   struct virtio_misc_vbuffer **vbuffer_p,
			   int req_size, int resp_size,
			   void *resp_buf)
{
	struct virtio_misc_vbuffer *vbuf;

	vbuf = virtio_misc_get_vbuf(vmd, req_size, resp_size, resp_buf, cb);
	if (IS_ERR(vbuf)) {
		*vbuffer_p = NULL;
		return ERR_CAST(vbuf);
	}
	*vbuffer_p = vbuf;

	return vbuf->buf;
}

int virtio_misc_alloc_events(struct virtio_misc_device *vmd)
{
	int ret;
	size_t i;
	struct virtio_misc_event *evts;
	size_t num = vmd->eventq.vq->num_free;

	evts = kzalloc(num * sizeof(struct virtio_misc_event), GFP_KERNEL);
	if (!evts) {
		MISC_LOGE(vmd, "failed to alloc event buffers!!!\n");
		return -ENOMEM;
	}
	vmd->evts = evts;

	for (i = 0; i < num; i++) {
		ret = virtio_misc_queue_event_buffer(vmd, &evts[i]);
		if (ret) {
			MISC_LOGE(vmd, "failed to queue event buffer\n");
			return ret;
		}
	}

	return 0;
}

void virtio_misc_process_events(struct work_struct *work)
{
	struct virtio_misc_device *vmd = container_of(work,
			struct virtio_misc_device, eventq.work);
	struct virtqueue *vq = vmd->eventq.vq;
	struct virtio_misc_event *evt;
	unsigned int len;

	while (vmd->eventq.ready) {
		virtqueue_disable_cb(vq);

		while ((evt = virtqueue_get_buf(vq, &len))) {
			virtio_misc_handle_event(vmd, evt);
			virtio_misc_queue_event_buffer(vmd, evt);
		}

		if (unlikely(virtqueue_is_broken(vq)))
			break;

		if (virtqueue_enable_cb(vq))
			break;
	}
}

static void virtio_vop_print_args(struct virtio_misc_device *vmd,
				  struct virtio_vop_comp *req_p)
{
	int i;

	MISC_LOGD(vmd, "num_plane %d\r\n", req_p->plane.num_plane);
	for(i = 0; i < req_p->plane.num_plane; i++) {
		MISC_LOGD(vmd, "Planes[%d].plane_id: %d\r\n", i, req_p->plane.planes[i].plane_id);
		MISC_LOGD(vmd, "Planes[%d].layer_id: %d\r\n", i, req_p->plane.planes[i].layer_id);
		MISC_LOGD(vmd, "Planes[%d].source_crop: %d %d %d %d\r\n", i,
				req_p->plane.planes[i].source_crop.left,
				req_p->plane.planes[i].source_crop.top,
				req_p->plane.planes[i].source_crop.right,
				req_p->plane.planes[i].source_crop.bottom);
		MISC_LOGD(vmd, "Planes[%d].display_frame: %d %d %d %d\r\n", i,
				req_p->plane.planes[i].display_frame.left,
				req_p->plane.planes[i].display_frame.top,
				req_p->plane.planes[i].display_frame.right,
				req_p->plane.planes[i].display_frame.bottom);
		MISC_LOGD(vmd, "Planes[%d].rotation: %ld\r\n", i, req_p->plane.planes[i].rotation);
		MISC_LOGD(vmd, "Planes[%d].alpha: %ld\r\n", i, req_p->plane.planes[i].alpha);
		MISC_LOGD(vmd, "Planes[%d].blend: %ld\r\n", i, req_p->plane.planes[i].blend);
		MISC_LOGD(vmd, "Planes[%d].zorder: %d\r\n", i, req_p->plane.planes[i].zorder);
		MISC_LOGD(vmd, "Planes[%d].format: %d\r\n", i, req_p->plane.planes[i].format);
	}
}

static unsigned int build_virtio_misc_sglist(struct virtio_misc_sg_list *sgl,
			 struct sg_table *sgt)
{
	int i;
	struct scatterlist *sg;

	for_each_sg(sgt->sgl, sg, sgt->nents, i) {
		sgl->entries[i].addr = cpu_to_le64(sg_phys(sg));
		sgl->entries[i].length = cpu_to_le32(sg->length);
	}

	sgl->num_entries = sgt->nents;

	return VIRTIO_MISC_SG_SIZE(sgt->nents);
}

static void virtio_misc_mbuf_free(struct virtio_misc_map_buffer *mbuf)
{
	if (mbuf->attach && mbuf->sgt) {
		drm_gem_unmap_dma_buf(mbuf->attach, mbuf->sgt, mbuf->dir);
		mbuf->sgt = NULL;
	}
	if (mbuf->attach) {
		dma_buf_detach(mbuf->dma_buf, mbuf->attach);
		mbuf->attach = NULL;
	}
	if (mbuf->dma_buf) {
		dma_buf_put(mbuf->dma_buf);
		mbuf->dma_buf = NULL;
	}

	mbuf->dir = 0;
}

static int virtio_misc_mbuf_map(struct virtio_misc_device *vmd,
				int buf_fd,
				enum dma_data_direction dir,
				struct virtio_misc_map_buffer *mbuf)
{
	int ret;

	memset(mbuf, 0, sizeof(*mbuf));

	mbuf->dma_buf = dma_buf_get(buf_fd);
	if (IS_ERR(mbuf->dma_buf)) {
		MISC_LOGE(vmd, "dma_buf_get fail fd[%d]\n", buf_fd);
		mbuf->dma_buf = NULL;
		return -EINVAL;
	}

	mbuf->attach = dma_buf_attach(mbuf->dma_buf, vmd->dev);
	if (IS_ERR(mbuf->attach)) {
		MISC_LOGE(vmd, "Failed to attach dma_buf\n");
		ret = -EINVAL;
		goto err_get_attach;
	}

	mbuf->sgt = drm_gem_map_dma_buf(mbuf->attach, dir);
	if (IS_ERR(mbuf->sgt)) {
		MISC_LOGE(vmd, "Failed to map src attachment\n");
		ret = -EINVAL;
		goto err_get_sgt;
	}
	
	mbuf->dir = dir;
	return 0;

err_get_sgt:
	if (mbuf->attach) {
		dma_buf_detach(mbuf->dma_buf, mbuf->attach);
		mbuf->attach = NULL;
	}
err_get_attach:
	if (mbuf->dma_buf) {
		dma_buf_put(mbuf->dma_buf);
		mbuf->dma_buf = NULL;
	}
	return ret;
}

int virtio_vop_cmd_comp(struct virtio_misc_device *vmd,
			struct vvop_plane args)
{
	struct virtio_vop_comp *req_p;
	struct virtio_misc_vbuffer *vbuf;
	struct virtio_misc_sg_list *sg_list;
	struct virtio_misc_map_buffer mbufs[VIRTVOP_MAX_PLANE];
	void *src_buf = NULL;
	size_t src_size = 0;
	unsigned int offset = 0;
	int i, ret = 0;
	enum dma_data_direction dir = DMA_BIDIRECTIONAL;

	if (args.num_plane > VIRTVOP_MAX_PLANE) {
		MISC_LOGE(vmd, "num_plane(%d) should be less than or equal %d\n",
				args.num_plane, VIRTVOP_MAX_PLANE);
		return -EINVAL;
	};

	for (i = 0; i < args.num_plane; i++)
	{
		ret = virtio_misc_mbuf_map(vmd, args.planes[i].buf_fd, dir, &mbufs[i]);	
		if (ret < 0)
			goto err_mbuf_map;

		src_size += VIRTIO_MISC_SG_SIZE(mbufs[i].sgt->nents);
	}

	src_buf = kcalloc(1, src_size, GFP_KERNEL);
	if (!src_buf) {
		MISC_LOGE(vmd, "no memory for src_buf\n");
		ret = -ENOMEM;
		goto err_kcalloc;
	}

	req_p = virtio_misc_alloc_req(vmd, &vbuf, sizeof(*req_p));
	if (IS_ERR(req_p)) {
		ret = PTR_ERR(req_p);
		goto err_alloc_req;
	}

	req_p->hdr.type = cpu_to_le32(VIRTIO_VOP_CMD_COMP);
	memcpy(&req_p->plane, &args, sizeof(args));
	for (i = 0; i < args.num_plane; i++) {
		sg_list = src_buf + offset;
		offset += build_virtio_misc_sglist(sg_list, mbufs[i].sgt);
		virtio_misc_mbuf_free(&mbufs[i]);
	}

	virtio_vop_print_args(vmd, req_p);

	vbuf->src_buf = src_buf;
	vbuf->src_size = src_size;
	return virtio_misc_queue_cmd_buffer_sync(vmd, vbuf);

err_alloc_req:
	kfree(src_buf);
err_kcalloc:
err_mbuf_map:
	for (i = 0; i < args.num_plane; i++)
		virtio_misc_mbuf_free(&mbufs[i]);
	return ret;
}

int virtio_vop_cmd_create_layer(struct virtio_misc_device *vmd,
                        struct vvop_layer args)
{
	struct virtio_vop_layer *req_p;
	struct virtio_misc_vbuffer *vbuf;

	req_p = virtio_misc_alloc_req(vmd, &vbuf, sizeof(*req_p));
	if (IS_ERR(req_p))
		return PTR_ERR(req_p);

	req_p->hdr.type = cpu_to_le32(VIRTIO_VOP_CMD_CREATE_LAYER);
	memcpy(&req_p->layer, &args, sizeof(args));
	MISC_LOGD(vmd, "layer_id %u\n", req_p->layer.layer_id);

	return virtio_misc_queue_cmd_buffer_sync(vmd, vbuf);
}


int virtio_vop_cmd_destroy_layer(struct virtio_misc_device *vmd,
                        struct vvop_layer args)
{
	struct virtio_vop_layer *req_p;
	struct virtio_misc_vbuffer *vbuf;

	req_p = virtio_misc_alloc_req(vmd, &vbuf, sizeof(*req_p));
	if (IS_ERR(req_p))
		return PTR_ERR(req_p);

	req_p->hdr.type = cpu_to_le32(VIRTIO_VOP_CMD_DESTROY_LAYER);
	memcpy(&req_p->layer, &args, sizeof(args));
	MISC_LOGD(vmd, "layer_id %u\n", req_p->layer.layer_id);

	return virtio_misc_queue_cmd_buffer_sync(vmd, vbuf);
}

int virtio_rga_cmd_blit(struct virtio_misc_device *vmd,
			struct rga_req args,
			bool sync)
{
	int ret = 0;
	struct virtio_rga_req *req_p;
	struct virtio_misc_vbuffer *vbuf;
	struct virtio_misc_sg_list *sg_list;
	struct virtio_misc_map_buffer src_mbuf, dst_mbuf, pat_mbuf;
	void *src_buf = NULL, *dst_buf = NULL, *pat_buf = NULL;
	size_t src_size = 0, dst_size = 0, pat_size = 0;
	enum dma_data_direction dir = DMA_BIDIRECTIONAL;
	bool src_valid = false, dst_valid = false, pat_valid = false;

	if (!sync) {
		MISC_LOGE(vmd, "blit_async is unsupported now!\n");
		return -EIO;
	}
	if ((int64_t)args.src.yrgb_addr > 0)
		src_valid = true;
	if ((int64_t)args.dst.yrgb_addr > 0)
		dst_valid = true;
	if ((int64_t)args.pat.yrgb_addr > 0)
		pat_valid = true;

	memset(&src_mbuf, 0, sizeof(src_mbuf));
	memset(&dst_mbuf, 0, sizeof(dst_mbuf));
	memset(&pat_mbuf, 0, sizeof(pat_mbuf));

	if (src_valid) {
		ret = virtio_misc_mbuf_map(vmd, args.src.yrgb_addr, dir, &src_mbuf);
		if (ret < 0)
			goto exit;

		src_size = VIRTIO_MISC_SG_SIZE(src_mbuf.sgt->nents);
		src_buf = kcalloc(1, src_size, GFP_KERNEL);
		if (!src_buf) {
			MISC_LOGE(vmd, "no memory for src_buf\n");
			virtio_misc_mbuf_free(&src_mbuf);
			ret = -ENOMEM;
			goto exit;
		}
		sg_list = src_buf;
		build_virtio_misc_sglist(sg_list, src_mbuf.sgt);
		virtio_misc_mbuf_free(&src_mbuf);
	}

	if (dst_valid) {
		ret = virtio_misc_mbuf_map(vmd, args.dst.yrgb_addr, dir, &dst_mbuf);
		if (ret < 0)
			goto exit;

		dst_size = VIRTIO_MISC_SG_SIZE(dst_mbuf.sgt->nents);
		dst_buf = kcalloc(1, dst_size, GFP_KERNEL);
		if (!dst_buf) {
			MISC_LOGE(vmd, "no memory for dst_buf\n");
			virtio_misc_mbuf_free(&dst_mbuf);
			ret = -ENOMEM;
			goto exit;
		}
		sg_list = dst_buf;
		build_virtio_misc_sglist(sg_list, dst_mbuf.sgt);
		virtio_misc_mbuf_free(&dst_mbuf);
	}

	if (pat_valid) {
		ret = virtio_misc_mbuf_map(vmd, args.pat.yrgb_addr, dir, &pat_mbuf);
		if (ret < 0)
			goto exit;

		pat_size = VIRTIO_MISC_SG_SIZE(pat_mbuf.sgt->nents);
		pat_buf = kcalloc(1, pat_size, GFP_KERNEL);
		if (!pat_buf) {
			MISC_LOGE(vmd, "no memory for pat_buf\n");
			virtio_misc_mbuf_free(&pat_mbuf);
			ret = -ENOMEM;
			goto exit;
		}
		sg_list = pat_buf;
		build_virtio_misc_sglist(sg_list, pat_mbuf.sgt);
		virtio_misc_mbuf_free(&pat_mbuf);
	}

	req_p = virtio_misc_alloc_req(vmd, &vbuf, sizeof(*req_p));
	if (IS_ERR(req_p)) {
		ret = PTR_ERR(req_p);
		goto exit;
	}
	req_p->hdr.type = cpu_to_le32(VIRTIO_RGA_CMD_BLIT);
	memcpy(&req_p->req, &args, sizeof(args));

	vbuf->src_buf = src_buf;
	vbuf->src_size = src_size;
	vbuf->dst_buf = dst_buf;
	vbuf->dst_size = dst_size;
	vbuf->pat_buf = pat_buf;
	vbuf->pat_size = pat_size;
	return virtio_misc_queue_cmd_buffer_sync(vmd, vbuf);

exit:
	if (pat_buf)
		kfree(pat_buf);
	if (dst_buf)
		kfree(dst_buf);
	if (src_buf)
		kfree(src_buf);
	return ret;
}

int virtio_rga_cmd_get_driver_version(struct virtio_misc_device *vmd,
				      struct virtio_rga_driver_version_resp *resp)
{
	int ret = 0;

	struct virtio_rga_driver_version *req_p;
	struct virtio_misc_vbuffer *vbuf;

	req_p = virtio_misc_alloc_req_resp(vmd, NULL, &vbuf, sizeof(*req_p),
					   sizeof(*resp), resp);
	if (IS_ERR(req_p))
		return PTR_ERR(req_p);

	req_p->hdr.type = cpu_to_le32(VIRTIO_RGA_CMD_GET_DRIVER_VERSION);
	ret = virtio_misc_queue_cmd_buffer_sync(vmd, vbuf);
	if (ret == -ETIMEDOUT)
		MISC_LOGE(vmd, "timed out waiting for get driver version\n");

	return ret;
}

int virtio_rga_cmd_get_hw_version(struct virtio_misc_device *vmd,
	  			  struct virtio_rga_hw_version_resp *resp)
{
	int ret = 0;
	struct virtio_rga_driver_version *req_p;
	struct virtio_misc_vbuffer *vbuf;

	req_p = virtio_misc_alloc_req_resp(vmd, NULL, &vbuf, sizeof(*req_p),
					   sizeof(*resp), resp);
	if (IS_ERR(req_p))
		return PTR_ERR(req_p);

	req_p->hdr.type = cpu_to_le32(VIRTIO_RGA_CMD_GET_HW_VERSION);
	ret = virtio_misc_queue_cmd_buffer_sync(vmd, vbuf);
	if (ret == -ETIMEDOUT)
		MISC_LOGE(vmd, "timed out waiting for get hwr version\n");

	return ret;
}

void virtio_misc_cmd_cb(struct virtqueue *vq)
{
	struct virtio_misc_device *vmd = vq->vdev->priv;
	struct virtio_misc_vbuffer *vbuf;
	unsigned long flags;
	unsigned int len;

	spin_lock_irqsave(&vmd->commandq.qlock, flags);
	while (vmd->commandq.ready) {
		virtqueue_disable_cb(vq);

		while ((vbuf = virtqueue_get_buf(vq, &len))) {
			if (!vbuf_is_pending(vmd, vbuf))
				continue;

			if (vbuf->resp_cb)
				vbuf->resp_cb(vmd, vbuf);

			if (vbuf->is_sync)
				complete(&vbuf->reclaimed);
			else
				free_vbuf(vmd, vbuf);
		}

		if (unlikely(virtqueue_is_broken(vq)))
			break;

		if (virtqueue_enable_cb(vq))
			break;
	}
	spin_unlock_irqrestore(&vmd->commandq.qlock, flags);

	wake_up(&vmd->commandq.reclaim_queue);
}

void virtio_misc_event_cb(struct virtqueue *vq)
{
        struct virtio_misc_device *vmd = vq->vdev->priv;

        schedule_work(&vmd->eventq.work);
}
