#
# Makefile for the virtio-video driver
#

# --tags to take into account non-annotated tags
# --dirty to mark version with uncommitted changes as dirty
# GIT_VERSION = $(shell git -C "$(MODULE_GIT_REPOSITORY_DIR)" describe --tags --dirty | sed 's/^v//')
GIT_VERSION = android-12.1.0_r0.40-1-g8053c6c

ccflags-y += -DDRIVER_VERSION=\"$(GIT_VERSION)\"

virtio_video-y := \
	virtio_video_driver.o \
	virtio_video_vq.o \
	virtio_video_device.o \
	virtio_video_dec.o \
	virtio_video_enc.o \
	virtio_video_cam.o \
	virtio_video_caps.o \
	virtio_video_helpers.o

obj-$(CONFIG_VIRTIO_VIDEO) += virtio_video.o
