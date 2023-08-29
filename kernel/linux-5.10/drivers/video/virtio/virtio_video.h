/* SPDX-License-Identifier: GPL-2.0+ */
/* Common header for virtio video driver.
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

#ifndef _VIRTIO_VIDEO_H
#define _VIRTIO_VIDEO_H

#include <linux/virtio.h>
#include <linux/virtio_ids.h>
#include <linux/virtio_config.h>
#include <linux/virtio_video.h>
#include <linux/list.h>
#include <linux/completion.h>
#include <media/v4l2-device.h>
#include <media/v4l2-mem2mem.h>
#include <media/v4l2-ctrls.h>
#include <media/videobuf2-dma-sg.h>
#include <media/videobuf2-dma-contig.h>

#define DRIVER_NAME "virtio-video"

#define VIRTIO_ID_VIDEO_DEC 31
#define VIRTIO_ID_VIDEO_ENC 30
#define VIRTIO_ID_VIDEO_CAM 100
#define VIRTIO_ID_VIDEO_HDMIIN 101

#define MIN_BUFS_MIN 0
#define MIN_BUFS_MAX VIDEO_MAX_FRAME
#define MIN_BUFS_STEP 1
#define MIN_BUFS_DEF 1

struct video_format_frame {
	struct virtio_video_format_frame frame;
	struct virtio_video_format_range *frame_rates;
};

struct video_format {
	struct list_head formats_list_entry;
	struct virtio_video_format_desc desc;
	struct video_format_frame *frames;
};

struct video_control_fmt_data {
	uint32_t min;
	uint32_t max;
	uint32_t num;
	uint32_t skip_mask;
	uint32_t *entries;
};

struct video_control_format {
	struct list_head controls_list_entry;
	uint32_t format;
	struct video_control_fmt_data *profile;
	struct video_control_fmt_data *level;
};

struct video_plane_format {
	uint32_t plane_size;
	uint32_t stride;
};

struct video_format_info {
	uint32_t fourcc_format;
	uint32_t frame_rate;
	uint32_t frame_width;
	uint32_t frame_height;
	uint32_t min_buffers;
	uint32_t max_buffers;
	struct virtio_video_crop crop;
	uint32_t num_planes;
	struct video_plane_format plane_format[VIRTIO_VIDEO_MAX_PLANES];
};

struct video_control_info {
	uint32_t profile;
	uint32_t level;
	uint32_t bitrate;
};

struct virtio_video_device;
struct virtio_video_vbuffer;

typedef void (*virtio_video_resp_cb)(struct virtio_video_device *vvd,
				     struct virtio_video_vbuffer *vbuf);

struct virtio_video_vbuffer {
	char *buf;
	int size;
	uint32_t id;

	void *data_buf;
	uint32_t data_size;

	char *resp_buf;
	int resp_size;

	void *priv;
	virtio_video_resp_cb resp_cb;

	bool is_sync;
	struct completion reclaimed;

	struct list_head pending_list_entry;
};

struct virtio_video_cmd_queue {
	struct virtqueue *vq;
	bool ready;
	spinlock_t qlock;
	wait_queue_head_t reclaim_queue;
};

struct virtio_video_event_queue {
	struct virtqueue *vq;
	bool ready;
	struct work_struct work;
};

enum video_stream_state {
	STREAM_STATE_IDLE = 0,
	STREAM_STATE_INIT,
	STREAM_STATE_DYNAMIC_RES_CHANGE, /* specific to decoder */
	STREAM_STATE_RUNNING,
	STREAM_STATE_DRAIN,
	STREAM_STATE_STOPPED,
	STREAM_STATE_RESET, /* specific to encoder */
	STREAM_STATE_ERROR,
};

struct virtio_video_stream {
	uint32_t stream_id;
	atomic_t state;
	struct video_device *video_dev;
	struct v4l2_fh fh;
	struct mutex vq_mutex;
	struct v4l2_ctrl_handler ctrl_handler;
	struct video_format_info in_info;
	struct video_format_info out_info;
	struct video_control_info control;
	struct video_format_frame *current_frame;
};

