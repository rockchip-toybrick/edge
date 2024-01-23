#!/bin/bash

ROOTFS=rootfs_toybrick_v1.1.0_20240115.tar.gz

wget http://repo.rock-chips.com/rootfs/${ROOTFS}

tar zxvf ${ROOTFS}
