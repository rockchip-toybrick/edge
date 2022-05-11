#!/bin/bash

function help()
{
	echo "Usage: resize.sh {IMAGE} {SIZE}"
	echo "e.g."
	echo "  resize.sh rootfs.img 500M"
	echo "  resize.sh rootfs.img auto"
	exit 1
}

function resize_rootfs()
{
	image_path=$1
	size=$2

	ret=1
	for i in $(seq 10)
	do
		sudo e2fsck -fy ${image_path}
		if [ $? -eq 0 ]; then
			ret=0
			break
		fi
		sleep 1
	done
	if [ ${ret} -ne 0 ]; then
		echo "e2fsck ${image_path} failed"
		return 2
	fi

	echo "Resize ${image_path} to ${size} ..."	
	sudo resize2fs -f ${image_path} ${size}
	if [ $? -ne 0 ]; then
		echo "resize2fs ${image_path} failed"
		return 1
	fi

	return 0
}

if [ $# -ne 2 ]; then
	help
	exit 1
fi

if [ ! -f $1 ]; then
	echo "Image(${image_path}) does NOT exist"
	exit 1
fi

case $2 in
auto)
	size=$(du -m $1 | awk -F' ' '{print $1}')
	size="$(expr ${size} + 2)M"
	;;
*)
	size=$2
	;;
esac
resize_rootfs $1 ${size}
