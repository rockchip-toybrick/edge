// SPDX-License-Identifier: GPL-2.0+
/* Encoder for virtio video device.
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

static int virtio_video_enc_start_streaming(struct vb2_queue *vq,
					    unsigned int count)
{
	struct virtio_video_stream *stream = vb2_get_drv_priv(vq);
	bool input_queue = V4L2_TYPE_IS_OUTPUT(vq->type);

	if (virtio_video_state(stream) == STREAM_STATE_ERROR)
		return -EIO;

	if (virtio_video_state(stream) == STREAM_STATE_INIT ||
	    (!input_queue &&
	     virtio_video_state(stream) == STREAM_STATE_RESET) ||
	    (input_queue &&
	     virtio_video_state(stream) == STREAM_STATE_STOPPED))
		virtio_video_state_update(stream, STREAM_STATE_RUNNING);

	return 0;
}

static void virtio_video_enc_stop_streaming(struct vb2_queue *vq)
{
	int ret, queue_type;
	struct virtio_video_stream *stream = vb2_get_drv_priv(vq);

	if (V4L2_TYPE_IS_OUTPUT(vq->type))
		queue_type = VIRTIO_VIDEO_QUEUE_TYPE_INPUT;
	else
		queue_type = VIRTIO_VIDEO_QUEUE_TYPE_OUTPUT;

	ret = virtio_video_queue_release_buffers(stream, queue_type);
	if (ret)
		return;

	vb2_wait_for_all_buffers(vq);

	if (V4L2_TYPE_IS_OUTPUT(vq->type))
		virtio_video_state_update(stream, STREAM_STATE_STOPPED);
	else
		virtio_video_state_update(stream, STREAM_STATE_RESET);
}

static const struct vb2_ops virtio_video_enc_qops = {
	.queue_setup	 = virtio_video_queue_setup,
	.buf_init	 = virtio_video_buf_init,
	.buf_cleanup	 = virtio_video_buf_cleanup,
	.buf_queue	 = virtio_video_buf_queue,
	.start_streaming = virtio_video_enc_start_streaming,
	.stop_streaming  = virtio_video_enc_stop_streaming,
	.wait_prepare	 = vb2_ops_wait_prepare,
	.wait_finish	 = vb2_ops_wait_finish,
};

static int virtio_video_enc_s_ctrl(struct v4l2_ctrl *ctrl)
{
	int ret = 0;
	struct virtio_video_stream *stream = ctrl2stream(ctrl);
	struct virtio_video_device *vvd = to_virtio_vd(stream->video_dev);
	uint32_t control, value;

	if (virtio_video_state(stream) == STREAM_STATE_ERROR)
		return -EIO;

	control = virtio_video_v4l2_control_to_virtio(ctrl->id);

	switch (ctrl->id) {
	case V4L2_CID_MPEG_VIDEO_BITRATE:
		ret = virtio_video_cmd_set_control(vvd, stream->stream_id,
						   control, ctrl->val);
		break;
	case V4L2_CID_MPEG_VIDEO_H264_LEVEL:
		value = virtio_video_v4l2_level_to_virtio(ctrl->val);
		ret = virtio_video_cmd_set_control(vvd, stream->stream_id,
						   control, value);
		break;
	case V4L2_CID_MPEG_VIDEO_H264_PROFILE:
		value = virtio_video_v4l2_profile_to_virtio(ctrl->val);
		ret = virtio_video_cmd_set_control(vvd, stream->stream_id,
						   control, value);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static int virtio_video_enc_g_ctrl(struct v4l2_ctrl *ctrl)
{
	int ret = 0;
	struct virtio_video_stream *stream = ctrl2stream(ctrl);

	if (virtio_video_state(stream) == STREAM_STATE_ERROR)
		return -EIO;

	switch (ctrl->id) {
	case V4L2_CID_MIN_BUFFERS_FOR_OUTPUT:
		if (virtio_video_state(stream) >= STREAM_STATE_INIT)
			ctrl->val = stream->in_info.min_buffers;
		else
			ctrl->val = 0;
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static const struct v4l2_ctrl_ops virtio_video_enc_ctrl_ops = {
	.g_volatile_ctrl	= virtio_video_enc_g_ctrl,
	.s_ctrl			= virtio_video_enc_s_ctrl,
};

int virtio_video_enc_init_ctrls(struct virtio_video_stream *stream)
{
	struct v4l2_ctrl *ctrl;
	struct virtio_video_device *vvd = to_virtio_vd(stream->video_dev);
	struct video_control_format *c_fmt = NULL;

	v4l2_ctrl_handler_init(&stream->ctrl_handler, 1);

	ctrl = v4l2_ctrl_new_std(&stream->ctrl_handler,
				&virtio_video_enc_ctrl_ops,
				V4L2_CID_MIN_BUFFERS_FOR_OUTPUT,
				MIN_BUFS_MIN, MIN_BUFS_MAX, MIN_BUFS_STEP,
				MIN_BUFS_DEF);

	if (ctrl)
		ctrl->flags |= V4L2_CTRL_FLAG_VOLATILE;

	list_for_each_entry(c_fmt, &vvd->controls_fmt_list,
			    controls_list_entry) {
		switch (c_fmt->format) {
		case V4L2_PIX_FMT_H264:
			if (c_fmt->profile)
				v4l2_ctrl_new_std_menu
					(&stream->ctrl_handler,
					 &virtio_video_enc_ctrl_ops,
					 V4L2_CID_MPEG_VIDEO_H264_PROFILE,
					 c_fmt->profile->max,
					 c_fmt->profile->skip_mask,
					 c_fmt->profile->min);

			if (c_fmt->level)
				v4l2_ctrl_new_std_menu
					(&stream->ctrl_handler,
					 &virtio_video_enc_ctrl_ops,
					 V4L2_CID_MPEG_VIDEO_H264_LEVEL,
					 c_fmt->level->max,
					 c_fmt->level->skip_mask,
					 c_fmt->level->min);
			break;
		default:
			v4l2_dbg(1, vvd->debug,
				 &vvd->v4l2_dev, "unsupported format\n");
			break;
		}
	}

	if (stream->control.bitrate) {
		v4l2_ctrl_new_std(&stream->ctrl_handler,
				  &virtio_video_enc_ctrl_ops,
				  V4L2_CID_MPEG_VIDEO_BITRATE,
				  1, S32_MAX,
				  1, stream->control.bitrate);
	}

	if (stream->ctrl_handler.error)
		return stream->ctrl_handler.error;

	v4l2_ctrl_handler_setup(&stream->ctrl_handler);

	return 0;
}

int virtio_video_enc_init_queues(void *priv, struct vb2_queue *src_vq,
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
	src_vq->ops = &virtio_video_enc_qops;
	src_vq->mem_ops = virtio_video_mem_ops(vvd);
	src_vq->min_buffers_needed = stream->in_info.min_buffers;
	src_vq->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_COPY;
	src_vq->lock = &stream->vq_mutex;
	src_vq->gfp_flags = virtio_video_gfp_flags(vvd);
	src_vq->dev = dev;

	ret = vb2_queue_init(src_vq);
	if (ret)
		return ret;

	dst_vq->type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	dst_vq->io_modes = VB2_MMAP | VB2_DMABUF;
	dst_vq->drv_priv = stream;
	dst_vq->buf_struct_size = sizeof(struct virtio_video_buffer);
	dst_vq->ops = &virtio_video_enc_qops;
	dst_vq->mem_ops = virtio_video_mem_ops(vvd);
	dst_vq->min_buffers_needed = stream->out_info.min_buffers;
	dst_vq->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_COPY;
	dst_vq->lock = &stream->vq_mutex;
	dst_vq->gfp_flags = virtio_video_gfp_flags(vvd);
	dst_vq->dev = dev;

	return vb2_queue_init(dst_vq);
}

static int virtio_video_try_encoder_cmd(struct file *file, void *fh,
					struct v4l2_encoder_cmd *cmd)
{
	struct virtio_video_stream *stream = file2stream(file);
	struct virtio_video_device *vvd = video_drvdata(file);

	if (virtio_video_state(stream) == STREAM_STATE_ERROR)
		return -EIO;

	if (virtio_video_state(stream) == STREAM_STATE_DRAIN)
		return -EBUSY;

	switch (cmd->cmd) {
	case V4L2_ENC_CMD_STOP:
	case V4L2_ENC_CMD_START:
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

static int virtio_video_encoder_cmd(struct file *file, void *fh,
				    struct v4l2_encoder_cmd *cmd)
{
	int ret;
	struct vb2_queue *src_vq, *dst_vq;
	struct virtio_video_stream *stream = file2stream(file);
	struct virtio_video_device *vvd = video_drvdata(file);

	ret = virtio_video_try_encoder_cmd(file, fh, cmd);
	if (ret < 0)
		return ret;

	dst_vq = v4l2_m2m_get_vq(stream->fh.m2m_ctx,
				 V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE);

	switch (cmd->cmd) {
	case V4L2_ENC_CMD_START:
		vb2_clear_last_buffer_dequeued(dst_vq);
		virtio_video_state_update(stream, STREAM_STATE_RUNNING);
		break;
	case V4L2_ENC_CMD_STOP:
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

static int virtio_video_enc_enum_fmt_vid_cap(struct file *file, void *fh,
					     struct v4l2_fmtdesc *f)
{
	struct virtio_video_stream *stream = file2stream(file);
	struct virtio_video_device *vvd = to_virtio_vd(stream->video_dev);
	struct video_format *fmt;
	int idx = 0;

	if (virtio_video_state(stream) == STREAM_STATE_ERROR)
		return -EIO;

	if (f->type != V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE)
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

static int virtio_video_enc_enum_fmt_vid_out(struct file *file, void *fh,
					     struct v4l2_fmtdesc *f)
{
	struct virtio_video_stream *stream = file2stream(file);
	struct virtio_video_device *vvd = to_virtio_vd(stream->video_dev);
	struct video_format_info *info = NULL;
	struct video_format *fmt = NULL;
	unsigned long output_mask = 0;
	int idx = 0, bit_num = 0;

	if (virtio_video_state(stream) == STREAM_STATE_ERROR)
		return -EIO;

	if (f->type != V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE)
		return -EINVAL;

	if (f->index >= vvd->num_input_fmts)
		return -EINVAL;

	info = &stream->out_info;
	list_for_each_entry(fmt, &vvd->output_fmt_list, formats_list_entry) {
		if (info->fourcc_format == fmt->desc.format) {
			output_mask = fmt->desc.mask;
			break;
		}
	}

	if (output_mask == 0)
		return -EINVAL;

	list_for_each_entry(fmt, &vvd->input_fmt_list, formats_list_entry) {
		if (test_bit(bit_num, &output_mask)) {
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

static int virtio_video_enc_s_fmt(struct file *file, void *fh,
				  struct v4l2_format *f)
{
	int ret;
	struct virtio_video_stream *stream = file2stream(file);

	ret = virtio_video_s_fmt(file, fh, f);
	if (ret)
		return ret;

	if (!V4L2_TYPE_IS_OUTPUT(f->type)) {
		if (virtio_video_state(stream) == STREAM_STATE_IDLE)
			virtio_video_state_update(stream, STREAM_STATE_INIT);
	}

	return 0;
}

static int virtio_video_enc_try_framerate(struct virtio_video_stream *stream,
					  unsigned int fps)
{
	int rate_idx;
	struct video_format_frame *frame = NULL;

	if (stream->current_frame == NULL)
		return -EINVAL;

	frame = stream->current_frame;
	for (rate_idx = 0; rate_idx < frame->frame.num_rates; rate_idx++) {
		struct virtio_video_format_range *frame_rate =
			&frame->frame_rates[rate_idx];

		if (within_range(frame_rate->min, fps, frame_rate->max))
			return 0;
	}

	return -EINVAL;
}

static void virtio_video_timeperframe_from_info(struct video_format_info *info,
						struct v4l2_fract *timeperframe)
{
	timeperframe->numerator = 1;
	timeperframe->denominator = info->frame_rate;
}

static int virtio_video_enc_g_parm(struct file *file, void *priv,
				   struct v4l2_streamparm *a)
{
	struct virtio_video_stream *stream = file2stream(file);
	struct virtio_video_device *vvd = to_virtio_vd(stream->video_dev);
	struct v4l2_outputparm *out = &a->parm.output;
	struct v4l2_fract *timeperframe = &out->timeperframe;

	if (virtio_video_state(stream) == STREAM_STATE_ERROR)
		return -EIO;

	if (!V4L2_TYPE_IS_OUTPUT(a->type)) {
		v4l2_err(&vvd->v4l2_dev,
			 "getting FPS is only possible for the output queue\n");
		return -EINVAL;
	}

	out->capability = V4L2_CAP_TIMEPERFRAME;
	virtio_video_timeperframe_from_info(&stream->in_info, timeperframe);

	return 0;
}

static int virtio_video_enc_s_parm(struct file *file, void *priv,
				   struct v4l2_streamparm *a)
{
	int ret;
	u64 frame_interval, frame_rate;
	struct video_format_info info;
	struct virtio_video_stream *stream = file2stream(file);
	struct virtio_video_device *vvd = to_virtio_vd(stream->video_dev);
	struct v4l2_outputparm *out = &a->parm.output;
	struct v4l2_fract *timeperframe = &out->timeperframe;

	if (virtio_video_state(stream) == STREAM_STATE_ERROR)
		return -EIO;

	if (V4L2_TYPE_IS_OUTPUT(a->type)) {
		frame_interval = timeperframe->numerator * (u64)USEC_PER_SEC;
		do_div(frame_interval, timeperframe->denominator);
		if (!frame_interval)
			return -EINVAL;

		frame_rate = (u64)USEC_PER_SEC;
		do_div(frame_rate, frame_interval);
	} else {
		v4l2_err(&vvd->v4l2_dev,
			 "setting FPS is only possible for the output queue\n");
		return -EINVAL;
	}

	ret = virtio_video_enc_try_framerate(stream, frame_rate);
	if (ret)
		return ret;

	virtio_video_format_fill_default_info(&info, &stream->in_info);
	info.frame_rate = frame_rate;

	virtio_video_cmd_set_params(vvd, stream, &info,
				    VIRTIO_VIDEO_QUEUE_TYPE_INPUT);
	virtio_video_stream_get_params(vvd, stream);

	out->capability = V4L2_CAP_TIMEPERFRAME;
	virtio_video_timeperframe_from_info(&stream->in_info, timeperframe);

	return 0;
}

static int virtio_video_enc_s_selection(struct file *file, void *fh,
					struct v4l2_selection *sel)
{
	struct virtio_video_stream *stream = file2stream(file);
	struct virtio_video_device *vvd = to_virtio_vd(stream->video_dev);
	int ret;

	if (!V4L2_TYPE_IS_OUTPUT(sel->type))
		return -EINVAL;

	switch (sel->target) {
	case V4L2_SEL_TGT_CROP:
		stream->in_info.crop.top = sel->r.top;
		stream->in_info.crop.left = sel->r.left;
		stream->in_info.crop.width = sel->r.width;
		stream->in_info.crop.height = sel->r.height;
		break;
	default:
		return -EINVAL;
	}

	ret = virtio_video_cmd_set_params(vvd, stream,  &stream->in_info,
					  VIRTIO_VIDEO_QUEUE_TYPE_INPUT);
	if (ret)
		return -EINVAL;

	return virtio_video_cmd_get_params(vvd, stream,
					   VIRTIO_VIDEO_QUEUE_TYPE_INPUT);
}

static const struct v4l2_ioctl_ops virtio_video_enc_ioctl_ops = {
	.vidioc_querycap	= virtio_video_querycap,

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 3, 0))
	.vidioc_enum_fmt_vid_cap        = virtio_video_enc_enum_fmt_vid_cap,
	.vidioc_enum_fmt_vid_out        = virtio_video_enc_enum_fmt_vid_out,
#else
	.vidioc_enum_fmt_vid_cap_mplane = virtio_video_enc_enum_fmt_vid_cap,
	.vidioc_enum_fmt_vid_out_mplane = virtio_video_enc_enum_fmt_vid_out,
#endif
	.vidioc_g_fmt_vid_cap_mplane	= virtio_video_g_fmt,
	.vidioc_s_fmt_vid_cap_mplane	= virtio_video_enc_s_fmt,

	.vidioc_g_fmt_vid_out_mplane	= virtio_video_g_fmt,
	.vidioc_s_fmt_vid_out_mplane	= virtio_video_enc_s_fmt,

	.vidioc_try_encoder_cmd	= virtio_video_try_encoder_cmd,
	.vidioc_encoder_cmd	= virtio_video_encoder_cmd,
	.vidioc_enum_frameintervals = virtio_video_enum_framemintervals,
	.vidioc_enum_framesizes = virtio_video_enum_framesizes,

	.vidioc_g_selection = virtio_video_g_selection,
	.vidioc_s_selection = virtio_video_enc_s_selection,

	.vidioc_reqbufs		= virtio_video_reqbufs,
	.vidioc_querybuf	= v4l2_m2m_ioctl_querybuf,
	.vidioc_qbuf		= virtio_video_qbuf,
	.vidioc_dqbuf		= virtio_video_dqbuf,
	.vidioc_prepare_buf	= v4l2_m2m_ioctl_prepare_buf,
	.vidioc_create_bufs	= v4l2_m2m_ioctl_create_bufs,
	.vidioc_expbuf		= v4l2_m2m_ioctl_expbuf,

	.vidioc_streamon	= v4l2_m2m_ioctl_streamon,
	.vidioc_streamoff	= v4l2_m2m_ioctl_streamoff,

	.vidioc_s_parm		= virtio_video_enc_s_parm,
	.vidioc_g_parm		= virtio_video_enc_g_parm,

	.vidioc_subscribe_event = virtio_video_subscribe_event,
	.vidioc_unsubscribe_event = v4l2_event_unsubscribe,
};

void *virtio_video_enc_get_fmt_list(struct virtio_video_device *vvd)
{
	return &vvd->output_fmt_list;
}

static struct virtio_video_device_ops virtio_video_enc_ops = {
	.init_ctrls = virtio_video_enc_init_ctrls,
	.init_queues = virtio_video_enc_init_queues,
	.get_fmt_list = virtio_video_enc_get_fmt_list,
};

int virtio_video_enc_init(struct virtio_video_device *vvd)
{
	ssize_t num;
	struct video_device *vd = &vvd->video_dev;

	vd->ioctl_ops = &virtio_video_enc_ioctl_ops;
	vvd->ops = &virtio_video_enc_ops;

	num = strscpy(vd->name, "stateful-encoder", sizeof(vd->name));
	if (num < 0)
		return num;

	return 0;
}
