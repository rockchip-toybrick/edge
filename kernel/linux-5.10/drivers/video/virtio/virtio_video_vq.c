// SPDX-License-Identifier: GPL-2.0+
/* Driver for virtio video device.
 *
 * Copyright 2020 OpenSynergy GmbH.
 *
 * Based on drivers/gpu/drm/virtio/virtgpu_vq.c
 * Copyright (C) 2015 Red Hat, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "virtio_video.h"

#define MAX_INLINE_CMD_SIZE   298
#define MAX_INLINE_RESP_SIZE  298
#define VBUFFER_SIZE          (sizeof(struct virtio_video_vbuffer) \
			       + MAX_INLINE_CMD_SIZE		   \
			       + MAX_INLINE_RESP_SIZE)

static int virtio_video_queue_event_buffer(struct virtio_video_device *vvd,
					   struct virtio_video_event *evt);
static void virtio_video_handle_event(struct virtio_video_device *vvd,
				      struct virtio_video_event *evt);

void virtio_video_resource_id_get(struct virtio_video_device *vvd, uint32_t *id)
{
	int handle;

	idr_preload(GFP_KERNEL);
	spin_lock(&vvd->resource_idr_lock);
	handle = idr_alloc(&vvd->resource_idr, NULL, 1, 0, GFP_NOWAIT);
	spin_unlock(&vvd->resource_idr_lock);
	idr_preload_end();
	*id = handle;
}

void virtio_video_resource_id_put(struct virtio_video_device *vvd, uint32_t id)
{
	spin_lock(&vvd->resource_idr_lock);
	idr_remove(&vvd->resource_idr, id);
	spin_unlock(&vvd->resource_idr_lock);
}

void virtio_video_stream_id_get(struct virtio_video_device *vvd,
				struct virtio_video_stream *stream,
				uint32_t *id)
{
	int handle;

	idr_preload(GFP_KERNEL);
	spin_lock(&vvd->stream_idr_lock);
	handle = idr_alloc(&vvd->stream_idr, stream, 1, 0, 0);
	spin_unlock(&vvd->stream_idr_lock);
	idr_preload_end();
	*id = handle;
}

void virtio_video_stream_id_put(struct virtio_video_device *vvd, uint32_t id)
{
	spin_lock(&vvd->stream_idr_lock);
	idr_remove(&vvd->stream_idr, id);
	spin_unlock(&vvd->stream_idr_lock);
}

static bool vbuf_is_pending(struct virtio_video_device *vvd,
			    struct virtio_video_vbuffer *vbuf)
{
	struct virtio_video_vbuffer *entry;

	list_for_each_entry(entry, &vvd->pending_vbuf_list, pending_list_entry)
	{
		if (entry == vbuf && entry->id == vbuf->id)
			return true;
	}

	return false;
}

static void free_vbuf(struct virtio_video_device *vvd,
		      struct virtio_video_vbuffer *vbuf)
{
	list_del(&vbuf->pending_list_entry);
	kfree(vbuf->data_buf);
	kmem_cache_free(vvd->vbufs, vbuf);
}

void virtio_video_cmd_cb(struct virtqueue *vq)
{
	struct virtio_video_device *vvd = vq->vdev->priv;
	struct virtio_video_vbuffer *vbuf;
	unsigned long flags;
	unsigned int len;

	spin_lock_irqsave(&vvd->commandq.qlock, flags);
	while (vvd->commandq.ready) {
		virtqueue_disable_cb(vq);

		while ((vbuf = virtqueue_get_buf(vq, &len))) {
			if (!vbuf_is_pending(vvd, vbuf))
				continue;

			if (vbuf->resp_cb)
				vbuf->resp_cb(vvd, vbuf);

			if (vbuf->is_sync)
				complete(&vbuf->reclaimed);
			else
				free_vbuf(vvd, vbuf);
		}

		if (unlikely(virtqueue_is_broken(vq)))
			break;

		if (virtqueue_enable_cb(vq))
			break;
	}
	spin_unlock_irqrestore(&vvd->commandq.qlock, flags);

	wake_up(&vvd->commandq.reclaim_queue);
}

void virtio_video_process_events(struct work_struct *work)
{
	struct virtio_video_device *vvd = container_of(work,
			struct virtio_video_device, eventq.work);
	struct virtqueue *vq = vvd->eventq.vq;
	struct virtio_video_event *evt;
	unsigned int len;

	while (vvd->eventq.ready) {
		virtqueue_disable_cb(vq);

		while ((evt = virtqueue_get_buf(vq, &len))) {
			virtio_video_handle_event(vvd, evt);
			virtio_video_queue_event_buffer(vvd, evt);
		}

		if (unlikely(virtqueue_is_broken(vq)))
			break;

		if (virtqueue_enable_cb(vq))
			break;
	}
}

void virtio_video_event_cb(struct virtqueue *vq)
{
	struct virtio_video_device *vvd = vq->vdev->priv;

	schedule_work(&vvd->eventq.work);
}

static struct virtio_video_vbuffer *
virtio_video_get_vbuf(struct virtio_video_device *vvd, int size, int resp_size,
		      void *resp_buf, virtio_video_resp_cb resp_cb)
{
	struct virtio_video_vbuffer *vbuf;

	vbuf = kmem_cache_alloc(vvd->vbufs, GFP_KERNEL);
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

int virtio_video_alloc_vbufs(struct virtio_video_device *vvd)
{
	vvd->vbufs =
		kmem_cache_create("virtio-video-vbufs", VBUFFER_SIZE,
				  __alignof__(struct virtio_video_vbuffer), 0,
				  NULL);
	if (!vvd->vbufs)
		return -ENOMEM;

	return 0;
}

void virtio_video_free_vbufs(struct virtio_video_device *vvd)
{
	struct virtio_video_vbuffer *vbuf;

	/* Release command buffers. Operation on vbufs here is lock safe,
           since before device was deinitialized and queues was stopped
           (in not ready state) */
	while ((vbuf = virtqueue_detach_unused_buf(vvd->commandq.vq))) {
		if (vbuf_is_pending(vvd, vbuf))
			free_vbuf(vvd, vbuf);
	}

	kmem_cache_destroy(vvd->vbufs);
	vvd->vbufs = NULL;

	/* Release event buffers */
	while (virtqueue_detach_unused_buf(vvd->eventq.vq));

	kfree(vvd->evts);
	vvd->evts = NULL;
}

