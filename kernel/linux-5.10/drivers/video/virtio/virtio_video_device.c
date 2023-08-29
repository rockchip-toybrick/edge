// SPDX-License-Identifier: GPL-2.0+
/* Driver for virtio video device.
 *
 * Copyright 2020 OpenSynergy GmbH.
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

#include <linux/version.h>
#include <media/v4l2-event.h>
#include <media/v4l2-ioctl.h>
#include <media/videobuf2-dma-sg.h>

#include "virtio_video.h"

enum video_stream_state virtio_video_state(struct virtio_video_stream *stream)
{
	return atomic_read(&stream->state);
}

void virtio_video_state_reset(struct virtio_video_stream *stream)
{
	atomic_set(&stream->state, STREAM_STATE_IDLE);
}

void virtio_video_state_update(struct virtio_video_stream *stream,
			       enum video_stream_state new_state)
{
	enum video_stream_state prev_state;

	do {
	    prev_state = atomic_read(&stream->state);
	    if (prev_state == STREAM_STATE_ERROR)
		    return;
	} while (atomic_cmpxchg(&stream->state, prev_state, new_state) !=
		 prev_state);
}

int virtio_video_pending_buf_list_empty(struct virtio_video_device *vvd)
{
	int ret = 0;

	if (vvd->is_m2m_dev) {
		v4l2_err(&vvd->v4l2_dev, "Unexpected call for m2m device!\n");
		return -EPERM;
	}

	spin_lock(&vvd->pending_buf_list_lock);
	if (list_empty(&vvd->pending_buf_list))
		ret = 1;
	spin_unlock(&vvd->pending_buf_list_lock);

	return ret;
}

int virtio_video_pending_buf_list_pop(struct virtio_video_device *vvd,
				      struct virtio_video_buffer **virtio_vb)
{
	struct virtio_video_buffer *retbuf;

	if (vvd->is_m2m_dev) {
		v4l2_err(&vvd->v4l2_dev, "Unexpected call for m2m device!\n");
		return -EPERM;
	}

	spin_lock(&vvd->pending_buf_list_lock);
	if (list_empty(&vvd->pending_buf_list)) {
		spin_unlock(&vvd->pending_buf_list_lock);
		return -EAGAIN;
	}

	retbuf = list_first_entry(&vvd->pending_buf_list,
				  struct virtio_video_buffer, list);
	spin_unlock(&vvd->pending_buf_list_lock);

	*virtio_vb = retbuf;
	return 0;
}

int virtio_video_pending_buf_list_add(struct virtio_video_device *vvd,
				      struct virtio_video_buffer *virtio_vb)
{
	if (vvd->is_m2m_dev) {
		v4l2_err(&vvd->v4l2_dev, "Unexpected call for m2m device!\n");
		return -EPERM;
	}

	spin_lock(&vvd->pending_buf_list_lock);
	list_add_tail(&virtio_vb->list, &vvd->pending_buf_list);
	spin_unlock(&vvd->pending_buf_list_lock);

	return 0;
}

int virtio_video_pending_buf_list_del(struct virtio_video_device *vvd,
				      struct virtio_video_buffer *virtio_vb)
{
	struct virtio_video_buffer *vb, *vb_tmp;
	int ret = -EINVAL;

	if (vvd->is_m2m_dev) {
		v4l2_err(&vvd->v4l2_dev, "Unexpected call for m2m device!\n");
		return -EPERM;
	}

	spin_lock(&vvd->pending_buf_list_lock);
	if (list_empty(&vvd->pending_buf_list)) {
		spin_unlock(&vvd->pending_buf_list_lock);
		return -EAGAIN;
	}

	list_for_each_entry_safe(vb, vb_tmp, &vvd->pending_buf_list, list) {
		if (vb->resource_id == virtio_vb->resource_id) {
			list_del(&vb->list);
			ret = 0;
			break;
		}
	}
	spin_unlock(&vvd->pending_buf_list_lock);

	return ret;
}

int virtio_video_queue_setup(struct vb2_queue *vq, unsigned int *num_buffers,
			     unsigned int *num_planes, unsigned int sizes[],
			     struct device *alloc_devs[])
{
	int i;
	struct virtio_video_stream *stream = vb2_get_drv_priv(vq);
	struct video_format_info *p_info;

	if (virtio_video_state(stream) == STREAM_STATE_ERROR)
		return -EIO;

	if (*num_planes)
		return 0;

	if (V4L2_TYPE_IS_OUTPUT(vq->type))
		p_info = &stream->in_info;
	else
		p_info = &stream->out_info;

	*num_planes = p_info->num_planes;

	for (i = 0; i < p_info->num_planes; i++)
		sizes[i] = p_info->plane_format[i].plane_size;

	return 0;
}

static unsigned int
build_virtio_video_sglist_contig(struct virtio_video_resource_sg_list *sgl,
			         struct vb2_buffer *vb, unsigned int plane)
{
	sgl->entries[0].addr = cpu_to_le64(vb2_dma_contig_plane_dma_addr(vb, plane));
	sgl->entries[0].length = cpu_to_le32(vb->planes[plane].length);

	sgl->num_entries = 1;

	return VIRTIO_VIDEO_RESOURCE_SG_SIZE(1);
}

static unsigned int
build_virtio_video_sglist(struct virtio_video_resource_sg_list *sgl,
			  struct vb2_buffer *vb, unsigned int plane,
			  bool has_iommu)
{
	int i;
	struct scatterlist *sg;
	struct sg_table *sgt = vb2_dma_sg_plane_desc(vb, plane);

	for_each_sg(sgt->sgl, sg, sgt->nents, i) {
		sgl->entries[i].addr = cpu_to_le64(has_iommu
							? sg_dma_address(sg)
							: sg_phys(sg));
		sgl->entries[i].length = cpu_to_le32(sg->length);
	}

	sgl->num_entries = sgt->nents;

	return VIRTIO_VIDEO_RESOURCE_SG_SIZE(sgt->nents);
}

int virtio_video_buf_init(struct vb2_buffer *vb)
{
	int ret = 0;
	void *buf;
	size_t buf_size = 0;
	struct virtio_video_resource_sg_list *sg_list;
	unsigned int i, offset = 0, resource_id, nents = 0;
	struct vb2_queue *vq = vb->vb2_queue;
	enum v4l2_buf_type queue_type = vq->type;
	struct virtio_video_stream *stream = vb2_get_drv_priv(vq);
	struct virtio_video_buffer *virtio_vb = to_virtio_vb(vb);
	struct virtio_video_device *vvd = to_virtio_vd(stream->video_dev);

	if (vvd->supp_non_contig) {
		for (i = 0; i < vb->num_planes; i++) {
			nents = vb2_dma_sg_plane_desc(vb, i)->nents;
			buf_size += VIRTIO_VIDEO_RESOURCE_SG_SIZE(nents);
		}

		buf = kcalloc(1, buf_size, GFP_KERNEL);
		if (!buf)
			return -ENOMEM;

		for (i = 0; i < vb->num_planes; i++) {
			sg_list = buf + offset;
			offset += build_virtio_video_sglist(sg_list, vb, i,
							    vvd->has_iommu);
		}
	} else {
		buf_size = vb->num_planes * VIRTIO_VIDEO_RESOURCE_SG_SIZE(nents);

		buf = kcalloc(1, buf_size, GFP_KERNEL);
		if (!buf)
			return -ENOMEM;

		for (i = 0; i < vb->num_planes; i++) {
			sg_list = buf + offset;
			offset += build_virtio_video_sglist_contig(sg_list,
								   vb, i);
		}
	}

	virtio_video_resource_id_get(vvd, &resource_id);

	ret = virtio_video_cmd_resource_attach(vvd, stream->stream_id,
					       resource_id,
					       to_virtio_queue_type(queue_type),
					       buf, buf_size);
	if (ret) {
		virtio_video_resource_id_put(vvd, resource_id);
		kfree(buf);
		return ret;
	}

	virtio_vb->queued = false;
	virtio_vb->resource_id = resource_id;

	return 0;
}

void virtio_video_buf_cleanup(struct vb2_buffer *vb)
{
	struct virtio_video_stream *stream = vb2_get_drv_priv(vb->vb2_queue);
	struct virtio_video_buffer *virtio_vb = to_virtio_vb(vb);
	struct virtio_video_device *vvd = to_virtio_vd(stream->video_dev);

	virtio_video_resource_id_put(vvd, virtio_vb->resource_id);
}

void virtio_video_buf_queue(struct vb2_buffer *vb)
{
	int i, ret;
	struct virtio_video_buffer *virtio_vb;
	uint32_t data_size[VB2_MAX_PLANES] = {0};
	struct virtio_video_stream *stream = vb2_get_drv_priv(vb->vb2_queue);
	struct virtio_video_device *vvd = to_virtio_vd(stream->video_dev);

	for (i = 0; i < vb->num_planes; ++i)
		data_size[i] = vb->planes[i].bytesused;

	virtio_vb = to_virtio_vb(vb);

	if (!vvd->is_m2m_dev)
		virtio_video_pending_buf_list_add(vvd, virtio_vb);

	ret = virtio_video_cmd_resource_queue(vvd, stream->stream_id,
					      virtio_vb, data_size,
					      vb->num_planes,
					      to_virtio_queue_type(vb->type));
	if (ret) {
		v4l2_err(&vvd->v4l2_dev, "failed to queue buffer\n");
		return;
	}

	virtio_vb->queued = true;
}

int virtio_video_qbuf(struct file *file, void *priv,
		      struct v4l2_buffer *buf)
{
	struct virtio_video_stream *stream = file2stream(file);
	struct virtio_video_device *vvd = video_drvdata(file);

	if (virtio_video_state(stream) == STREAM_STATE_ERROR)
		return -EIO;

	if (vvd->is_m2m_dev)
		return v4l2_m2m_ioctl_qbuf(file, priv, buf);

	return vb2_ioctl_qbuf(file, priv, buf);
}

int virtio_video_dqbuf(struct file *file, void *priv,
		       struct v4l2_buffer *buf)
{
	struct virtio_video_stream *stream = file2stream(file);
	struct virtio_video_device *vvd = video_drvdata(file);

	if (virtio_video_state(stream) == STREAM_STATE_ERROR)
		return -EIO;

	if (vvd->is_m2m_dev)
		return v4l2_m2m_ioctl_dqbuf(file, priv, buf);

	return vb2_ioctl_dqbuf(file, priv, buf);
}

int virtio_video_querycap(struct file *file, void *fh,
			  struct v4l2_capability *cap)
{
	struct video_device *video_dev = video_devdata(file);
	struct virtio_video_device *vvd = video_drvdata(file);

	if (strscpy(cap->driver, DRIVER_NAME, sizeof(cap->driver)) < 0)
		v4l2_err(&vvd->v4l2_dev, "failed to copy driver name\n");
	if (strscpy(cap->card, video_dev->name, sizeof(cap->card)) < 0)
		v4l2_err(&vvd->v4l2_dev, "failed to copy card name\n");

	snprintf(cap->bus_info, sizeof(cap->bus_info), "virtio:%s",
		 video_dev->name);

	cap->device_caps = video_dev->device_caps;
	return 0;
}

int virtio_video_enum_framesizes(struct file *file, void *fh,
				 struct v4l2_frmsizeenum *f)
{
	struct virtio_video_stream *stream = file2stream(file);
	struct virtio_video_device *vvd = to_virtio_vd(stream->video_dev);
	struct video_format *fmt;
	struct video_format_frame *frm;
	struct virtio_video_format_frame *frame;
	int idx = f->index;

	fmt = virtio_video_find_video_format(&vvd->input_fmt_list,
					     f->pixel_format);
	if (fmt == NULL)
		fmt = virtio_video_find_video_format(&vvd->output_fmt_list,
						     f->pixel_format);
	if (fmt == NULL)
		return -EINVAL;

	if (idx >= fmt->desc.num_frames)
		return -EINVAL;

	frm = &fmt->frames[idx];
	frame = &frm->frame;

	if (frame->width.min == frame->width.max &&
	    frame->height.min == frame->height.max) {
		f->type = V4L2_FRMSIZE_TYPE_DISCRETE;
		f->discrete.width = frame->width.min;
		f->discrete.height = frame->height.min;
		return 0;
	}

	f->type = V4L2_FRMSIZE_TYPE_CONTINUOUS;
	f->stepwise.min_width = frame->width.min;
	f->stepwise.max_width = frame->width.max;
	f->stepwise.min_height = frame->height.min;
	f->stepwise.max_height = frame->height.max;
	f->stepwise.step_width = frame->width.step;
	f->stepwise.step_height = frame->height.step;
	return 0;
}

static bool in_stepped_interval(struct virtio_video_format_range range,
				uint32_t point)
{
	if (point < range.min || point > range.max)
		return false;

	if (range.step == 0 && range.min == range.max && range.min == point)
		return true;

	if (range.step != 0 && (point - range.min) % range.step == 0)
		return true;

	return false;
}

int virtio_video_enum_framemintervals(struct file *file, void *fh,
				      struct v4l2_frmivalenum *f)
{
	struct virtio_video_stream *stream = file2stream(file);
	struct virtio_video_device *vvd = to_virtio_vd(stream->video_dev);
	struct video_format *fmt;
	struct video_format_frame *frm;
	struct virtio_video_format_frame *frame;
	struct virtio_video_format_range *frate;
	int idx = f->index;
	int f_idx;

	fmt = virtio_video_find_video_format(&vvd->input_fmt_list,
					     f->pixel_format);
	if (fmt == NULL)
		fmt = virtio_video_find_video_format(&vvd->output_fmt_list,
						     f->pixel_format);
	if (fmt == NULL)
		return -EINVAL;

	for (f_idx = 0; f_idx <= fmt->desc.num_frames; f_idx++) {
		frm = &fmt->frames[f_idx];
		frame = &frm->frame;
		if (in_stepped_interval(frame->width, f->width) &&
		    in_stepped_interval(frame->height, f->height))
			break;
	}

	if (frame == NULL || f->index >= frame->num_rates)
		return -EINVAL;

	frate = &frm->frame_rates[idx];
	if (frate->max == frate->min) {
		f->type = V4L2_FRMIVAL_TYPE_DISCRETE;
		f->discrete.numerator = 1;
		f->discrete.denominator = frate->max;
	} else {
		f->stepwise.min.numerator = 1;
		f->stepwise.min.denominator = frate->max;
		f->stepwise.max.numerator = 1;
		f->stepwise.max.denominator = frate->min;
		f->stepwise.step.numerator = 1;
		f->stepwise.step.denominator = frate->step;
		if (frate->step == 1)
			f->type = V4L2_FRMIVAL_TYPE_CONTINUOUS;
		else
			f->type = V4L2_FRMIVAL_TYPE_STEPWISE;
	}
	return 0;
}

int virtio_video_stream_get_params(struct virtio_video_device *vvd,
				   struct virtio_video_stream *stream)
{
	int ret;

	if (vvd->is_m2m_dev) {
		ret = virtio_video_cmd_get_params(vvd, stream,
						VIRTIO_VIDEO_QUEUE_TYPE_INPUT);
		if (ret) {
			v4l2_err(&vvd->v4l2_dev,
				 "failed to get stream in params\n");
			goto err_get_parms;
		}
	}

	ret = virtio_video_cmd_get_params(vvd, stream,
					  VIRTIO_VIDEO_QUEUE_TYPE_OUTPUT);
	if (ret)
		v4l2_err(&vvd->v4l2_dev, "failed to get stream out params\n");

err_get_parms:
	return ret;
}

int virtio_video_stream_get_controls(struct virtio_video_device *vvd,
				     struct virtio_video_stream *stream)
{
	int ret;

	ret = virtio_video_cmd_get_control(vvd, stream,
					   VIRTIO_VIDEO_CONTROL_PROFILE);
	if (ret) {
		v4l2_err(&vvd->v4l2_dev, "failed to get stream profile\n");
		goto err_get_ctrl;
	}

	ret = virtio_video_cmd_get_control(vvd, stream,
					   VIRTIO_VIDEO_CONTROL_LEVEL);
	if (ret) {
		v4l2_err(&vvd->v4l2_dev, "failed to get stream level\n");
		goto err_get_ctrl;
	}

	ret = virtio_video_cmd_get_control(vvd, stream,
					   VIRTIO_VIDEO_CONTROL_BITRATE);
	if (ret)
		v4l2_err(&vvd->v4l2_dev, "failed to get stream bitrate\n");

err_get_ctrl:
	return ret;
}

int virtio_video_g_fmt(struct file *file, void *fh, struct v4l2_format *f)
{
	struct video_format_info *info;
	struct v4l2_pix_format_mplane *pix_mp = &f->fmt.pix_mp;
	struct virtio_video_stream *stream = file2stream(file);

	if (!V4L2_TYPE_IS_OUTPUT(f->type))
		info = &stream->out_info;
	else
		info = &stream->in_info;

	virtio_video_format_from_info(info, pix_mp);

	return 0;
}

int virtio_video_s_fmt(struct file *file, void *fh, struct v4l2_format *f)
{
	int i, ret;
	struct virtio_video_stream *stream = file2stream(file);
	struct v4l2_pix_format_mplane *pix_mp = &f->fmt.pix_mp;
	struct virtio_video_device *vvd = to_virtio_vd(stream->video_dev);
	struct video_format_info info;
	struct video_format_info *p_info;
	uint32_t queue;
	bool resolution_is_invalid = (pix_mp->width == UINT_MAX && pix_mp->height == UINT_MAX) || (pix_mp->width == 0 && pix_mp->height == 0);

	ret = virtio_video_try_fmt(stream, f);
	if (ret)
		return ret;

	if (V4L2_TYPE_IS_OUTPUT(f->type)) {
		virtio_video_format_fill_default_info(&info, &stream->in_info);
		queue = VIRTIO_VIDEO_QUEUE_TYPE_INPUT;
	} else {
		virtio_video_format_fill_default_info(&info, &stream->out_info);
		queue = VIRTIO_VIDEO_QUEUE_TYPE_OUTPUT;
	}

	// On QNX we treat clearly invalid resolution values as zero. Otherwise the
	// virtio_video_try_fmt() call above will have changed these to the nearest
	// valid supported resolution. For example, UINT_MAX x UINT_MAX becomes
	// 1920x1080, and 0x0 becomes 128x96, making it impossible for the vdev to
	// know that the resolution passed is actually invalid. These invalid values
	// are passed for VIRTIO_VIDEO_QUEUE_TYPE_OUTPUT (which from the vdev's
	// point of view is the input queue) because in v4l2_codec2 code the
	// V4L2Decoder::setupInputFormat() function uses the ui::Size() default
	// constructor as the resolution passed to mInputQueue->setFormat().
	info.frame_width = resolution_is_invalid ? 0 : pix_mp->width;
	info.frame_height = resolution_is_invalid ? 0 : pix_mp->height;
	info.num_planes = pix_mp->num_planes;
	info.fourcc_format = pix_mp->pixelformat;

	for (i = 0; i < info.num_planes; i++) {
		info.plane_format[i].stride =
					 pix_mp->plane_fmt[i].bytesperline;
		info.plane_format[i].plane_size =
					 pix_mp->plane_fmt[i].sizeimage;
	}

	virtio_video_cmd_set_params(vvd, stream, &info, queue);
	virtio_video_stream_get_params(vvd, stream);

	if (V4L2_TYPE_IS_OUTPUT(f->type))
		p_info = &stream->in_info;
	else
		p_info = &stream->out_info;

	virtio_video_format_from_info(p_info, pix_mp);

	return 0;
}

int virtio_video_g_selection(struct file *file, void *fh,
			 struct v4l2_selection *sel)
{
	struct video_format_info *info;
	struct virtio_video_stream *stream = file2stream(file);
	struct virtio_video_device *vvd = video_drvdata(file);

	switch (vvd->type) {
	case VIRTIO_VIDEO_DEVICE_ENCODER:
		if (!V4L2_TYPE_IS_OUTPUT(sel->type))
			return -EINVAL;
		info = &stream->in_info;
		break;
	case VIRTIO_VIDEO_DEVICE_DECODER:
	case VIRTIO_VIDEO_DEVICE_CAMERA:
		if (V4L2_TYPE_IS_OUTPUT(sel->type))
			return -EINVAL;
		info = &stream->out_info;
		break;
	default:
		v4l2_err(&vvd->v4l2_dev, "unsupported device type\n");
		return -EINVAL;
	}

	switch (sel->target) {
	case V4L2_SEL_TGT_COMPOSE:
	case V4L2_SEL_TGT_COMPOSE_BOUNDS:
	case V4L2_SEL_TGT_COMPOSE_PADDED:
	case V4L2_SEL_TGT_COMPOSE_DEFAULT:
		sel->r.width = info->frame_width;
		sel->r.height = info->frame_height;
		break;
	case V4L2_SEL_TGT_CROP_BOUNDS:
	case V4L2_SEL_TGT_CROP_DEFAULT:
	case V4L2_SEL_TGT_CROP:
		sel->r.top = info->crop.top;
		sel->r.left = info->crop.left;
		sel->r.width = info->frame_width;
		sel->r.height = info->frame_height;
		break;
	default:
		v4l2_dbg(1, vvd->debug, &vvd->v4l2_dev,
			 "unsupported/invalid selection target: %d\n",
			 sel->target);
		return -EINVAL;
	}

	return 0;
}

int virtio_video_try_fmt(struct virtio_video_stream *stream,
			 struct v4l2_format *f)
{
	int i, idx = 0;
	struct v4l2_pix_format_mplane *pix_mp = &f->fmt.pix_mp;
	struct virtio_video_device *vvd = to_virtio_vd(stream->video_dev);
	struct video_format *fmt;
	bool found = false;
	struct video_format_frame *frm;
	struct virtio_video_format_frame *frame;

	if (virtio_video_state(stream) == STREAM_STATE_ERROR)
		return -EIO;

	if (V4L2_TYPE_IS_OUTPUT(f->type))
		fmt = virtio_video_find_video_format(&vvd->input_fmt_list,
						     pix_mp->pixelformat);
	else
		fmt = virtio_video_find_video_format(&vvd->output_fmt_list,
						     pix_mp->pixelformat);

	if (!fmt) {
		if (f->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE)
			virtio_video_format_from_info(&stream->out_info,
						      pix_mp);
		else if (f->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE)
			virtio_video_format_from_info(&stream->in_info,
						      pix_mp);
		else
			return -EINVAL;
		return 0;
	}

	for (i = 0; i < fmt->desc.num_frames && !found; i++) {
		frm = &fmt->frames[i];
		frame = &frm->frame;
		if (!within_range(frame->width.min, pix_mp->width,
				  frame->width.max))
			continue;

		if (!within_range(frame->height.min, pix_mp->height,
				  frame->height.max))
			continue;
		idx = i;
		/*
		 * Try to find a more suitable frame size. Go with the current
		 * one otherwise.
		 */
		if (needs_alignment(pix_mp->width, frame->width.step))
			continue;

		if (needs_alignment(pix_mp->height, frame->height.step))
			continue;

		stream->current_frame = frm;
		found = true;
	}

	if (!found) {
		frm = &fmt->frames[idx];
		if (!frm)
			return -EINVAL;

		frame = &frm->frame;
		pix_mp->width = clamp(pix_mp->width, frame->width.min,
				      frame->width.max);
		if (frame->width.step != 0)
			pix_mp->width = ALIGN(pix_mp->width, frame->width.step);

		pix_mp->height = clamp(pix_mp->height, frame->height.min,
				       frame->height.max);
		if (frame->height.step != 0)
			pix_mp->height = ALIGN(pix_mp->height,
					       frame->height.step);
		stream->current_frame = frm;
	}

	return 0;
}

