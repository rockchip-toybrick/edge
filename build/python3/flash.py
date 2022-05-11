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

def edge_query_ret(cmd):
    text = edge_cmd_result(cmd)

    if text.find('Mode=Loader') >= 0:
        return 'loader'
    elif text.find('Mode=Maskrom') >= 0:
        return 'maskrom'
    else:
        return 'none'

class Flash:
    def __init__(self, root_path): 
        host_arch = platform.machine()
        self.config = Config(root_path)
        conf = self.config.get()

        self.root_path = root_path
        self.out_path = conf['out_path']
        self.flash_tool = 'sudo %s/build/bin/flash.%s' % (root_path, host_arch) 

    def flash_help(self):
        conf = self.config.get()
        bootmode = conf['bootmode']
        text = 'Usage: %s flash [options]\n' % EDGE_NAME
        text += '\n'
        text += 'Options:\n'
        text += '  -h, --help               Show this help message and exit\n'
        text += '  -q, --query              Query board flash mode(none, loader, maskrom)\n'
        text += '  -u, --uboot              Flash uboot(MiniLoader.bin and uboot.bin)\n'
        if bootmode in ('fit'):
            text += '  -k, --kernel             Flash kernel(boot.img and recovery.img)\n'
            text += '  -m, --misc               Flash misc(misc.img)\n'
            text += '  -o, --oem                Flash oem(oem.img)\n'
            text += '  -d, --data               Flash userdata(userdata.img)\n'
            text += '  -r, --rootfs             Flash rootfs(rootfs.img)\n'
        elif bootmode in ('flash'):
            text += '  -k, --kernel             Flash kernel(boot.img)\n'
        elif bootmode in ('extlinux'):
            text += '  -k, --kernel             Flash kernel(source.img, boot_linux.img and recovery.img)\n'
            text += '  -m, --misc               Flash misc(misc.img)\n'
            text += '  -r, --rootfs             Flash rootfs(rootfs.img)\n'
        else:
            sys.exit(1)
        text += '  -a, --all                Flash all images\n'
        text += '  -D, --dir                Specify the images directory\n'
        text += '\n'
        text += 'e.g.\n'
        text += '  %s build -uk\n' % EDGE_NAME
        text += '  %s build -a\n' % EDGE_NAME
        text +='\n'

        EDGE_DBG(text)

    def flash_parse_args(self, argv):
        conf = self.config.get()
        bootmode = conf['bootmode']
        flash_list = []
        image_path = None
        bootmode = conf['bootmode']
        if bootmode in ('fit'):
            short_optarg = 'hqukmodraD:'
            long_optarg = ['help', 'query', 'uboot', 'kernel', 'misc', 'oem', 'data', 'rootfs', 'all', 'dir']
            flash_list_all = ['uboot', 'misc', 'boot', 'recovery', 'oem', 'userdata', 'rootfs']
        elif bootmode in ('flash'):
            short_optarg = 'hqukaD:'
            long_optarg = ['help', 'query', 'uboot', 'kernel', 'all', 'dir']
            flash_list_all = ['uboot', 'boot']
        elif bootmode in ('extlinux'):
            short_optarg = 'hqukmraD:'
            long_optarg = ['help', 'query', 'uboot', 'kernel', 'misc', 'rootfs', 'all', 'dir']
            flash_list_all = ['uboot', 'misc', 'boot_linux', 'recovery', 'resource', 'rootfs']
        else:
            sys.exit(1)

        try:
            options,args = getopt.getopt(argv, short_optarg, long_optarg)
        except getopt.GetoptError:
            self.flash_help()
            sys.exit(1)

        if len(options) == 0:
            self.flash_help()
            sys.exit(1)

        for option, param in options:
            if option in ('-D', '--dir'):
                image_path = param

        for option, param in options:
            if option in ('-h', '--help'):
                self.flash_help()
                sys.exit(1)
            if option in ('-q', '--query'):
                flash_list = ['query']
                break
            if option in ('-a', '--all'):
                flash_list = flash_list_all
                break
            else:
                if option in ('-u', '--uboot'):
                    flash_list.append('uboot')
                if option in ('-k', '--kernel'):
                    if bootmode in ('fit'):
                        flash_list.append('boot')
                        flash_list.append('recovery')
                    elif bootmode in ('flash'):
                        flash_list.append('boot')
                    elif bootmode in ('extlinux'):
                        flash_list.append('boot_linux')
                        flash_list.append('resource')
                        flash_list.append('recovery')
                    else:
                        sys.exit(1)
                if option in ('-m', '--misc'):
                    flash_list.append('misc')
                if option in ('-d', '--data'):
                    flash_list.append('userdata')
                if option in ('-r', '--rootfs'):
                    flash_list.append('rootfs')

        return flash_list,image_path

    def query(self):
        cmd = '%s LD' % self.flash_tool
        print(edge_query_ret(cmd))
     
    def reboot(self):
        cmd = '%s rd' % self.flash_tool
        return edge_cmd(cmd, None)

    def flash_loader(self, image_path):
        cmd = '%s UL %s/MiniLoaderAll.bin' % (self.flash_tool, image_path)
        return edge_cmd(cmd, None)

    def flash_param(self, image_path):
        cmd = '%s DI -p %s/parameter.txt' % (self.flash_tool, image_path)
        return edge_cmd(cmd, None)

    def flash_part(self, part, image_path):
        flash_image = '%s/%s.img' % (image_path, part)
        if part in ('userdata', 'oem'):
            if os.path.exists(flash_image) == False:
                EDGE_WARN('%s does NOT exist' % flash_image)
                return 0
        if part in ('uboot', 'trust'):
            cmd = '%s DI -%s %s %s/parameter.txt' % (self.flash_tool, part, flash_image, image_path)
        else:
            cmd = '%s DI -%s %s' % (self.flash_tool, part, flash_image)
        return edge_cmd(cmd, None)

    def flash(self, argv):
        flash_list,image_path = self.flash_parse_args(argv)
        if len(flash_list) == 0:
            sys.exit(1)

        if flash_list[0] == 'query':
            self.query()
            sys.exit(0)

        if image_path == None:
            image_path = self.out_path

        if self.flash_loader(image_path) != 0:
            EDGE_ERR("Flash miniloader failed")
            sys.exit(1)
        if self.flash_param(image_path) != 0:
            EDGE_ERR("Flash parameter failed")
            sys.exit(1)

        for list in flash_list:
            if self.flash_part(list, image_path) != 0:
               EDGE_ERR("Flash %s failed" % list)
               sys.exit(1)

        if self.reboot() != 0:
            EDGE_ERR("Flash reboot failed")
            sys.exit(1)

        EDGE_INFO('Flash all successfully!')
        return 0