struct virtio_video_device {
	struct virtio_device *vdev;
	struct virtio_video_cmd_queue commandq;
	struct virtio_video_event_queue eventq;
	wait_queue_head_t wq;

	struct kmem_cache *vbufs;
	struct virtio_video_event *evts;

	struct idr resource_idr;
	spinlock_t resource_idr_lock;
	struct idr stream_idr;
	spinlock_t stream_idr_lock;

	uint32_t max_caps_len;
	uint32_t max_resp_len;

	bool has_iommu;
	bool supp_non_contig;

	int debug;
	int use_dma_mem;

	struct v4l2_device v4l2_dev;
	struct video_device video_dev;
	struct mutex video_dev_mutex;

	bool is_m2m_dev;
	struct v4l2_m2m_dev *m2m_dev;

	/* non-m2m queue (camera) */
	struct vb2_queue vb2_output_queue;
	struct list_head pending_buf_list;
	spinlock_t pending_buf_list_lock;

	uint32_t vbufs_sent;
	struct list_head pending_vbuf_list;

	/* device_busy - to block multiple opens for non-m2m (camera) */
	bool device_busy;

	/* vid_dev_nr - try register starting at video device number */
	int vid_dev_nr;

	/* is_mplane_cam - camera has multiplanar capabilities (default true) */
	bool is_mplane_cam;

	/* VIRTIO_VIDEO_FUNC_ */
	uint32_t type;

	uint32_t num_input_fmts;
	struct list_head input_fmt_list;

	uint32_t num_output_fmts;
	struct list_head output_fmt_list;

	struct list_head controls_fmt_list;
	struct virtio_video_device_ops *ops;
};

struct virtio_video_device_ops {
	int (*init_ctrls)(struct virtio_video_stream *stream);
	int (*init_queues)(void *priv, struct vb2_queue *src_vq,
			   struct vb2_queue *dst_vq);
	void* (*get_fmt_list)(struct virtio_video_device *vvd);
};

struct virtio_video_buffer {
	struct v4l2_m2m_buffer v4l2_m2m_vb;
	uint32_t resource_id;
	bool queued;
	struct list_head list;
};

static inline gfp_t
virtio_video_gfp_flags(struct virtio_video_device *vvd)
{
	if (vvd->use_dma_mem)
		return GFP_DMA;
	else
		return 0;
}

static inline const struct vb2_mem_ops *
virtio_video_mem_ops(struct virtio_video_device *vvd)
{
	if (vvd->supp_non_contig)
		return &vb2_dma_sg_memops;
	else
		return &vb2_dma_contig_memops;
}

static inline struct virtio_video_device *
to_virtio_vd(struct video_device *video_dev)
{
	return container_of(video_dev, struct virtio_video_device,
			 video_dev);
}

static inline struct virtio_video_stream *file2stream(struct file *file)
{
	return container_of(file->private_data, struct virtio_video_stream, fh);
}

static inline struct virtio_video_stream *ctrl2stream(struct v4l2_ctrl *ctrl)
{
	return container_of(ctrl->handler, struct virtio_video_stream,
			    ctrl_handler);
}

static inline struct virtio_video_buffer *to_virtio_vb(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *v4l2_vb = to_vb2_v4l2_buffer(vb);

	return container_of(v4l2_vb, struct virtio_video_buffer,
			    v4l2_m2m_vb.vb);
}

static inline enum virtio_video_queue_type
to_virtio_queue_type(enum v4l2_buf_type type)
{
	if (V4L2_TYPE_IS_OUTPUT(type))
		return VIRTIO_VIDEO_QUEUE_TYPE_INPUT;
	else
		return VIRTIO_VIDEO_QUEUE_TYPE_OUTPUT;
}

static inline bool within_range(uint32_t min, uint32_t val, uint32_t max)
{
	return ((min <= val) && (val <= max));
}

static inline bool needs_alignment(uint32_t val, uint32_t a)
{
	if (a == 0 || IS_ALIGNED(val, a))
		return false;

	return true;
}