static int virtio_video_queue_free(struct virtio_video_device *vvd,
				   struct virtio_video_stream *stream,
				   enum v4l2_buf_type type)
{
	int ret;
	enum virtio_video_queue_type queue_type = to_virtio_queue_type(type);

	ret = virtio_video_cmd_queue_detach_resources(vvd, stream, queue_type);
	if (ret) {
		v4l2_warn(&vvd->v4l2_dev,
			  "failed to destroy resources\n");
		return ret;
	}

	return 0;
}

int virtio_video_reqbufs(struct file *file, void *priv,
			 struct v4l2_requestbuffers *rb)
{
	int ret;
	struct virtio_video_stream *stream = file2stream(file);
	struct v4l2_m2m_ctx *m2m_ctx = stream->fh.m2m_ctx;
	struct virtio_video_device *vvd = video_drvdata(file);
	struct video_device *vdev = video_devdata(file);
	struct vb2_queue *vq;

	if (vvd->is_m2m_dev)
		vq = v4l2_m2m_get_vq(m2m_ctx, rb->type);
	else
		vq = vdev->queue;

	if (rb->count == 0) {
		ret = virtio_video_queue_free(vvd, stream, vq->type);
		if (ret < 0)
			return ret;
	}

	if (vvd->is_m2m_dev)
		return v4l2_m2m_reqbufs(file, m2m_ctx, rb);
	else
		return vb2_ioctl_reqbufs(file, priv, rb);
}