static void *virtio_video_alloc_req(struct virtio_video_device *vvd,
				    struct virtio_video_vbuffer **vbuffer_p,
				    int size)
{
	struct virtio_video_vbuffer *vbuf;

	vbuf = virtio_video_get_vbuf(vvd, size,
				     sizeof(struct virtio_video_cmd_hdr),
				     NULL, NULL);
	if (IS_ERR(vbuf)) {
		*vbuffer_p = NULL;
		return ERR_CAST(vbuf);
	}
	*vbuffer_p = vbuf;

	return vbuf->buf;
}

static void *
virtio_video_alloc_req_resp(struct virtio_video_device *vvd,
			    virtio_video_resp_cb cb,
			    struct virtio_video_vbuffer **vbuffer_p,
			    int req_size, int resp_size,
			    void *resp_buf)
{
	struct virtio_video_vbuffer *vbuf;

	vbuf = virtio_video_get_vbuf(vvd, req_size, resp_size, resp_buf, cb);
	if (IS_ERR(vbuf)) {
		*vbuffer_p = NULL;
		return ERR_CAST(vbuf);
	}
	*vbuffer_p = vbuf;

	return vbuf->buf;
}

static int
virtio_video_queue_cmd_buffer(struct virtio_video_device *vvd,
			      struct virtio_video_vbuffer *vbuf)
{
	unsigned long flags;
	struct virtqueue *vq = vvd->commandq.vq;
	struct scatterlist *sgs[3], vreq, vout, vresp;
	int outcnt = 0, incnt = 0;
	int ret;

	if (!vvd->commandq.ready)
		return -ENODEV;

	spin_lock_irqsave(&vvd->commandq.qlock, flags);

	vbuf->id = vvd->vbufs_sent++;
	list_add_tail(&vbuf->pending_list_entry, &vvd->pending_vbuf_list);

	sg_init_one(&vreq, vbuf->buf, vbuf->size);
	sgs[outcnt + incnt] = &vreq;
	outcnt++;

	if (vbuf->data_size) {
		sg_init_one(&vout, vbuf->data_buf, vbuf->data_size);
		sgs[outcnt + incnt] = &vout;
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
		spin_unlock_irqrestore(&vvd->commandq.qlock, flags);
		wait_event(vvd->commandq.reclaim_queue, vq->num_free);
		spin_lock_irqsave(&vvd->commandq.qlock, flags);
		goto retry;
	} else {
		virtqueue_kick(vq);
	}

	spin_unlock_irqrestore(&vvd->commandq.qlock, flags);

	return ret;
}

static int
virtio_video_queue_cmd_buffer_sync(struct virtio_video_device *vvd,
				   struct virtio_video_vbuffer *vbuf)
{
	int ret;
	unsigned long rem;
	unsigned long flags;

	vbuf->is_sync = true;
	init_completion(&vbuf->reclaimed);

