#!/bin/bash

if [ $# -lt 1 ]; then
	echo "Usage: getsize.sh {PATH}"
	exit 1
fi

if [ ! -d $1 ]; then
	echo "$1 has not exist !"
	exit 1
fi

size=$(du -m -d 0 $1 | awk -F' ' '{print $1}')
echo $size