int virtio_video_subscribe_event(struct v4l2_fh *fh,
				 const struct v4l2_event_subscription *sub)
{
	switch (sub->type) {
	case V4L2_EVENT_SOURCE_CHANGE:
		return v4l2_src_change_event_subscribe(fh, sub);
	default:
		return v4l2_ctrl_subscribe_event(fh, sub);
	}
}

void virtio_video_queue_eos_event(struct virtio_video_stream *stream)
{
	static const struct v4l2_event eos_event = {
		.type = V4L2_EVENT_EOS
	};

	v4l2_event_queue_fh(&stream->fh, &eos_event);
}

void virtio_video_queue_res_chg_event(struct virtio_video_stream *stream)
{
	static const struct v4l2_event ev_src_ch = {
		.type = V4L2_EVENT_SOURCE_CHANGE,
		.u.src_change.changes =
			V4L2_EVENT_SRC_CH_RESOLUTION,
	};

	v4l2_event_queue_fh(&stream->fh, &ev_src_ch);
}

void virtio_video_handle_error(struct virtio_video_stream *stream)
{
	struct virtio_video_device *vvd = to_virtio_vd(stream->video_dev);

	if (vvd->is_m2m_dev)
		virtio_video_queue_release_buffers
			(stream, VIRTIO_VIDEO_QUEUE_TYPE_INPUT);

	virtio_video_queue_release_buffers
		(stream, VIRTIO_VIDEO_QUEUE_TYPE_OUTPUT);
}

