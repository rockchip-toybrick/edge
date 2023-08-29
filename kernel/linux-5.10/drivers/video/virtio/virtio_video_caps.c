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

#include <media/v4l2-ioctl.h>
#include <media/videobuf2-dma-sg.h>

#include "virtio_video.h"

static void virtio_video_free_frame_rates(struct video_format_frame *frame)
{
	kfree(frame->frame_rates);
}

static void virtio_video_free_frames(struct video_format *fmt)
{
	size_t idx = 0;

	for (idx = 0; idx < fmt->desc.num_frames; idx++)
		virtio_video_free_frame_rates(&fmt->frames[idx]);
	kfree(fmt->frames);
}

static void virtio_video_free_fmt(struct list_head *fmts_list)
{
	struct video_format *fmt, *tmp;

	list_for_each_entry_safe(fmt, tmp, fmts_list, formats_list_entry) {
		list_del(&fmt->formats_list_entry);
		virtio_video_free_frames(fmt);
		kfree(fmt);
	}
}

static void virtio_video_free_fmts(struct virtio_video_device *vvd)
{
	virtio_video_free_fmt(&vvd->input_fmt_list);
	virtio_video_free_fmt(&vvd->output_fmt_list);
}

static void virtio_video_copy_fmt_range(struct virtio_video_format_range *d_rge,
					struct virtio_video_format_range *s_rge)
{
	d_rge->min = le32_to_cpu(s_rge->min);
	d_rge->max = le32_to_cpu(s_rge->max);
	d_rge->step = le32_to_cpu(s_rge->step);
}

static size_t
virtio_video_parse_virtio_frame_rate(struct virtio_video_device *vvd,
				     struct virtio_video_format_range *f_rate,
				     void *buf)
{
	struct virtio_video_format_range *virtio_frame_rate;

	virtio_frame_rate = buf;
	virtio_video_copy_fmt_range(f_rate, virtio_frame_rate);

	return sizeof(struct virtio_video_format_range);
}

static size_t virtio_video_parse_virtio_frame(struct virtio_video_device *vvd,
					      struct video_format_frame *frm,
					      void *buf)
{
	struct virtio_video_format_frame *virtio_frame;
	struct virtio_video_format_frame *frame = &frm->frame;
	struct virtio_video_format_range *rate;
	size_t idx, offset, extra_size;

	virtio_frame = buf;

	virtio_video_copy_fmt_range(&frame->width, &virtio_frame->width);
	virtio_video_copy_fmt_range(&frame->height, &virtio_frame->height);

	frame->num_rates = le32_to_cpu(virtio_frame->num_rates);
	frm->frame_rates = kcalloc(frame->num_rates,
				   sizeof(struct virtio_video_format_range),
				   GFP_KERNEL);

	offset = sizeof(struct virtio_video_format_frame);
	for (idx = 0; idx < frame->num_rates; idx++) {
		rate = &frm->frame_rates[idx];
		extra_size =
			virtio_video_parse_virtio_frame_rate(vvd, rate,
							     buf + offset);
		if (extra_size == 0) {
			kfree(frm->frame_rates);
			v4l2_err(&vvd->v4l2_dev,
				 "failed to parse frame rate\n");
			return 0;
		}
		offset += extra_size;
	}

	return offset;
}

static size_t virtio_video_parse_virtio_fmt(struct virtio_video_device *vvd,
					    struct video_format *fmt, void *buf)
{
	struct virtio_video_format_desc *virtio_fmt_desc;
	struct virtio_video_format_desc *fmt_desc;
	struct video_format_frame *frame;
	size_t idx, offset, extra_size;

	virtio_fmt_desc = buf;
	fmt_desc = &fmt->desc;

	fmt_desc->format =
		virtio_video_format_to_v4l2
		(le32_to_cpu(virtio_fmt_desc->format));
	fmt_desc->mask = le64_to_cpu(virtio_fmt_desc->mask);
	fmt_desc->planes_layout = le32_to_cpu(virtio_fmt_desc->planes_layout);

	fmt_desc->num_frames = le32_to_cpu(virtio_fmt_desc->num_frames);
	fmt->frames = kcalloc(fmt_desc->num_frames,
			      sizeof(struct video_format_frame),
			      GFP_KERNEL);

	offset = sizeof(struct virtio_video_format_desc);
	for (idx = 0; idx < fmt_desc->num_frames; idx++) {
		frame = &fmt->frames[idx];
		extra_size =
			virtio_video_parse_virtio_frame(vvd, frame,
							buf + offset);
		if (extra_size == 0) {
			kfree(fmt->frames);
			v4l2_err(&vvd->v4l2_dev, "failed to parse frame\n");
			return 0;
		}
		offset += extra_size;
	}

	return offset;
}

