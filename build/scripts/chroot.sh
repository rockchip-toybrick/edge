#!/bin/bash

function help()
{
	echo "Usage: chroot.sh {out_path} {in|out}"
}

if [ $# -lt 2 ]; then
	help
	exit 1
fi

if [ ! -d $1 ]; then
	echo "Output dirctory(%s) does NOT exist!"
	exit 1

fi

rootfs_path=$1/rootfs
rootfs_image=$1/rootfs.img
mkdir -p ${rootfs_path}

case $2 in
in)
	sudo mount ${rootfs_image} ${rootfs_path}
	sudo mount -t proc /proc ${rootfs_path}/proc/
	sudo mount -t sysfs /sys ${rootfs_path}/sys/
	sudo mount --rbind /dev ${rootfs_path}/dev/
	#sudo mount --bind /dev ${rootfs_path}/dev/
	sudo mount --bind /etc/resolv.conf ${rootfs_path}/etc/resolv.conf
	sudo chroot ${rootfs_path}
	;;
out)
	sudo umount ${rootfs_path}/proc
	sudo umount ${rootfs_path}/sys
	#sudo umount ${rootfs_path}/dev
	sudo mount --make-rslave ${rootfs_path}/dev
	sudo umount -R ${rootfs_path}/dev
	sudo umount ${rootfs_path}/etc/resolv.conf
	sudo umount ${rootfs_path}
	;;
*)
	help
	exit 1
	;;
esac