int virtio_video_queue_release_buffers(struct virtio_video_stream *stream,
				       enum virtio_video_queue_type queue_type)
{
	int ret;
	struct virtio_video_device *vvd = to_virtio_vd(stream->video_dev);
	struct vb2_v4l2_buffer *v4l2_vb;
	struct virtio_video_buffer *vvb;

	ret = virtio_video_cmd_queue_clear(vvd, stream, queue_type);
	if (ret) {
		v4l2_err(&vvd->v4l2_dev, "failed to clear queue\n");
		return ret;
	}

	if (!vvd->is_m2m_dev) {
		while (!virtio_video_pending_buf_list_pop(vvd, &vvb) && vvb) {
			v4l2_vb = &vvb->v4l2_m2m_vb.vb;
			v4l2_m2m_buf_done(v4l2_vb, VB2_BUF_STATE_ERROR);
		}
		return 0;
	}

	for (;;) {
		if (queue_type == VIRTIO_VIDEO_QUEUE_TYPE_INPUT)
			v4l2_vb = v4l2_m2m_src_buf_remove(stream->fh.m2m_ctx);
		else
			v4l2_vb = v4l2_m2m_dst_buf_remove(stream->fh.m2m_ctx);
		if (!v4l2_vb)
			break;
		v4l2_m2m_buf_done(v4l2_vb, VB2_BUF_STATE_ERROR);
	}