int virtio_video_parse_virtio_capability(struct virtio_video_device *vvd,
					 void *resp_buf,
					 struct list_head *ret_fmt_list,
					 uint32_t *ret_num_fmts)
{
	struct virtio_video_query_capability_resp *resp = resp_buf;
	struct video_format *fmt;
	uint32_t fmt_count;
	int fmt_idx;
	size_t offset;
	int ret;

	if (!resp || ret_fmt_list == NULL || ret_num_fmts == NULL) {
		v4l2_err(&vvd->v4l2_dev, "invalid arguments!\n");
		return -EINVAL;
	}

	if (le32_to_cpu(resp->num_descs) <= 0) {
		v4l2_err(&vvd->v4l2_dev, "invalid capability response\n");
		return -EINVAL;
	}

	fmt_count = le32_to_cpu(resp->num_descs);
	offset = sizeof(struct virtio_video_query_capability_resp);

	for (fmt_idx = 0; fmt_idx < fmt_count; fmt_idx++) {
		size_t fmt_size = 0;

		fmt = kzalloc(sizeof(*fmt), GFP_KERNEL);
		if (!fmt) {
			ret = -ENOMEM;
			goto alloc_err;
		}

		fmt_size = virtio_video_parse_virtio_fmt(vvd, fmt,
							 resp_buf + offset);
		if (fmt_size == 0) {
			v4l2_err(&vvd->v4l2_dev, "failed to parse fmt\n");
			ret = -ENOENT;
			goto parse_fmt_err;
		}
		offset += fmt_size;
		list_add(&fmt->formats_list_entry, ret_fmt_list);
	}

	*ret_num_fmts = fmt_count;
	return 0;

parse_fmt_err:
	kfree(fmt);
alloc_err:
	virtio_video_free_fmts(vvd);
	return ret;
}

int virtio_video_parse_virtio_capabilities(struct virtio_video_device *vvd,
					   void *input_buf, void *output_buf)
{
	int ret;

	if (input_buf) {
		ret = virtio_video_parse_virtio_capability(vvd, input_buf,
						&vvd->input_fmt_list,
						&vvd->num_input_fmts);
		if (ret) {
			v4l2_err(&vvd->v4l2_dev,
				 "Failed to parse input capability: %d\n",
				 ret);
			return ret;
		}
	}

	if (output_buf) {
		ret = virtio_video_parse_virtio_capability(vvd, output_buf,
						 &vvd->output_fmt_list,
						 &vvd->num_output_fmts);
		if (ret) {
			v4l2_err(&vvd->v4l2_dev,
				 "Failed to parse output capability: %d\n",
				 ret);
			return ret;
		}
	}

	return 0;
}

void virtio_video_clean_capability(struct virtio_video_device *vvd)
{
	virtio_video_free_fmts(vvd);
}

static void
virtio_video_free_control_fmt_data(struct video_control_fmt_data *data)
{
	kfree(data->entries);
	kfree(data);
}

static void virtio_video_free_control_formats(struct virtio_video_device *vvd)
{
	struct video_control_format *c_fmt, *tmp;

	list_for_each_entry_safe(c_fmt, tmp, &vvd->controls_fmt_list,
				 controls_list_entry) {
		list_del(&c_fmt->controls_list_entry);
		virtio_video_free_control_fmt_data(c_fmt->profile);
		virtio_video_free_control_fmt_data(c_fmt->level);
		kfree(c_fmt);
	}
}

static int virtio_video_parse_control_levels(struct virtio_video_device *vvd,
					     struct video_control_format *fmt)
{
	int idx, ret;
	struct virtio_video_query_control_resp *resp_buf;
	struct virtio_video_query_control_resp_level *l_resp_buf;
	struct video_control_fmt_data *level;
	enum virtio_video_format virtio_format;
	uint32_t *virtio_levels;
	uint32_t num_levels, mask = 0;
	int max = 0, min = UINT_MAX;
	size_t resp_size;

	resp_size = vvd->max_resp_len;

	virtio_format = virtio_video_v4l2_format_to_virtio(fmt->format);

	resp_buf = kzalloc(resp_size, GFP_KERNEL);
	if (IS_ERR(resp_buf)) {
		ret = PTR_ERR(resp_buf);
		goto lvl_err;
	}

	ret = virtio_video_query_control_level(vvd, resp_buf, resp_size,
					       virtio_format);
	if (ret) {
		v4l2_err(&vvd->v4l2_dev, "failed to query level\n");
		goto lvl_err;
	}

	l_resp_buf = (void *)((char *)resp_buf + sizeof(*resp_buf));
	num_levels = le32_to_cpu(l_resp_buf->num);
	if (num_levels == 0)
		goto lvl_err;

	fmt->level = kzalloc(sizeof(*level), GFP_KERNEL);
	if (!fmt->level) {
		ret = -ENOMEM;
		goto lvl_err;
	}

	level = fmt->level;
	level->entries = kcalloc(num_levels, sizeof(uint32_t), GFP_KERNEL);
	if (!level->entries) {
		kfree(fmt->level);
		ret = -ENOMEM;
		goto lvl_err;
	}