	ret = virtio_video_queue_cmd_buffer(vvd, vbuf);
	if (ret)
		return ret;

	rem = wait_for_completion_timeout(&vbuf->reclaimed, 5 * HZ);
	if (rem == 0)
		ret = -ETIMEDOUT;

	spin_lock_irqsave(&vvd->commandq.qlock, flags);
	if (vbuf_is_pending(vvd, vbuf))
		free_vbuf(vvd, vbuf);
	spin_unlock_irqrestore(&vvd->commandq.qlock, flags);

	return ret;
}

static int virtio_video_queue_event_buffer(struct virtio_video_device *vvd,
					   struct virtio_video_event *evt)
{
	int ret;
	struct scatterlist sg;
	struct virtqueue *vq = vvd->eventq.vq;

	memset(evt, 0, sizeof(struct virtio_video_event));
	sg_init_one(&sg, evt, sizeof(struct virtio_video_event));

	ret = virtqueue_add_inbuf(vq, &sg, 1, evt, GFP_KERNEL);
	if (ret) {
		v4l2_err(&vvd->v4l2_dev, "failed to queue event buffer\n");
		return ret;
	}

	virtqueue_kick(vq);

	return 0;
}

static void virtio_video_handle_event(struct virtio_video_device *vvd,
				      struct virtio_video_event *evt)
{
	struct virtio_video_stream *stream;
	uint32_t stream_id = evt->stream_id;
	struct video_device *vd = &vvd->video_dev;

	mutex_lock(vd->lock);

	stream = idr_find(&vvd->stream_idr, stream_id);
	if (!stream) {
		v4l2_warn(&vvd->v4l2_dev, "stream_id=%u not found for event\n",
			  stream_id);
		mutex_unlock(vd->lock);
		return;
	}

	switch (le32_to_cpu(evt->event_type)) {
	case VIRTIO_VIDEO_EVENT_DECODER_RESOLUTION_CHANGED:
		v4l2_dbg(1, vvd->debug, &vvd->v4l2_dev,
			 "stream_id=%u: resolution change event\n", stream_id);
		virtio_video_cmd_get_params(vvd, stream,
					   VIRTIO_VIDEO_QUEUE_TYPE_OUTPUT);
		virtio_video_queue_res_chg_event(stream);
		if (virtio_video_state(stream) == STREAM_STATE_INIT) {
			virtio_video_state_update(stream,
						  STREAM_STATE_DYNAMIC_RES_CHANGE);
			wake_up(&vvd->wq);
		}
		break;
	case VIRTIO_VIDEO_EVENT_ERROR:
		v4l2_err(&vvd->v4l2_dev, "stream_id=%i: error event\n",
			 stream_id);
		virtio_video_state_update(stream, STREAM_STATE_ERROR);
		virtio_video_handle_error(stream);
		break;
	default:
		v4l2_warn(&vvd->v4l2_dev, "stream_id=%i: unknown event\n",
			  stream_id);
		break;
	}

	mutex_unlock(vd->lock);
}

int virtio_video_alloc_events(struct virtio_video_device *vvd)
{
	int ret;
	size_t i;
	struct virtio_video_event *evts;
	size_t num =  vvd->eventq.vq->num_free;

	evts = kzalloc(num * sizeof(struct virtio_video_event), GFP_KERNEL);
	if (!evts) {
		v4l2_err(&vvd->v4l2_dev, "failed to alloc event buffers!!!\n");
		return -ENOMEM;
	}
	vvd->evts = evts;

	for (i = 0; i < num; i++) {
		ret = virtio_video_queue_event_buffer(vvd, &evts[i]);
		if (ret) {
			v4l2_err(&vvd->v4l2_dev,
				 "failed to queue event buffer\n");
			return ret;
		}
	}

	return 0;
}

// TODO: replace virtio_video_cmd_hdr accoring to specification v4
int virtio_video_cmd_stream_create(struct virtio_video_device *vvd,
				   uint32_t stream_id,
				   enum virtio_video_format format,
				   const char *tag)
{
	struct virtio_video_stream_create *req_p;
	struct virtio_video_vbuffer *vbuf;

	req_p = virtio_video_alloc_req(vvd, &vbuf, sizeof(*req_p));
	if (IS_ERR(req_p))
		return PTR_ERR(req_p);

	req_p->hdr.type = cpu_to_le32(VIRTIO_VIDEO_CMD_STREAM_CREATE);
	req_p->hdr.stream_id = cpu_to_le32(stream_id);
	req_p->in_mem_type = cpu_to_le32(VIRTIO_VIDEO_MEM_TYPE_GUEST_PAGES);
	req_p->out_mem_type = cpu_to_le32(VIRTIO_VIDEO_MEM_TYPE_GUEST_PAGES);
	req_p->coded_format = cpu_to_le32(format);
	if (strscpy(req_p->tag, tag, sizeof(req_p->tag) - 1) < 0)
		v4l2_err(&vvd->v4l2_dev, "failed to copy stream tag\n");
	req_p->tag[sizeof(req_p->tag) - 1] = 0;

	return virtio_video_queue_cmd_buffer(vvd, vbuf);
}