	return 0;
}

void virtio_video_buf_done(struct virtio_video_buffer *virtio_vb,
			   uint32_t flags, uint64_t timestamp,
			   uint32_t data_sizes[])
{
	int i;
	enum vb2_buffer_state done_state = VB2_BUF_STATE_DONE;
	struct vb2_v4l2_buffer *v4l2_vb = &virtio_vb->v4l2_m2m_vb.vb;
	struct vb2_buffer *vb = &v4l2_vb->vb2_buf;
	struct vb2_queue *vb2_queue = vb->vb2_queue;
	struct virtio_video_stream *stream = vb2_get_drv_priv(vb2_queue);
	struct virtio_video_device *vvd = to_virtio_vd(stream->video_dev);
	struct video_format_info *p_info;

	virtio_vb->queued = false;

	if (flags & VIRTIO_VIDEO_DEQUEUE_FLAG_ERR)
		done_state = VB2_BUF_STATE_ERROR;

	if (flags & VIRTIO_VIDEO_DEQUEUE_FLAG_KEY_FRAME)
		v4l2_vb->flags |= V4L2_BUF_FLAG_KEYFRAME;

	if (flags & VIRTIO_VIDEO_DEQUEUE_FLAG_BFRAME)
		v4l2_vb->flags |= V4L2_BUF_FLAG_BFRAME;

	if (flags & VIRTIO_VIDEO_DEQUEUE_FLAG_PFRAME)
		v4l2_vb->flags |= V4L2_BUF_FLAG_PFRAME;

	if (flags & VIRTIO_VIDEO_DEQUEUE_FLAG_EOS) {
		v4l2_vb->flags |= V4L2_BUF_FLAG_LAST;
		virtio_video_state_update(stream, STREAM_STATE_STOPPED);
		virtio_video_queue_eos_event(stream);
	}

	if ((flags & VIRTIO_VIDEO_DEQUEUE_FLAG_ERR) ||
	    (flags & VIRTIO_VIDEO_DEQUEUE_FLAG_EOS)) {
		vb->planes[0].bytesused = 0;

		if (!vvd->is_m2m_dev)
			virtio_video_pending_buf_list_del(vvd, virtio_vb);

		v4l2_m2m_buf_done(v4l2_vb, done_state);
		return;
	}

	if (!V4L2_TYPE_IS_OUTPUT(vb2_queue->type)) {
		switch (vvd->type) {
		case VIRTIO_VIDEO_DEVICE_ENCODER:
			for (i = 0; i < vb->num_planes; i++)
				vb->planes[i].bytesused =
					le32_to_cpu(data_sizes[i]);
			break;
		case VIRTIO_VIDEO_DEVICE_CAMERA:
		case VIRTIO_VIDEO_DEVICE_DECODER:
			p_info = &stream->out_info;
			for (i = 0; i < p_info->num_planes; i++)
				vb->planes[i].bytesused =
					p_info->plane_format[i].plane_size;
			break;
		}

		vb->timestamp = timestamp;
	}

	if (!vvd->is_m2m_dev)
		virtio_video_pending_buf_list_del(vvd, virtio_vb);

	v4l2_m2m_buf_done(v4l2_vb, done_state);
}