enum video_stream_state virtio_video_state(struct virtio_video_stream *stream);
void virtio_video_state_reset(struct virtio_video_stream *stream);
void virtio_video_state_update(struct virtio_video_stream *stream,
			       enum video_stream_state new_state);

int virtio_video_alloc_vbufs(struct virtio_video_device *vvd);
void virtio_video_free_vbufs(struct virtio_video_device *vvd);
int virtio_video_alloc_events(struct virtio_video_device *vvd);

int virtio_video_device_init(struct virtio_video_device *vvd);
void virtio_video_device_deinit(struct virtio_video_device *vvd);

int virtio_video_dec_init(struct virtio_video_device *vvd);
int virtio_video_enc_init(struct virtio_video_device *vvd);
int virtio_video_cam_init(struct virtio_video_device *vvd);

void virtio_video_stream_id_get(struct virtio_video_device *vvd,
				struct virtio_video_stream *stream,
				uint32_t *id);
void virtio_video_stream_id_put(struct virtio_video_device *vvd, uint32_t id);
void virtio_video_resource_id_get(struct virtio_video_device *vvd,
				  uint32_t *id);
void virtio_video_resource_id_put(struct virtio_video_device *vvd, uint32_t id);

int virtio_video_cmd_stream_create(struct virtio_video_device *vvd,
				   uint32_t stream_id,
				   enum virtio_video_format format,
				   const char *tag);
int virtio_video_cmd_stream_destroy(struct virtio_video_device *vvd,
				    uint32_t stream_id);
int virtio_video_cmd_stream_drain(struct virtio_video_device *vvd,
				  uint32_t stream_id);
int virtio_video_cmd_resource_attach(struct virtio_video_device *vvd,
				     uint32_t stream_id, uint32_t resource_id,
				     enum virtio_video_queue_type queue_type,
				     void *buf, size_t buf_size);
int virtio_video_cmd_resource_queue(struct virtio_video_device *vvd,
				    uint32_t stream_id,
				    struct virtio_video_buffer *virtio_vb,
				    uint32_t data_size[], uint8_t num_data_size,
				    enum virtio_video_queue_type queue_type);
int virtio_video_cmd_queue_detach_resources(struct virtio_video_device *vvd,
				struct virtio_video_stream *stream,
				enum virtio_video_queue_type queue_type);
int virtio_video_cmd_queue_clear(struct virtio_video_device *vvd,
				 struct virtio_video_stream *stream,
				 enum virtio_video_queue_type queue_type);
int virtio_video_cmd_query_capability(struct virtio_video_device *vvd,
				      void *resp_buf, size_t resp_size,
				      enum virtio_video_queue_type queue_type);
int virtio_video_query_control_profile(struct virtio_video_device *vvd,
				       void *resp_buf, size_t resp_size,
				       enum virtio_video_format format);
int virtio_video_query_control_level(struct virtio_video_device *vvd,
				     void *resp_buf, size_t resp_size,
				     enum virtio_video_format format);
int virtio_video_cmd_set_params(struct virtio_video_device *vvd,
				struct virtio_video_stream *stream,
				struct video_format_info *format_info,
				enum virtio_video_queue_type queue_type);
int virtio_video_cmd_get_params(struct virtio_video_device *vvd,
				struct virtio_video_stream *stream,
				enum virtio_video_queue_type queue_type);
int virtio_video_cmd_set_control(struct virtio_video_device *vvd,
				 uint32_t stream_id,
				 enum virtio_video_control_type control,
				 uint32_t value);
int virtio_video_cmd_get_control(struct virtio_video_device *vvd,
				 struct virtio_video_stream *stream,
				 enum virtio_video_control_type control);

void virtio_video_queue_res_chg_event(struct virtio_video_stream *stream);
void virtio_video_queue_eos_event(struct virtio_video_stream *stream);
void virtio_video_handle_error(struct virtio_video_stream *stream);
int virtio_video_queue_release_buffers(struct virtio_video_stream *stream,
				       enum virtio_video_queue_type queue_type);