// TODO: replace virtio_video_cmd_hdr accoring to specification v4
int virtio_video_cmd_stream_destroy(struct virtio_video_device *vvd,
				    uint32_t stream_id)
{
	struct virtio_video_stream_destroy *req_p;
	struct virtio_video_vbuffer *vbuf;

	req_p = virtio_video_alloc_req(vvd, &vbuf, sizeof(*req_p));
	if (IS_ERR(req_p))
		return PTR_ERR(req_p);

	req_p->hdr.type = cpu_to_le32(VIRTIO_VIDEO_CMD_STREAM_DESTROY);
	req_p->hdr.stream_id = cpu_to_le32(stream_id);

	return virtio_video_queue_cmd_buffer(vvd, vbuf);
}

// TODO: replace virtio_video_cmd_hdr accoring to specification v4
int virtio_video_cmd_stream_drain(struct virtio_video_device *vvd,
				  uint32_t stream_id)
{
	struct virtio_video_stream_drain *req_p;
	struct virtio_video_vbuffer *vbuf;

	req_p = virtio_video_alloc_req(vvd, &vbuf, sizeof(*req_p));
	if (IS_ERR(req_p))
		return PTR_ERR(req_p);

	req_p->hdr.type = cpu_to_le32(VIRTIO_VIDEO_CMD_STREAM_DRAIN);
	req_p->hdr.stream_id = cpu_to_le32(stream_id);

	return virtio_video_queue_cmd_buffer(vvd, vbuf);
}

int virtio_video_cmd_resource_attach(struct virtio_video_device *vvd,
				     uint32_t stream_id, uint32_t resource_id,
				     enum virtio_video_queue_type queue_type,
				     void *buf, size_t buf_size)
{
	struct virtio_video_resource_attach *req_p;
	struct virtio_video_vbuffer *vbuf;

	req_p = virtio_video_alloc_req(vvd, &vbuf, sizeof(*req_p));
	if (IS_ERR(req_p))
		return PTR_ERR(req_p);

	req_p->cmd_type = cpu_to_le32(VIRTIO_VIDEO_CMD_RESOURCE_ATTACH);
	req_p->stream_id = cpu_to_le32(stream_id);
	req_p->queue_type = cpu_to_le32(queue_type);
	req_p->resource_id = cpu_to_le32(resource_id);

	vbuf->data_buf = buf;
	vbuf->data_size = buf_size;

	return virtio_video_queue_cmd_buffer(vvd, vbuf);
}

int virtio_video_cmd_queue_detach_resources(struct virtio_video_device *vvd,
				struct virtio_video_stream *stream,
				enum virtio_video_queue_type queue_type)
{
	int ret;
	struct virtio_video_queue_detach_resources *req_p;
	struct virtio_video_vbuffer *vbuf;

	req_p = virtio_video_alloc_req(vvd, &vbuf, sizeof(*req_p));
	if (IS_ERR(req_p))
		return PTR_ERR(req_p);

	req_p->cmd_type = cpu_to_le32(VIRTIO_VIDEO_CMD_QUEUE_DETACH_RESOURCES);
	req_p->stream_id = cpu_to_le32(stream->stream_id);
	req_p->queue_type = cpu_to_le32(queue_type);

	ret = virtio_video_queue_cmd_buffer_sync(vvd, vbuf);
	if (ret == -ETIMEDOUT)
		v4l2_err(&vvd->v4l2_dev,
			 "timed out waiting for resource destruction for %s\n",
			 (queue_type == VIRTIO_VIDEO_QUEUE_TYPE_INPUT) ?
			 "OUTPUT" : "CAPTURE");
	return ret;
}

static void
virtio_video_cmd_resource_queue_cb(struct virtio_video_device *vvd,
				   struct virtio_video_vbuffer *vbuf)
{
	uint32_t flags;
	uint64_t timestamp;
	struct virtio_video_buffer *virtio_vb = vbuf->priv;
	struct virtio_video_resource_queue_resp *resp =
		(struct virtio_video_resource_queue_resp *)vbuf->resp_buf;

	flags = le32_to_cpu(resp->flags);
	timestamp = le64_to_cpu(resp->timestamp);