static int virtio_video_set_device_busy(struct virtio_video_device *vvd)
{
	struct video_device *vd = &vvd->video_dev;
	int ret = 0;

	/* Multiple open is allowed for m2m device */
	if (vvd->is_m2m_dev)
		return 0;

	mutex_lock(vd->lock);

	if (vvd->device_busy)
		ret = -EBUSY;
	else
		vvd->device_busy = true;

	mutex_unlock(vd->lock);

	return ret;
}

static void virtio_video_clear_device_busy(struct virtio_video_device *vvd,
					   struct mutex *lock)
{
	/* Nothing to do for m2m device */
	if (vvd->is_m2m_dev)
		return;

	if (lock)
		mutex_lock(lock);

	vvd->device_busy = false;

	if (lock)
		mutex_unlock(lock);
}

static int virtio_video_device_open(struct file *file)
{
	int ret;
	uint32_t stream_id;
	char name[TASK_COMM_LEN];
	struct virtio_video_stream *stream;
	struct video_format *default_fmt;
	enum virtio_video_format format;
	struct video_device *video_dev = video_devdata(file);
	struct virtio_video_device *vvd = video_drvdata(file);

	ret = virtio_video_set_device_busy(vvd);
	if (ret) {
		v4l2_err(&vvd->v4l2_dev, "device already in use.\n");
		return ret;
	}

	default_fmt = list_first_entry_or_null(vvd->ops->get_fmt_list(vvd),
					       struct video_format,
					       formats_list_entry);
	if (!default_fmt) {
		v4l2_err(&vvd->v4l2_dev, "device failed to start\n");
		ret = -EIO;
		goto err;
	}

	stream = kzalloc(sizeof(*stream), GFP_KERNEL);
	if (!stream) {
		ret = -ENOMEM;
		goto err;
	}

	get_task_comm(name, current);
	format = virtio_video_v4l2_format_to_virtio(default_fmt->desc.format);
	virtio_video_stream_id_get(vvd, stream, &stream_id);
	ret = virtio_video_cmd_stream_create(vvd, stream_id, format, name);
	if (ret) {
		v4l2_err(&vvd->v4l2_dev, "failed to create stream\n");
		goto err_stream_create;
	}

	stream->video_dev = video_dev;
	stream->stream_id = stream_id;

	virtio_video_state_reset(stream);

	ret = virtio_video_stream_get_params(vvd, stream);
	if (ret)
		goto err_stream_create;

	if (format >= VIRTIO_VIDEO_FORMAT_CODED_MIN &&
	    format <= VIRTIO_VIDEO_FORMAT_CODED_MAX) {
		ret = virtio_video_stream_get_controls(vvd, stream);
		if (ret)
			goto err_stream_create;
	}

	mutex_init(&stream->vq_mutex);
	v4l2_fh_init(&stream->fh, video_dev);
	stream->fh.ctrl_handler = &stream->ctrl_handler;

	if (vvd->is_m2m_dev) {
		stream->fh.m2m_ctx = v4l2_m2m_ctx_init(vvd->m2m_dev, stream,
						       vvd->ops->init_queues);
		if (IS_ERR(stream->fh.m2m_ctx)) {
			ret = PTR_ERR(stream->fh.m2m_ctx);
			goto err_init_ctx;
		}

		v4l2_m2m_set_src_buffered(stream->fh.m2m_ctx, true);
		v4l2_m2m_set_dst_buffered(stream->fh.m2m_ctx, true);
	} else {
		vvd->ops->init_queues(stream, NULL, &vvd->vb2_output_queue);
		/* Video dev queue is required for vb2 ioctl wrappers */
		video_dev->queue = &vvd->vb2_output_queue;
	}

	file->private_data = &stream->fh;
	v4l2_fh_add(&stream->fh);

	if (vvd->ops->init_ctrls) {
		ret = vvd->ops->init_ctrls(stream);
		if (ret) {
			v4l2_err(&vvd->v4l2_dev, "failed to init controls\n");
			goto err_init_ctrls;
		}
	}
	return 0;

err_init_ctrls:
	v4l2_fh_del(&stream->fh);
	mutex_lock(video_dev->lock);
	if (vvd->is_m2m_dev)
		v4l2_m2m_ctx_release(stream->fh.m2m_ctx);
	mutex_unlock(video_dev->lock);
err_init_ctx:
	v4l2_fh_exit(&stream->fh);
err_stream_create:
	virtio_video_stream_id_put(vvd, stream_id);
	kfree(stream);
err:
	virtio_video_clear_device_busy(vvd, video_dev->lock);
	return ret;
}

