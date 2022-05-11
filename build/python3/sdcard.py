#!/usr/bin/env python3

#encoding=utf-8
__author__ = 'addy.ke@rock-chips.com'

import traceback
import os
import sys
import getopt
import json
import platform

from utils import *
from config import Config

class Sdcard:
    def __init__(self, root_path): 
        config = Config(root_path)
        self.conf = config.get()

        self.root_path = root_path
        self.out_path = self.conf['out_path']
        self.sddisk_tool = '%s/build/scripts/sddisk.sh' % root_path 

    def sdcard_help(self):
        text = 'Usage: %s sdcard [options]\n' % EDGE_NAME
        text += '\n'
        text += 'Options:\n'
        text += '  -h, --help               Show this help message and exit\n'
        text += '  -n, --new                Create two partitions(one for boot_linux and another for rootfs)\n'
        text += '  -k, --kernel             Flash kernel(source.img, boot_linux.img)\n'
        text += '  -r, --rootfs             Flash rootfs(rootfs.img)\n'
        text += '  -a, --all                Create two partitions and flash all images\n'
        text += '  -d, --dir                Specify the images directory\n'
        text += '\n'
        text += 'e.g.\n'
        text += '  %s sdcard -k\n' % EDGE_NAME
        text += '  %s sdcard -a\n' % EDGE_NAME
        text +='\n'

        EDGE_DBG(text)

    def sdcard_parse_args(self, argv):
        flash_part = ''
        image_path = None
        try:
            options,args = getopt.getopt(argv, 'hnkrad:', ['help', 'new', 'kernel', 'rootfs', 'all', 'dir'])
        except getopt.GetoptError:
            self.sdcard_help()
            sys.exit(1)

        if len(options) == 0:
            self.sdcard_help()
            sys.exit(1)

        for option, param in options:
            if option in ('-h', '--help'):
                self.sdcard_help()
                sys.exit(1)
            if option in ('-d', '--dir'):
                image_path = param
            else:
                if option in ('-a', '--all'):
                    flash_part = 'all'
                if option in ('-n', '--new'):
                    flash_part = 'new'
                if option in ('-k', '--kernel'):
                    flash_part = 'boot_linux'
                if option in ('-r', '--rootfs'):
                    flash_part = 'rootfs'

        return flash_part,image_path

    def flash_part(self, part, image_path):
        kernel_uuid = self.conf['kernel_uuid']
        rootfs_uuid = self.conf['rootfs_uuid']
        cmd = '%s %s %s %s %s' % (self.sddisk_tool, part, image_path, kernel_uuid, rootfs_uuid)
        return edge_cmd(cmd, None)

    def flash(self, argv):
        flash_part,image_path = self.sdcard_parse_args(argv)
        if len(flash_part) == 0:
            sys.exit(1)

        if image_path == None:
            image_path = self.out_path

        if self.flash_part(flash_part, image_path) != 0:
            EDGE_ERR("Sdcard %s failed" % flash_part)
            sys.exit(1)

        EDGE_INFO('Sdcard all successfully!')
        return 0