	virtio_video_buf_done(virtio_vb, flags, timestamp, resp->data_sizes);
}

int virtio_video_cmd_resource_queue(struct virtio_video_device *vvd,
				    uint32_t stream_id,
				    struct virtio_video_buffer *virtio_vb,
				    uint32_t data_size[],
				    uint8_t num_data_size,
				    enum virtio_video_queue_type queue_type)
{
	uint8_t i;
	struct virtio_video_resource_queue *req_p;
	struct virtio_video_resource_queue_resp *resp_p;
	struct virtio_video_vbuffer *vbuf;
	size_t resp_size = sizeof(struct virtio_video_resource_queue_resp);

	req_p = virtio_video_alloc_req_resp(vvd,
					    &virtio_video_cmd_resource_queue_cb,
					    &vbuf, sizeof(*req_p), resp_size,
					    NULL);
	if (IS_ERR(req_p))
		return PTR_ERR(req_p);

	req_p->cmd_type = cpu_to_le32(VIRTIO_VIDEO_CMD_RESOURCE_QUEUE);
	req_p->stream_id = cpu_to_le32(stream_id);
	req_p->queue_type = cpu_to_le32(queue_type);
	req_p->resource_id = cpu_to_le32(virtio_vb->resource_id);
	req_p->flags = 0;
	req_p->timestamp =
		cpu_to_le64(virtio_vb->v4l2_m2m_vb.vb.vb2_buf.timestamp);

	for (i = 0; i < num_data_size; ++i)
		req_p->data_sizes[i] = cpu_to_le32(data_size[i]);

	resp_p = (struct virtio_video_resource_queue_resp *)vbuf->resp_buf;
	memset(resp_p, 0, sizeof(*resp_p));

	vbuf->priv = virtio_vb;

	return virtio_video_queue_cmd_buffer(vvd, vbuf);
}

// TODO: replace virtio_video_cmd_hdr accoring to specification v4
int virtio_video_cmd_queue_clear(struct virtio_video_device *vvd,
				 struct virtio_video_stream *stream,
				 enum virtio_video_queue_type queue_type)
{
	int ret;
	struct virtio_video_queue_clear *req_p;
	struct virtio_video_vbuffer *vbuf;

	req_p = virtio_video_alloc_req(vvd, &vbuf, sizeof(*req_p));
	if (IS_ERR(req_p))
		return PTR_ERR(req_p);

	req_p->hdr.type = cpu_to_le32(VIRTIO_VIDEO_CMD_QUEUE_CLEAR);
	req_p->hdr.stream_id = cpu_to_le32(stream->stream_id);
	req_p->queue_type = cpu_to_le32(queue_type);

	ret = virtio_video_queue_cmd_buffer_sync(vvd, vbuf);
	if (ret == -ETIMEDOUT)
		v4l2_err(&vvd->v4l2_dev,
			 "timed out waiting for %s queue clear\n",
			 (queue_type == VIRTIO_VIDEO_QUEUE_TYPE_INPUT) ?
			 "OUTPUT" : "CAPTURE");
	return ret;
}

// TODO: replace virtio_video_cmd_hdr accoring to specification v4
int virtio_video_cmd_query_capability(struct virtio_video_device *vvd,
				      void *resp_buf, size_t resp_size,
				      enum virtio_video_queue_type queue_type)
{
	int ret;
	struct virtio_video_query_capability *req_p;
	struct virtio_video_vbuffer *vbuf;

	req_p = virtio_video_alloc_req_resp(vvd, NULL, &vbuf, sizeof(*req_p),
					    resp_size, resp_buf);
	if (IS_ERR(req_p))
		return PTR_ERR(req_p);

	req_p->hdr.type = cpu_to_le32(VIRTIO_VIDEO_CMD_QUERY_CAPABILITY);
	req_p->queue_type = cpu_to_le32(queue_type);

	ret = virtio_video_queue_cmd_buffer_sync(vvd, vbuf);
	if (ret == -ETIMEDOUT)
		v4l2_err(&vvd->v4l2_dev,
			 "timed out waiting for capabilities for %s\n",
			 (queue_type == VIRTIO_VIDEO_QUEUE_TYPE_INPUT) ?
			 "OUTPUT" : "CAPTURE");
	return ret;
}

// TODO: replace virtio_video_cmd_hdr accoring to specification v4
int virtio_video_query_control_level(struct virtio_video_device *vvd,
				     void *resp_buf, size_t resp_size,
				     enum virtio_video_format format)
{
	int ret;
	struct virtio_video_query_control *req_p;
	struct virtio_video_query_control_level *ctrl_l;
	struct virtio_video_vbuffer *vbuf;
	uint32_t req_size = 0;

