// SPDX-License-Identifier: GPL-2.0+
/* Capture for virtio video device.
 *
 * Copyright 2021 OpenSynergy GmbH.
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

#include "virtio_video.h"

static int virtio_video_cam_start_streaming(struct vb2_queue *vq,
					    unsigned int count)
{
	struct virtio_video_stream *stream = vb2_get_drv_priv(vq);

	if (virtio_video_state(stream) == STREAM_STATE_ERROR)
		return -EIO;

	if (virtio_video_state(stream) >= STREAM_STATE_INIT)
		virtio_video_state_update(stream, STREAM_STATE_RUNNING);

	return 0;
}

static void virtio_video_cam_stop_streaming(struct vb2_queue *vq)
{
	struct virtio_video_stream *stream = vb2_get_drv_priv(vq);

	virtio_video_queue_release_buffers(stream,
					   VIRTIO_VIDEO_QUEUE_TYPE_OUTPUT);

	vb2_wait_for_all_buffers(vq);
}

static const struct vb2_ops virtio_video_cam_qops = {
	.queue_setup	 = virtio_video_queue_setup,
	.buf_init	 = virtio_video_buf_init,
	.buf_cleanup	 = virtio_video_buf_cleanup,
	.buf_queue	 = virtio_video_buf_queue,
	.start_streaming = virtio_video_cam_start_streaming,
	.stop_streaming  = virtio_video_cam_stop_streaming,
	.wait_prepare	 = vb2_ops_wait_prepare,
	.wait_finish	 = vb2_ops_wait_finish,
};

static int virtio_video_cam_g_ctrl(struct v4l2_ctrl *ctrl)
{
	int ret = 0;
	struct virtio_video_stream *stream = ctrl2stream(ctrl);

	if (virtio_video_state(stream) == STREAM_STATE_ERROR)
		return -EIO;

	switch (ctrl->id) {
	case V4L2_CID_MIN_BUFFERS_FOR_CAPTURE:
		if (virtio_video_state(stream) >=
		    STREAM_STATE_DYNAMIC_RES_CHANGE)
			ctrl->val = stream->out_info.min_buffers;
		else
			ctrl->val = 0;
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static const struct v4l2_ctrl_ops virtio_video_cam_ctrl_ops = {
	.g_volatile_ctrl = virtio_video_cam_g_ctrl,
};

int virtio_video_cam_init_ctrls(struct virtio_video_stream *stream)
{
	struct v4l2_ctrl *ctrl;

	v4l2_ctrl_handler_init(&stream->ctrl_handler, 2);

	ctrl = v4l2_ctrl_new_std(&stream->ctrl_handler,
				 &virtio_video_cam_ctrl_ops,
				 V4L2_CID_MIN_BUFFERS_FOR_CAPTURE,
				 MIN_BUFS_MIN, MIN_BUFS_MAX, MIN_BUFS_STEP,
				 MIN_BUFS_DEF);

	if (ctrl)
		ctrl->flags |= V4L2_CTRL_FLAG_VOLATILE;

	if (stream->ctrl_handler.error)
		return stream->ctrl_handler.error;

	v4l2_ctrl_handler_setup(&stream->ctrl_handler);

	return 0;
}

int virtio_video_cam_init_queues(void *priv, struct vb2_queue *src_vq,
				 struct vb2_queue *dst_vq)
{
	struct virtio_video_stream *stream = priv;
	struct virtio_video_device *vvd = to_virtio_vd(stream->video_dev);
	struct device *dev = vvd->v4l2_dev.dev;
	int vq_type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;

	if (!vvd->is_mplane_cam)
		vq_type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

	dst_vq->type = vq_type;
	dst_vq->io_modes = VB2_MMAP | VB2_DMABUF | VB2_USERPTR;;
	dst_vq->drv_priv = stream;
	dst_vq->buf_struct_size = sizeof(struct virtio_video_buffer);
	dst_vq->ops = &virtio_video_cam_qops;
	dst_vq->mem_ops = virtio_video_mem_ops(vvd);
	dst_vq->min_buffers_needed = stream->out_info.min_buffers;
	dst_vq->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;
	dst_vq->lock = &stream->vq_mutex;
	dst_vq->gfp_flags = virtio_video_gfp_flags(vvd);
	dst_vq->dev = dev;

	return vb2_queue_init(dst_vq);
}

int virtio_video_cam_try_fmt(struct file *file, void *fh, struct v4l2_format *f)
{
	struct virtio_video_stream *stream = file2stream(file);
	struct virtio_video_device *vvd = to_virtio_vd(stream->video_dev);
	struct v4l2_format *fmt_try = f;
	struct v4l2_format fmt_mp = { 0 };
	int ret;

	if (!vvd->is_mplane_cam) {
		if (f->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
			return -EINVAL;

		fmt_mp.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
		fmt_try = &fmt_mp;

		virtio_video_pix_fmt_sp2mp(&f->fmt.pix, &fmt_try->fmt.pix_mp);
	}

	ret = virtio_video_try_fmt(stream, fmt_try);
	if (ret)
		return ret;

	if (!vvd->is_mplane_cam) {
		if (fmt_try->fmt.pix_mp.num_planes != 1)
			return -EINVAL;

		virtio_video_pix_fmt_mp2sp(&fmt_try->fmt.pix_mp, &f->fmt.pix);
	}

	return 0;
}

static int virtio_video_cam_enum_fmt_vid_cap(struct file *file, void *fh,
					     struct v4l2_fmtdesc *f)
{
	struct virtio_video_stream *stream = file2stream(file);
	struct virtio_video_device *vvd = to_virtio_vd(stream->video_dev);
	struct video_format *fmt;
	int idx = 0;

	if (virtio_video_state(stream) == STREAM_STATE_ERROR)
		return -EIO;

	if (f->type != V4L2_BUF_TYPE_VIDEO_CAPTURE &&
	    f->type != V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE)
		return -EINVAL;

	if (f->index >= vvd->num_output_fmts)
		return -EINVAL;

	list_for_each_entry(fmt, &vvd->output_fmt_list, formats_list_entry) {
		if (f->index == idx) {
			f->pixelformat = fmt->desc.format;
			return 0;
		}
		idx++;
	}
	return -EINVAL;
}

static int virtio_video_cam_g_fmt(struct file *file, void *fh,
				  struct v4l2_format *f)
{
	struct virtio_video_stream *stream = file2stream(file);
	struct virtio_video_device *vvd = to_virtio_vd(stream->video_dev);
	struct v4l2_format fmt_mp = { 0 };
	struct v4l2_format *fmt_get = f;
	int ret;

	if (!vvd->is_mplane_cam) {
		fmt_mp.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
		fmt_get = &fmt_mp;
	}

	ret = virtio_video_g_fmt(file, fh, fmt_get);
	if (ret)
		return ret;

	if (virtio_video_state(stream) == STREAM_STATE_IDLE)
		virtio_video_state_update(stream, STREAM_STATE_INIT);

	if (!vvd->is_mplane_cam) {
		if (fmt_get->fmt.pix_mp.num_planes != 1)
			return -EINVAL;

		virtio_video_pix_fmt_mp2sp(&fmt_get->fmt.pix_mp, &f->fmt.pix);
	}

	return 0;
}

static int virtio_video_cam_s_fmt(struct file *file, void *fh,
				  struct v4l2_format *f)
{
	int ret;
	struct virtio_video_stream *stream = file2stream(file);
	struct virtio_video_device *vvd = to_virtio_vd(stream->video_dev);
	struct v4l2_format fmt_mp = { 0 };
	struct v4l2_format *fmt_set = f;

	if (!vvd->is_mplane_cam) {
		if (f->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
			return -EINVAL;

		fmt_mp.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
		fmt_set = &fmt_mp;

		virtio_video_pix_fmt_sp2mp(&f->fmt.pix, &fmt_set->fmt.pix_mp);
	}

	ret = virtio_video_s_fmt(file, fh, fmt_set);
	if (ret)
		return ret;

	if (virtio_video_state(stream) == STREAM_STATE_IDLE)
		virtio_video_state_update(stream, STREAM_STATE_INIT);

	if (!vvd->is_mplane_cam) {
		if (fmt_set->fmt.pix_mp.num_planes != 1)
			return -EINVAL;

		virtio_video_pix_fmt_mp2sp(&fmt_set->fmt.pix_mp, &f->fmt.pix);
	}

	return 0;
}

static int virtio_video_cam_s_selection(struct file *file, void *fh,
					struct v4l2_selection *sel)
{
	struct virtio_video_stream *stream = file2stream(file);
	struct virtio_video_device *vvd = to_virtio_vd(stream->video_dev);
	int ret;

	if (V4L2_TYPE_IS_OUTPUT(sel->type))
		return -EINVAL;

	switch (sel->target) {
	case V4L2_SEL_TGT_CROP:
		stream->out_info.crop.top = sel->r.top;
		stream->out_info.crop.left = sel->r.left;
		stream->out_info.crop.width = sel->r.width;
		stream->out_info.crop.height = sel->r.height;
		v4l2_info(&vvd->v4l2_dev,
			  "Set : top:%d, left:%d, w:%d, h:%d\n",
			  sel->r.top, sel->r.left, sel->r.width, sel->r.height);
		break;
	default:
		return -EINVAL;
	}

	ret = virtio_video_cmd_set_params(vvd, stream,  &stream->out_info,
					  VIRTIO_VIDEO_QUEUE_TYPE_OUTPUT);
	if (ret)
		return -EINVAL;

	ret = virtio_video_cmd_get_params(vvd, stream,
					  VIRTIO_VIDEO_QUEUE_TYPE_OUTPUT);
	return ret;
}

static const struct v4l2_ioctl_ops virtio_video_cam_ioctl_ops = {
	.vidioc_querycap		= virtio_video_querycap,

#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 3, 0))
	.vidioc_enum_fmt_vid_cap_mplane = virtio_video_cam_enum_fmt_vid_cap,
#endif
	.vidioc_try_fmt_vid_cap_mplane  = virtio_video_cam_try_fmt,
	.vidioc_g_fmt_vid_cap_mplane	= virtio_video_cam_g_fmt,
	.vidioc_s_fmt_vid_cap_mplane	= virtio_video_cam_s_fmt,

	.vidioc_enum_fmt_vid_cap	= virtio_video_cam_enum_fmt_vid_cap,
	.vidioc_try_fmt_vid_cap		= virtio_video_cam_try_fmt,
	.vidioc_g_fmt_vid_cap		= virtio_video_cam_g_fmt,
	.vidioc_s_fmt_vid_cap		= virtio_video_cam_s_fmt,

	.vidioc_g_selection		= virtio_video_g_selection,
	.vidioc_s_selection		= virtio_video_cam_s_selection,

	.vidioc_enum_frameintervals	= virtio_video_enum_framemintervals,
	.vidioc_enum_framesizes		= virtio_video_enum_framesizes,

	.vidioc_reqbufs		= virtio_video_reqbufs,
	.vidioc_querybuf	= vb2_ioctl_querybuf,
	.vidioc_qbuf		= virtio_video_qbuf,
	.vidioc_dqbuf		= virtio_video_dqbuf,
	.vidioc_prepare_buf	= vb2_ioctl_prepare_buf,
	.vidioc_create_bufs	= vb2_ioctl_create_bufs,
	.vidioc_expbuf		= vb2_ioctl_expbuf,

	.vidioc_streamon	= vb2_ioctl_streamon,
	.vidioc_streamoff	= vb2_ioctl_streamoff,

	.vidioc_subscribe_event   = virtio_video_subscribe_event,
	.vidioc_unsubscribe_event = v4l2_event_unsubscribe,
};

void *virtio_video_cam_get_fmt_list(struct virtio_video_device *vvd)
{
	return &vvd->output_fmt_list;
}

static struct virtio_video_device_ops virtio_video_cam_ops = {
	.init_ctrls = virtio_video_cam_init_ctrls,
	.init_queues = virtio_video_cam_init_queues,
	.get_fmt_list = virtio_video_cam_get_fmt_list,
};

int virtio_video_cam_init(struct virtio_video_device *vvd)
{
	ssize_t num;
	struct video_device *vd = &vvd->video_dev;

	vd->ioctl_ops = &virtio_video_cam_ioctl_ops;
	vvd->ops = &virtio_video_cam_ops;

	num = strscpy(vd->name, "camera", sizeof(vd->name));
	if (num < 0)
		return num;

	return 0;
}
