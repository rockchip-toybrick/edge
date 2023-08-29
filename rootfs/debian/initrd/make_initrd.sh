#!/bin/bash

case $1 in
arm | arm64)
	cd $1
	if [ $# -eq 1 ]; then
		find . | cpio -o -H newc | gzip > ../initrd-$1.img
	else
		find . | cpio -o -H newc | xz -9 --format=$2 > ../initrd-$1.img
	fi
	cd -
	;;
*)
	echo "Usage: make_initrd.sh arm|arm64"
	exit 1
	;;
esac