	req_size = sizeof(struct virtio_video_query_control) +
		sizeof(struct virtio_video_query_control_level);

	req_p = virtio_video_alloc_req_resp(vvd, NULL, &vbuf, req_size,
					    resp_size, resp_buf);
	if (IS_ERR(req_p))
		return PTR_ERR(req_p);

	req_p->hdr.type = cpu_to_le32(VIRTIO_VIDEO_CMD_QUERY_CONTROL);
	req_p->control = cpu_to_le32(VIRTIO_VIDEO_CONTROL_LEVEL);
	ctrl_l = (void *)((char *)req_p +
			  sizeof(struct virtio_video_query_control));
	ctrl_l->format = cpu_to_le32(format);

	ret = virtio_video_queue_cmd_buffer_sync(vvd, vbuf);
	if (ret == -ETIMEDOUT)
		v4l2_err(&vvd->v4l2_dev,
			 "timed out waiting for level query\n");
	return ret;
}

// TODO: replace virtio_video_cmd_hdr accoring to specification v4
int virtio_video_query_control_profile(struct virtio_video_device *vvd,
				       void *resp_buf, size_t resp_size,
				       enum virtio_video_format format)
{
	int ret;
	struct virtio_video_query_control *req_p;
	struct virtio_video_query_control_profile *ctrl_p;
	struct virtio_video_vbuffer *vbuf;
	uint32_t req_size = 0;

	req_size = sizeof(struct virtio_video_query_control) +
		sizeof(struct virtio_video_query_control_profile);

	req_p = virtio_video_alloc_req_resp(vvd, NULL, &vbuf, req_size,
					    resp_size, resp_buf);
	if (IS_ERR(req_p))
		return PTR_ERR(req_p);

	req_p->hdr.type = cpu_to_le32(VIRTIO_VIDEO_CMD_QUERY_CONTROL);
	req_p->control = cpu_to_le32(VIRTIO_VIDEO_CONTROL_PROFILE);
	ctrl_p = (void *)((char *)req_p +
			  sizeof(struct virtio_video_query_control));
	ctrl_p->format = cpu_to_le32(format);

	ret = virtio_video_queue_cmd_buffer_sync(vvd, vbuf);
	if (ret == -ETIMEDOUT)
		v4l2_err(&vvd->v4l2_dev,
			 "timed out waiting for profile query\n");
	return ret;
}

static void
virtio_video_cmd_get_params_cb(struct virtio_video_device *vvd,
			       struct virtio_video_vbuffer *vbuf)
{
	int i;
	struct virtio_video_get_params_resp *resp =
		(struct virtio_video_get_params_resp *)vbuf->resp_buf;
	struct virtio_video_params *params = &resp->params;
	struct virtio_video_stream *stream = vbuf->priv;
	enum virtio_video_queue_type queue_type;
	struct video_format_info *format_info;

	queue_type = le32_to_cpu(params->queue_type);
	if (queue_type == VIRTIO_VIDEO_QUEUE_TYPE_INPUT)
		format_info = &stream->in_info;
	else
		format_info = &stream->out_info;

	format_info->frame_rate = le32_to_cpu(params->frame_rate);
	format_info->frame_width = le32_to_cpu(params->frame_width);
	format_info->frame_height = le32_to_cpu(params->frame_height);
	format_info->min_buffers = le32_to_cpu(params->min_buffers);
	format_info->max_buffers = le32_to_cpu(params->max_buffers);
	format_info->fourcc_format =
		virtio_video_format_to_v4l2(le32_to_cpu(params->format));

	format_info->crop.top = le32_to_cpu(params->crop.top);
	format_info->crop.left = le32_to_cpu(params->crop.left);
	format_info->crop.width = le32_to_cpu(params->crop.width);
	format_info->crop.height = le32_to_cpu(params->crop.height);

	format_info->num_planes = le32_to_cpu(params->num_planes);
	for (i = 0; i < le32_to_cpu(params->num_planes); i++) {
		struct virtio_video_plane_format *plane_formats =
						 &params->plane_formats[i];
		struct video_plane_format *plane_format =
						 &format_info->plane_format[i];

		plane_format->plane_size =
				 le32_to_cpu(plane_formats->plane_size);
		plane_format->stride = le32_to_cpu(plane_formats->stride);
	}
}

// TODO: replace virtio_video_cmd_hdr accoring to specification v4
int virtio_video_cmd_get_params(struct virtio_video_device *vvd,
				struct virtio_video_stream *stream,
				enum virtio_video_queue_type queue_type)
{
	int ret;
	struct virtio_video_get_params *req_p;
	struct virtio_video_vbuffer *vbuf;
	struct virtio_video_get_params_resp *resp_p;
	size_t resp_size = sizeof(struct virtio_video_get_params_resp);

