// SPDX-License-Identifier: GPL-2.0+
/* Decoder for virtio video device.
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

#include "virtio_video.h"

static int virtio_video_dec_start_streaming(struct vb2_queue *vq,
					    unsigned int count)
{
	struct virtio_video_stream *stream = vb2_get_drv_priv(vq);

	if (virtio_video_state(stream) == STREAM_STATE_ERROR)
		return -EIO;

	if (!V4L2_TYPE_IS_OUTPUT(vq->type) &&
	    virtio_video_state(stream) >= STREAM_STATE_INIT)
		virtio_video_state_update(stream, STREAM_STATE_RUNNING);

	return 0;
}

static void virtio_video_dec_stop_streaming(struct vb2_queue *vq)
{
	int queue_type;
	struct virtio_video_stream *stream = vb2_get_drv_priv(vq);

	if (V4L2_TYPE_IS_OUTPUT(vq->type))
		queue_type = VIRTIO_VIDEO_QUEUE_TYPE_INPUT;
	else
		queue_type = VIRTIO_VIDEO_QUEUE_TYPE_OUTPUT;

	virtio_video_queue_release_buffers(stream, queue_type);
	vb2_wait_for_all_buffers(vq);
}

static const struct vb2_ops virtio_video_dec_qops = {
	.queue_setup	 = virtio_video_queue_setup,
	.buf_init	 = virtio_video_buf_init,
	.buf_cleanup	 = virtio_video_buf_cleanup,
	.buf_queue	 = virtio_video_buf_queue,
	.start_streaming = virtio_video_dec_start_streaming,
	.stop_streaming  = virtio_video_dec_stop_streaming,
	.wait_prepare	 = vb2_ops_wait_prepare,
	.wait_finish	 = vb2_ops_wait_finish,
};

static int virtio_video_dec_g_ctrl(struct v4l2_ctrl *ctrl)
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

static const struct v4l2_ctrl_ops virtio_video_dec_ctrl_ops = {
	.g_volatile_ctrl	= virtio_video_dec_g_ctrl,
};

int virtio_video_dec_init_ctrls(struct virtio_video_stream *stream)
{
	struct v4l2_ctrl *ctrl;

	v4l2_ctrl_handler_init(&stream->ctrl_handler, 2);

	ctrl = v4l2_ctrl_new_std(&stream->ctrl_handler,
				&virtio_video_dec_ctrl_ops,
				V4L2_CID_MIN_BUFFERS_FOR_CAPTURE,
				MIN_BUFS_MIN, MIN_BUFS_MAX, MIN_BUFS_STEP,
				MIN_BUFS_DEF);

	if (ctrl)
		ctrl->flags |= V4L2_CTRL_FLAG_VOLATILE;

	if (stream->ctrl_handler.error)
		return stream->ctrl_handler.error;

	(void)v4l2_ctrl_new_std(&stream->ctrl_handler, NULL,
				V4L2_CID_MIN_BUFFERS_FOR_OUTPUT,
				MIN_BUFS_MIN, MIN_BUFS_MAX, MIN_BUFS_STEP,
				stream->in_info.min_buffers);

	if (stream->ctrl_handler.error)
		return stream->ctrl_handler.error;

	v4l2_ctrl_handler_setup(&stream->ctrl_handler);

	return 0;
}

int virtio_video_dec_init_queues(void *priv, struct vb2_queue *src_vq,
				 struct vb2_queue *dst_vq)
{
	int ret;
	struct virtio_video_stream *stream = priv;
	struct virtio_video_device *vvd = to_virtio_vd(stream->video_dev);
	struct device *dev = vvd->v4l2_dev.dev;

	src_vq->type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	src_vq->io_modes = VB2_MMAP | VB2_DMABUF;
	src_vq->drv_priv = stream;
	src_vq->buf_struct_size = sizeof(struct virtio_video_buffer);
	src_vq->ops = &virtio_video_dec_qops;
	src_vq->mem_ops = virtio_video_mem_ops(vvd);
	src_vq->min_buffers_needed = stream->in_info.min_buffers;
	src_vq->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_COPY;
	src_vq->lock = &stream->vq_mutex;
	src_vq->gfp_flags = virtio_video_gfp_flags(vvd);
	src_vq->dev = dev;
	src_vq->allow_zero_bytesused = 1; // On QNX, allow size 0 EOS indication.

	ret = vb2_queue_init(src_vq);
	if (ret)
		return ret;

	dst_vq->type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	dst_vq->io_modes = VB2_MMAP | VB2_DMABUF | VB2_USERPTR;;
	dst_vq->drv_priv = stream;
	dst_vq->buf_struct_size = sizeof(struct virtio_video_buffer);
	dst_vq->ops = &virtio_video_dec_qops;
	dst_vq->mem_ops = virtio_video_mem_ops(vvd);
	dst_vq->min_buffers_needed = stream->out_info.min_buffers;
	// On QNX we override this to 1 for a slight improvement in latency when
	// video playback first starts, or resumes after a seek. Right now the value
	// configured in the vdev for out_info.min_buffers is 9 output buffers. That
	// plus the 4 "extra" output buffers gives us 13 in total. We don't need to
	// set min_buffers_needed to 9 for this queue. A value of 1 is perfectly
	// fine and will result in slightly lower latency. The min_buffers_needed
	// value is used in vb2_core_qbuf(). It waits to call vb2_start_streaming()
	// until this many output buffers have been received. There is no reason
	// to wait though. Waiting just increases latency. See kernel source file
	// videobuf2-core.c for the vb2_core_qbuf() function and min_buffers_needed
	// check before vb2_start_streaming() gets called to start sending output
	// buffers to the vdev to be filled. We don't need to do anything for
	// input buffers (above) because min_buffers_needed already ends up being 1.
	dst_vq->min_buffers_needed = 1;
	dst_vq->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_COPY;
	dst_vq->lock = &stream->vq_mutex;
	dst_vq->gfp_flags = virtio_video_gfp_flags(vvd);
	dst_vq->dev = dev;

	return vb2_queue_init(dst_vq);
}

static int virtio_video_try_decoder_cmd(struct file *file, void *fh,
					struct v4l2_decoder_cmd *cmd)
{
	struct virtio_video_stream *stream = file2stream(file);
	struct virtio_video_device *vvd = video_drvdata(file);

	if (virtio_video_state(stream) == STREAM_STATE_ERROR)
		return -EIO;

	if (virtio_video_state(stream) == STREAM_STATE_DRAIN)
		return -EBUSY;

	switch (cmd->cmd) {
	case V4L2_DEC_CMD_STOP:
	case V4L2_DEC_CMD_START:
		if (cmd->flags != 0) {
			v4l2_err(&vvd->v4l2_dev, "flags=%u are not supported",
				 cmd->flags);
			return -EINVAL;
		}
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int virtio_video_decoder_cmd(struct file *file, void *fh,
				    struct v4l2_decoder_cmd *cmd)
{
	int ret;
	struct vb2_queue *src_vq, *dst_vq;
	struct virtio_video_stream *stream = file2stream(file);
	struct virtio_video_device *vvd = video_drvdata(file);

	ret = virtio_video_try_decoder_cmd(file, fh, cmd);
	if (ret < 0)
		return ret;

	dst_vq = v4l2_m2m_get_vq(stream->fh.m2m_ctx,
				 V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE);

	switch (cmd->cmd) {
	case V4L2_DEC_CMD_START:
		vb2_clear_last_buffer_dequeued(dst_vq);
		break;
	case V4L2_DEC_CMD_STOP:
		src_vq = v4l2_m2m_get_vq(stream->fh.m2m_ctx,
					 V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE);

		if (!vb2_is_streaming(src_vq)) {
			v4l2_dbg(1, vvd->debug,
				 &vvd->v4l2_dev, "output is not streaming\n");
			return 0;
		}

		if (!vb2_is_streaming(dst_vq)) {
			v4l2_dbg(1, vvd->debug,
				 &vvd->v4l2_dev, "capture is not streaming\n");
			return 0;
		}

		ret = virtio_video_cmd_stream_drain(vvd, stream->stream_id);
		if (ret) {
			v4l2_err(&vvd->v4l2_dev, "failed to drain stream\n");
			return ret;
		}

		virtio_video_state_update(stream, STREAM_STATE_DRAIN);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int virtio_video_dec_enum_fmt_vid_cap(struct file *file, void *fh,
					     struct v4l2_fmtdesc *f)
{
	struct virtio_video_stream *stream = file2stream(file);
	struct virtio_video_device *vvd = to_virtio_vd(stream->video_dev);
	struct video_format_info *info;
	struct video_format *fmt;
	unsigned long input_mask = 0;
	int idx = 0, bit_num = 0;

	if (virtio_video_state(stream) == STREAM_STATE_ERROR)
		return -EIO;

	if (f->type != V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE)
		return -EINVAL;

	if (f->index >= vvd->num_output_fmts)
		return -EINVAL;

	info = &stream->in_info;
	list_for_each_entry(fmt, &vvd->input_fmt_list, formats_list_entry) {
		if (info->fourcc_format == fmt->desc.format) {
			input_mask = fmt->desc.mask;
			break;
		}
	}

	if (input_mask == 0)
		return -EINVAL;

	list_for_each_entry(fmt, &vvd->output_fmt_list, formats_list_entry) {
		if (test_bit(bit_num, &input_mask)) {
			if (f->index == idx) {
				f->pixelformat = fmt->desc.format;
				return 0;
			}
			idx++;
		}
		bit_num++;
	}
	return -EINVAL;
}


int virtio_video_dec_enum_fmt_vid_out(struct file *file, void *fh,
				      struct v4l2_fmtdesc *f)
{
	struct virtio_video_stream *stream = file2stream(file);
	struct virtio_video_device *vvd = to_virtio_vd(stream->video_dev);
	struct video_format *fmt;
	int idx = 0;

	if (virtio_video_state(stream) == STREAM_STATE_ERROR)
		return -EIO;

	if (f->type != V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE)
		return -EINVAL;

	if (f->index >= vvd->num_input_fmts)
		return -EINVAL;

	list_for_each_entry(fmt, &vvd->input_fmt_list, formats_list_entry) {
		if (f->index == idx) {
			f->pixelformat = fmt->desc.format;
			return 0;
		}
		idx++;
	}
	return -EINVAL;
}

static int virtio_video_dec_s_fmt(struct file *file, void *fh,
				  struct v4l2_format *f)
{
	int ret;
	struct virtio_video_stream *stream = file2stream(file);

	ret = virtio_video_s_fmt(file, fh, f);
	if (ret)
		return ret;

	if (V4L2_TYPE_IS_OUTPUT(f->type)) {
		if (virtio_video_state(stream) == STREAM_STATE_IDLE)
			virtio_video_state_update(stream, STREAM_STATE_INIT);
	}

	return 0;
}

static int virtio_video_dec_s_selection(struct file *file, void *fh,
					struct v4l2_selection *sel)
{
	struct virtio_video_stream *stream = file2stream(file);
	struct virtio_video_device *vvd = to_virtio_vd(stream->video_dev);
	int ret;

	if (V4L2_TYPE_IS_OUTPUT(sel->type))
		return -EINVAL;

	switch (sel->target) {
	case V4L2_SEL_TGT_COMPOSE:
		stream->out_info.crop.top = sel->r.top;
		stream->out_info.crop.left = sel->r.left;
		stream->out_info.crop.width = sel->r.width;
		stream->out_info.crop.height = sel->r.height;
		break;
	default:
		return -EINVAL;
	}

	ret = virtio_video_cmd_set_params(vvd, stream,  &stream->out_info,
					   VIRTIO_VIDEO_QUEUE_TYPE_OUTPUT);
	if (ret)
		return -EINVAL;

	return virtio_video_cmd_get_params(vvd, stream,
					   VIRTIO_VIDEO_QUEUE_TYPE_OUTPUT);
}

static const struct v4l2_ioctl_ops virtio_video_dec_ioctl_ops = {
	.vidioc_querycap	= virtio_video_querycap,

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 3, 0))
	.vidioc_enum_fmt_vid_cap        = virtio_video_dec_enum_fmt_vid_cap,
	.vidioc_enum_fmt_vid_out        = virtio_video_dec_enum_fmt_vid_out,
#else
	.vidioc_enum_fmt_vid_cap_mplane = virtio_video_dec_enum_fmt_vid_cap,
	.vidioc_enum_fmt_vid_out_mplane = virtio_video_dec_enum_fmt_vid_out,
#endif
	.vidioc_g_fmt_vid_cap_mplane	= virtio_video_g_fmt,
	.vidioc_s_fmt_vid_cap_mplane	= virtio_video_dec_s_fmt,

	.vidioc_g_fmt_vid_out_mplane	= virtio_video_g_fmt,
	.vidioc_s_fmt_vid_out_mplane	= virtio_video_dec_s_fmt,

	.vidioc_g_selection = virtio_video_g_selection,
	.vidioc_s_selection = virtio_video_dec_s_selection,

	.vidioc_try_decoder_cmd	= virtio_video_try_decoder_cmd,
	.vidioc_decoder_cmd	= virtio_video_decoder_cmd,
	.vidioc_enum_frameintervals = virtio_video_enum_framemintervals,
	.vidioc_enum_framesizes = virtio_video_enum_framesizes,

	.vidioc_reqbufs		= virtio_video_reqbufs,
	.vidioc_querybuf	= v4l2_m2m_ioctl_querybuf,
	.vidioc_qbuf		= virtio_video_qbuf,
	.vidioc_dqbuf		= virtio_video_dqbuf,
	.vidioc_prepare_buf	= v4l2_m2m_ioctl_prepare_buf,
	.vidioc_create_bufs	= v4l2_m2m_ioctl_create_bufs,
	.vidioc_expbuf		= v4l2_m2m_ioctl_expbuf,

	.vidioc_streamon	= v4l2_m2m_ioctl_streamon,
	.vidioc_streamoff	= v4l2_m2m_ioctl_streamoff,

	.vidioc_subscribe_event = virtio_video_subscribe_event,
	.vidioc_unsubscribe_event = v4l2_event_unsubscribe,
};

void *virtio_video_dec_get_fmt_list(struct virtio_video_device *vvd)
{
	return &vvd->input_fmt_list;
}

static struct virtio_video_device_ops virtio_video_dec_ops = {
	.init_ctrls = virtio_video_dec_init_ctrls,
	.init_queues = virtio_video_dec_init_queues,
	.get_fmt_list = virtio_video_dec_get_fmt_list,
};

int virtio_video_dec_init(struct virtio_video_device *vvd)
{
	ssize_t num;
	struct video_device *vd = &vvd->video_dev;

	vd->ioctl_ops = &virtio_video_dec_ioctl_ops;
	vvd->ops = &virtio_video_dec_ops;

	num = strscpy(vd->name, "stateful-decoder", sizeof(vd->name));
	if (num < 0)
		return num;

	return 0;
}