	virtio_levels = (void *)((char *)l_resp_buf + sizeof(*l_resp_buf));

	for (idx = 0; idx < num_levels; idx++) {
		level->entries[idx] =
			virtio_video_level_to_v4l2
			(le32_to_cpu(virtio_levels[idx]));

		mask = mask | (1 << level->entries[idx]);
		if (level->entries[idx] > max)
			max = level->entries[idx];
		if (level->entries[idx] < min)
			min = level->entries[idx];
	}
	level->min = min;
	level->max = max;
	level->num = num_levels;
	level->skip_mask = ~mask;

lvl_err:
	kfree(resp_buf);

	return ret;
}

static int virtio_video_parse_control_profiles(struct virtio_video_device *vvd,
					       struct video_control_format *fmt)
{
	int idx, ret;
	struct virtio_video_query_control_resp *resp_buf;
	struct virtio_video_query_control_resp_profile *p_resp_buf;
	struct video_control_fmt_data *profile;
	uint32_t virtio_format, num_profiles, mask = 0;
	uint32_t *virtio_profiles;
	int max = 0, min = UINT_MAX;
	size_t resp_size;

	resp_size = vvd->max_resp_len;
	virtio_format = virtio_video_v4l2_format_to_virtio(fmt->format);
	resp_buf = kzalloc(resp_size, GFP_KERNEL);
	if (IS_ERR(resp_buf)) {
		ret = PTR_ERR(resp_buf);
		goto prf_err;
	}

	ret = virtio_video_query_control_profile(vvd, resp_buf, resp_size,
						 virtio_format);
	if (ret) {
		v4l2_err(&vvd->v4l2_dev, "failed to query profile\n");
		goto prf_err;
	}

	p_resp_buf = (void *)((char *)resp_buf + sizeof(*resp_buf));
	num_profiles = le32_to_cpu(p_resp_buf->num);
	if (num_profiles == 0)
		goto prf_err;

	fmt->profile = kzalloc(sizeof(*profile), GFP_KERNEL);
	if (!fmt->profile) {
		ret = -ENOMEM;
		goto prf_err;
	}

	profile = fmt->profile;
	profile->entries = kcalloc(num_profiles, sizeof(uint32_t), GFP_KERNEL);
	if (!profile->entries) {
		kfree(fmt->profile);
		ret = -ENOMEM;
		goto prf_err;
	}

	virtio_profiles = (void *)((char *)p_resp_buf + sizeof(*p_resp_buf));

	for (idx = 0; idx < num_profiles; idx++) {
		profile->entries[idx] =
			virtio_video_profile_to_v4l2
			(le32_to_cpu(virtio_profiles[idx]));

		mask = mask | (1 << profile->entries[idx]);
		if (profile->entries[idx] > max)
			max = profile->entries[idx];
		if (profile->entries[idx] < min)
			min = profile->entries[idx];
	}
	profile->min = min;
	profile->max = max;
	profile->num = num_profiles;
	profile->skip_mask = ~mask;

prf_err:
	kfree(resp_buf);

	return ret;
}

int virtio_video_parse_virtio_control(struct virtio_video_device *vvd)
{
	struct video_format *fmt;
	struct video_control_format *c_fmt;
	uint32_t virtio_format;
	int ret;

	list_for_each_entry(fmt, &vvd->output_fmt_list, formats_list_entry) {
		virtio_format =
			virtio_video_v4l2_format_to_virtio(fmt->desc.format);
		if (virtio_format < VIRTIO_VIDEO_FORMAT_CODED_MIN ||
		    virtio_format > VIRTIO_VIDEO_FORMAT_CODED_MAX)
			continue;

		c_fmt = kzalloc(sizeof(*c_fmt), GFP_KERNEL);
		if (!c_fmt) {
			ret = -ENOMEM;
			goto parse_ctrl_alloc_err;
		}

		c_fmt->format = fmt->desc.format;

		ret = virtio_video_parse_control_profiles(vvd, c_fmt);
		if (ret) {
			v4l2_err(&vvd->v4l2_dev,
				 "failed to parse control profile\n");
			goto parse_ctrl_prf_err;
		}

		ret = virtio_video_parse_control_levels(vvd, c_fmt);
		if (ret) {
			v4l2_err(&vvd->v4l2_dev,
				 "failed to parse control level\n");
			goto parse_ctrl_lvl_err;
		}
		list_add(&c_fmt->controls_list_entry, &vvd->controls_fmt_list);
	}
	return 0;

parse_ctrl_lvl_err:
	virtio_video_free_control_fmt_data(c_fmt->profile);
parse_ctrl_prf_err:
	kfree(c_fmt);
parse_ctrl_alloc_err:
	virtio_video_free_control_formats(vvd);

	return ret;
}

void virtio_video_clean_control(struct virtio_video_device *vvd)
{
	virtio_video_free_control_formats(vvd);
}