	req_p = virtio_video_alloc_req_resp(vvd,
					&virtio_video_cmd_get_params_cb,
					&vbuf, sizeof(*req_p), resp_size,
					NULL);
	if (IS_ERR(req_p))
		return PTR_ERR(req_p);

	req_p->hdr.type = cpu_to_le32(VIRTIO_VIDEO_CMD_GET_PARAMS);
	req_p->hdr.stream_id = cpu_to_le32(stream->stream_id);
	req_p->queue_type = cpu_to_le32(queue_type);

	resp_p = (struct virtio_video_get_params_resp *)vbuf->resp_buf;

	vbuf->priv = stream;

	ret = virtio_video_queue_cmd_buffer_sync(vvd, vbuf);
	if (ret == -ETIMEDOUT)
		v4l2_err(&vvd->v4l2_dev,
			 "timed out waiting for get_params\n");
	return ret;
}

// TODO: replace virtio_video_cmd_hdr accoring to specification v4
int
virtio_video_cmd_set_params(struct virtio_video_device *vvd,
			    struct virtio_video_stream *stream,
			    struct video_format_info *format_info,
			    enum virtio_video_queue_type queue_type)
{
	int i;
	struct virtio_video_set_params *req_p;
	struct virtio_video_vbuffer *vbuf;

	req_p = virtio_video_alloc_req(vvd, &vbuf, sizeof(*req_p));
	if (IS_ERR(req_p))
		return PTR_ERR(req_p);

	req_p->hdr.type = cpu_to_le32(VIRTIO_VIDEO_CMD_SET_PARAMS);
	req_p->hdr.stream_id = cpu_to_le32(stream->stream_id);
	req_p->params.queue_type = cpu_to_le32(queue_type);
	req_p->params.frame_rate = cpu_to_le32(format_info->frame_rate);
	req_p->params.frame_width = cpu_to_le32(format_info->frame_width);
	req_p->params.frame_height = cpu_to_le32(format_info->frame_height);
	req_p->params.format = virtio_video_v4l2_format_to_virtio(
				 cpu_to_le32(format_info->fourcc_format));
	req_p->params.min_buffers = cpu_to_le32(format_info->min_buffers);
	req_p->params.max_buffers = cpu_to_le32(format_info->max_buffers);
	req_p->params.num_planes = cpu_to_le32(format_info->num_planes);

	for (i = 0; i < format_info->num_planes; i++) {
		struct virtio_video_plane_format *plane_formats =
			&req_p->params.plane_formats[i];
		struct video_plane_format *plane_format =
			&format_info->plane_format[i];
		plane_formats->plane_size =
				 cpu_to_le32(plane_format->plane_size);
		plane_formats->stride = cpu_to_le32(plane_format->stride);
	}

	return virtio_video_queue_cmd_buffer(vvd, vbuf);
}

static void
virtio_video_cmd_get_ctrl_profile_cb(struct virtio_video_device *vvd,
				     struct virtio_video_vbuffer *vbuf)
{
	struct virtio_video_get_control_resp *resp =
		(struct virtio_video_get_control_resp *)vbuf->resp_buf;
	struct virtio_video_control_val_profile *resp_p = NULL;
	struct virtio_video_stream *stream = vbuf->priv;
	struct video_control_info *control = &stream->control;

	resp_p = (void *)((char *)resp +
			  sizeof(struct virtio_video_get_control_resp));

	control->profile = le32_to_cpu(resp_p->profile);
}

static void
virtio_video_cmd_get_ctrl_level_cb(struct virtio_video_device *vvd,
				   struct virtio_video_vbuffer *vbuf)
{
	struct virtio_video_get_control_resp *resp =
		(struct virtio_video_get_control_resp *)vbuf->resp_buf;
	struct virtio_video_control_val_level *resp_p;
	struct virtio_video_stream *stream = vbuf->priv;
	struct video_control_info *control = &stream->control;

	resp_p = (void *)((char *)resp +
			  sizeof(struct virtio_video_get_control_resp));

	control->level = le32_to_cpu(resp_p->level);
}

static void
virtio_video_cmd_get_ctrl_bitrate_cb(struct virtio_video_device *vvd,
				     struct virtio_video_vbuffer *vbuf)
{
	struct virtio_video_get_control_resp *resp =
		(struct virtio_video_get_control_resp *)vbuf->resp_buf;
	struct virtio_video_control_val_bitrate *resp_p = NULL;
	struct virtio_video_stream *stream = vbuf->priv;
	struct video_control_info *control = &stream->control;