void virtio_video_cmd_cb(struct virtqueue *vq);
void virtio_video_event_cb(struct virtqueue *vq);
void virtio_video_process_events(struct work_struct *work);

void virtio_video_buf_done(struct virtio_video_buffer *virtio_vb,
			   uint32_t flags, uint64_t timestamp,
			   uint32_t data_sizes[]);
int virtio_video_buf_plane_init(uint32_t idx,uint32_t resource_id,
				struct virtio_video_device *vvd,
				struct virtio_video_stream *stream,
				struct vb2_buffer *vb);
int virtio_video_queue_setup(struct vb2_queue *vq, unsigned int *num_buffers,
			     unsigned int *num_planes, unsigned int sizes[],
			     struct device *alloc_devs[]);
int virtio_video_buf_init(struct vb2_buffer *vb);
void virtio_video_buf_cleanup(struct vb2_buffer *vb);
void virtio_video_buf_queue(struct vb2_buffer *vb);
int virtio_video_qbuf(struct file *file, void *priv,
		      struct v4l2_buffer *buf);
int virtio_video_dqbuf(struct file *file, void *priv,
		       struct v4l2_buffer *buf);
int virtio_video_querycap(struct file *file, void *fh,
			  struct v4l2_capability *cap);
int virtio_video_enum_framesizes(struct file *file, void *fh,
				 struct v4l2_frmsizeenum *f);
int virtio_video_enum_framemintervals(struct file *file, void *fh,
				      struct v4l2_frmivalenum *f);
int virtio_video_g_fmt(struct file *file, void *fh, struct v4l2_format *f);
int virtio_video_s_fmt(struct file *file, void *fh, struct v4l2_format *f);
int virtio_video_try_fmt(struct virtio_video_stream *stream,
			 struct v4l2_format *f);
int virtio_video_reqbufs(struct file *file, void *priv,
                        struct v4l2_requestbuffers *rb);
int virtio_video_subscribe_event(struct v4l2_fh *fh,
				 const struct v4l2_event_subscription *sub);

void virtio_video_free_caps_list(struct list_head *caps_list);
int virtio_video_parse_virtio_capabilities(struct virtio_video_device *vvd,
					   void *input_buf, void *output_buf);
void virtio_video_clean_capability(struct virtio_video_device *vvd);
int virtio_video_parse_virtio_control(struct virtio_video_device *vvd);
void virtio_video_clean_control(struct virtio_video_device *vvd);

uint32_t virtio_video_format_to_v4l2(uint32_t format);
uint32_t virtio_video_control_to_v4l2(uint32_t control);
uint32_t virtio_video_profile_to_v4l2(uint32_t profile);
uint32_t virtio_video_level_to_v4l2(uint32_t level);
uint32_t virtio_video_v4l2_format_to_virtio(uint32_t v4l2_format);
uint32_t virtio_video_v4l2_control_to_virtio(uint32_t v4l2_control);
uint32_t virtio_video_v4l2_profile_to_virtio(uint32_t v4l2_profile);
uint32_t virtio_video_v4l2_level_to_virtio(uint32_t v4l2_level);

struct video_format *virtio_video_find_video_format(struct list_head *fmts_list,
						    uint32_t fourcc);
void virtio_video_format_from_info(struct video_format_info *info,
				   struct v4l2_pix_format_mplane *pix_mp);
void virtio_video_format_fill_default_info(struct video_format_info *dst_info,
                                          struct video_format_info *src_info);
void virtio_video_pix_fmt_sp2mp(const struct v4l2_pix_format *pix,
				struct v4l2_pix_format_mplane *pix_mp);
void virtio_video_pix_fmt_mp2sp(const struct v4l2_pix_format_mplane *pix_mp,
				struct v4l2_pix_format *pix);

int virtio_video_g_selection(struct file *file, void *fh,
			     struct v4l2_selection *sel);

int virtio_video_stream_get_params(struct virtio_video_device *vvd,
				   struct virtio_video_stream *stream);
int virtio_video_stream_get_controls(struct virtio_video_device *vvd,
				     struct virtio_video_stream *stream);

#endif /* _VIRTIO_VIDEO_H */
