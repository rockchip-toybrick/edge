#!/bin/bash

for p in $(ps -aux |grep qemu-aarch64-static | grep -v grep | awk -F' ' '{print $2}')
do
	kill -9 $p
done