	resp_p = (void *)((char *) resp +
			  sizeof(struct virtio_video_get_control_resp));

	control->bitrate = le32_to_cpu(resp_p->bitrate);
}

// TODO: replace virtio_video_cmd_hdr accoring to specification v4
int virtio_video_cmd_get_control(struct virtio_video_device *vvd,
				 struct virtio_video_stream *stream,
				 enum virtio_video_control_type control)
{
	int ret;
	struct virtio_video_get_control *req_p;
	struct virtio_video_get_control_resp *resp_p;
	struct virtio_video_vbuffer *vbuf;
	size_t resp_size = sizeof(struct virtio_video_get_control_resp);
	virtio_video_resp_cb cb;

	switch (control) {
	case VIRTIO_VIDEO_CONTROL_PROFILE:
		resp_size += sizeof(struct virtio_video_control_val_profile);
		cb = &virtio_video_cmd_get_ctrl_profile_cb;
		break;
	case VIRTIO_VIDEO_CONTROL_LEVEL:
		resp_size += sizeof(struct virtio_video_control_val_level);
		cb = &virtio_video_cmd_get_ctrl_level_cb;
		break;
	case VIRTIO_VIDEO_CONTROL_BITRATE:
		resp_size += sizeof(struct virtio_video_control_val_bitrate);
		cb = &virtio_video_cmd_get_ctrl_bitrate_cb;
		break;
	default:
		return -EINVAL;
	}

	req_p = virtio_video_alloc_req_resp(vvd, cb, &vbuf,
					    sizeof(*req_p), resp_size, NULL);
	if (IS_ERR(req_p))
		return PTR_ERR(req_p);

	req_p->hdr.type = cpu_to_le32(VIRTIO_VIDEO_CMD_GET_CONTROL);
	req_p->hdr.stream_id = cpu_to_le32(stream->stream_id);
	req_p->control = cpu_to_le32(control);

	resp_p = (struct virtio_video_get_control_resp *)vbuf->resp_buf;

	vbuf->priv = stream;

	ret = virtio_video_queue_cmd_buffer_sync(vvd, vbuf);
	if (ret == -ETIMEDOUT)
		v4l2_err(&vvd->v4l2_dev,
			 "timed out waiting for get_control\n");
	return ret;
}

// TODO: replace virtio_video_cmd_hdr accoring to specification v4
int virtio_video_cmd_set_control(struct virtio_video_device *vvd,
				 uint32_t stream_id,
				 enum virtio_video_control_type control,
				 uint32_t value)
{
	struct virtio_video_set_control *req_p;
	struct virtio_video_vbuffer *vbuf;
	struct virtio_video_control_val_level *ctrl_l;
	struct virtio_video_control_val_profile *ctrl_p;
	struct virtio_video_control_val_bitrate *ctrl_b;
	size_t size;

	if (value == 0)
		return -EINVAL;

	switch (control) {
	case VIRTIO_VIDEO_CONTROL_PROFILE:
		size = sizeof(struct virtio_video_control_val_profile);
		break;
	case VIRTIO_VIDEO_CONTROL_LEVEL:
		size = sizeof(struct virtio_video_control_val_level);
		break;
	case VIRTIO_VIDEO_CONTROL_BITRATE:
		size = sizeof(struct virtio_video_control_val_bitrate);
		break;
	default:
		return -EINVAL;
	}

	req_p = virtio_video_alloc_req(vvd, &vbuf, size + sizeof(*req_p));
	if (IS_ERR(req_p))
		return PTR_ERR(req_p);

	req_p->hdr.type = cpu_to_le32(VIRTIO_VIDEO_CMD_SET_CONTROL);
	req_p->hdr.stream_id = cpu_to_le32(stream_id);
	req_p->control = cpu_to_le32(control);

	switch (control) {
	case VIRTIO_VIDEO_CONTROL_PROFILE:
		ctrl_p = (void *)((char *)req_p +
				  sizeof(struct virtio_video_set_control));
		ctrl_p->profile = cpu_to_le32(value);
		break;
	case VIRTIO_VIDEO_CONTROL_LEVEL:
		ctrl_l = (void *)((char *)req_p +
				 sizeof(struct virtio_video_set_control));
		ctrl_l->level = cpu_to_le32(value);
		break;
	case VIRTIO_VIDEO_CONTROL_BITRATE:
		ctrl_b = (void *)((char *)req_p +
				 sizeof(struct virtio_video_set_control));
		ctrl_b->bitrate = cpu_to_le32(value);
		break;
	}

	return virtio_video_queue_cmd_buffer(vvd, vbuf);
}

