#!/bin/bash

function pre_install()
{
	sudo mount -t proc /proc rootfs/proc/
	sudo mount -t sysfs /sys rootfs/sys/
	sudo mount --rbind /dev rootfs/dev/
	sudo mount --bind /etc/resolv.conf rootfs/etc/resolv.conf
}

function post_install()
{
	sudo umount rootfs/proc
	sudo umount rootfs/sys
	sudo mount --make-rslave rootfs/dev
	sudo umount -R rootfs/dev
	sudo umount rootfs/etc/resolv.conf
}

pre_install
sudo chroot rootfs /pre-install/install.sh $*
ret=$?
sleep 1
post_install
exit ${ret}