static int virtio_video_device_release(struct file *file)
{
	struct virtio_video_stream *stream = file2stream(file);
	struct video_device *video_dev = video_devdata(file);
	struct virtio_video_device *vvd = video_drvdata(file);

	mutex_lock(video_dev->lock);

	v4l2_fh_del(&stream->fh);
	if (vvd->is_m2m_dev) {
		v4l2_m2m_ctx_release(stream->fh.m2m_ctx);
	} else if (file->private_data == video_dev->queue->owner) {
		vb2_queue_release(&vvd->vb2_output_queue);
		video_dev->queue->owner = NULL;
	}

	v4l2_fh_exit(&stream->fh);

	virtio_video_cmd_stream_destroy(vvd, stream->stream_id);
	virtio_video_stream_id_put(vvd, stream->stream_id);

	kfree(stream);

	/* Mutex already locked here, passing NULL */
	virtio_video_clear_device_busy(vvd, NULL);

	mutex_unlock(video_dev->lock);

	return 0;
}

static const struct v4l2_file_operations virtio_video_device_m2m_fops = {
	.owner		= THIS_MODULE,
	.open		= virtio_video_device_open,
	.release	= virtio_video_device_release,
	.poll		= v4l2_m2m_fop_poll,
	.unlocked_ioctl	= video_ioctl2,
	.mmap		= v4l2_m2m_fop_mmap,
};

static const struct v4l2_file_operations virtio_video_device_fops = {
	.owner		= THIS_MODULE,
	.open		= virtio_video_device_open,
	.release	= virtio_video_device_release,
	.poll		= vb2_fop_poll,
	.unlocked_ioctl	= video_ioctl2,
	.mmap		= vb2_fop_mmap,
};

static void virtio_video_device_run(void *priv)
{

}

static void virtio_video_device_job_abort(void *priv)
{
	struct virtio_video_stream *stream = priv;
	struct virtio_video_device *vvd = to_virtio_vd(stream->video_dev);

	v4l2_m2m_job_finish(vvd->m2m_dev, stream->fh.m2m_ctx);
}

static const struct v4l2_m2m_ops virtio_video_device_m2m_ops = {
	.device_run	= virtio_video_device_run,
	.job_abort	= virtio_video_device_job_abort,
};

static int virtio_video_device_register(struct virtio_video_device *vvd)
{
	int ret;
	struct video_device *vd;

	vd = &vvd->video_dev;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,7,0)
	ret = video_register_device(vd, VFL_TYPE_VIDEO, vvd->vid_dev_nr);
#else
	ret = video_register_device(vd, VFL_TYPE_GRABBER, vvd->vid_dev_nr);
#endif
	if (ret) {
		v4l2_err(&vvd->v4l2_dev, "failed to register video device\n");
		return ret;
	}

	v4l2_info(&vvd->v4l2_dev, "Device '%s' registered as /dev/video%d\n",
		  vd->name, vd->num);

	return 0;
}

static void virtio_video_device_unregister(struct virtio_video_device *vvd)
{
	video_unregister_device(&vvd->video_dev);
}

static int
virtio_video_query_capability(struct virtio_video_device *vvd,
			      void *resp_buf,
			      enum virtio_video_queue_type queue_type)
{
	int ret;
	int resp_size = vvd->max_caps_len;

	ret = virtio_video_cmd_query_capability(vvd, resp_buf, resp_size,
						queue_type);
	if (ret)
		v4l2_err(&vvd->v4l2_dev, "failed to query capability\n");

	return ret;
}

int virtio_video_device_init(struct virtio_video_device *vvd)
{
	int ret;
	int vfl_dir;
	u32 dev_caps;
	struct video_device *vd;
	struct v4l2_m2m_dev *m2m_dev;
	const struct v4l2_file_operations *fops;
	void *input_resp_buf, *output_resp_buf;

	output_resp_buf = kzalloc(vvd->max_caps_len, GFP_KERNEL);
	if (!output_resp_buf)
		return -ENOMEM;

	ret = virtio_video_query_capability(vvd, output_resp_buf,
					    VIRTIO_VIDEO_QUEUE_TYPE_OUTPUT);
	if (ret) {
		v4l2_err(&vvd->v4l2_dev, "failed to get output caps\n");
		goto err_output_cap;
	}

	if (vvd->is_m2m_dev) {
		input_resp_buf = kzalloc(vvd->max_caps_len, GFP_KERNEL);
		if (!input_resp_buf) {
			ret = -ENOMEM;
			goto err_input_buf;
		}

		ret = virtio_video_query_capability(vvd, input_resp_buf,
						VIRTIO_VIDEO_QUEUE_TYPE_INPUT);
		if (ret) {
			v4l2_err(&vvd->v4l2_dev, "failed to get input caps\n");
			goto err_input_cap;
		}

		m2m_dev = v4l2_m2m_init(&virtio_video_device_m2m_ops);
		if (IS_ERR(m2m_dev)) {
			v4l2_err(&vvd->v4l2_dev, "failed to init m2m device\n");
			ret = PTR_ERR(m2m_dev);
			goto err_m2m_dev;
		}
		vfl_dir = VFL_DIR_M2M;
		fops = &virtio_video_device_m2m_fops;
		dev_caps = V4L2_CAP_STREAMING | V4L2_CAP_VIDEO_M2M_MPLANE;
	} else {
		input_resp_buf = NULL;
		m2m_dev = NULL;
		vfl_dir = VFL_DIR_RX;
		fops = &virtio_video_device_fops;
		dev_caps = V4L2_CAP_STREAMING;
		if (vvd->is_mplane_cam)
			dev_caps |= V4L2_CAP_VIDEO_CAPTURE_MPLANE;
		else
			dev_caps |= V4L2_CAP_VIDEO_CAPTURE;
	}

	vvd->m2m_dev = m2m_dev;
	mutex_init(&vvd->video_dev_mutex);
	vd = &vvd->video_dev;
	vd->lock = &vvd->video_dev_mutex;
	vd->v4l2_dev = &vvd->v4l2_dev;
	vd->vfl_dir = vfl_dir;
	vd->ioctl_ops = NULL;
	vd->fops = fops;
	vd->device_caps = dev_caps;
	vd->release = video_device_release_empty;

	/* Use the selection API instead */
	v4l2_disable_ioctl(vd, VIDIOC_CROPCAP);
	v4l2_disable_ioctl(vd, VIDIOC_G_CROP);

	video_set_drvdata(vd, vvd);

	INIT_LIST_HEAD(&vvd->input_fmt_list);
	INIT_LIST_HEAD(&vvd->output_fmt_list);
	INIT_LIST_HEAD(&vvd->controls_fmt_list);
	INIT_LIST_HEAD(&vvd->pending_buf_list);

	vvd->num_output_fmts = 0;
	vvd->num_input_fmts = 0;

	switch (vvd->type) {
	case VIRTIO_VIDEO_DEVICE_CAMERA:
		virtio_video_cam_init(vvd);
		break;
	case VIRTIO_VIDEO_DEVICE_ENCODER:
		virtio_video_enc_init(vvd);
		break;
	case VIRTIO_VIDEO_DEVICE_DECODER:
	default:
		virtio_video_dec_init(vvd);
		break;
	}

	ret = virtio_video_parse_virtio_capabilities(vvd, input_resp_buf,
						     output_resp_buf);
	if (ret) {
		v4l2_err(&vvd->v4l2_dev, "failed to parse a function\n");
		goto parse_cap_err;
	}

	ret = virtio_video_parse_virtio_control(vvd);
	if (ret) {
		v4l2_err(&vvd->v4l2_dev, "failed to query controls\n");
		goto parse_ctrl_err;
	}

	ret = virtio_video_device_register(vvd);
	if (ret) {
		v4l2_err(&vvd->v4l2_dev,
			 "failed to init virtio video device\n");
		goto register_err;
	}

	goto out_cleanup;

register_err:
	virtio_video_clean_control(vvd);
parse_ctrl_err:
	virtio_video_clean_capability(vvd);
parse_cap_err:
	if (vvd->is_m2m_dev)
		v4l2_m2m_release(vvd->m2m_dev);
err_m2m_dev:
err_input_cap:
out_cleanup:
	if (vvd->is_m2m_dev)
		kfree(input_resp_buf);
err_input_buf:
err_output_cap:
	kfree(output_resp_buf);

	return ret;
}

void virtio_video_device_deinit(struct virtio_video_device *vvd)
{
	vvd->commandq.ready = false;
	vvd->eventq.ready = false;

	virtio_video_device_unregister(vvd);
	if (vvd->is_m2m_dev)
		v4l2_m2m_release(vvd->m2m_dev);
	virtio_video_clean_control(vvd);
	virtio_video_clean_capability(vvd);
}
