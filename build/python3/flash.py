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
        config = Config(root_path)
        conf = config.get()

        self.root_path = root_path
        self.out_path = conf['out_path']
        self.flash_tool = 'sudo %s/build/bin/flash.%s' % (root_path, host_arch) 

    def query(self):
        cmd = '%s LD' % self.flash_tool
        print(edge_query_ret(cmd))
     
    def reboot(self):
        cmd = '%s rd' % self.flash_tool
        return edge_cmd(cmd, None)

    def flash_loader(self):
        cmd = '%s UL %s/MiniLoaderAll.bin' % (self.flash_tool, self.out_path)
        return edge_cmd(cmd, None)

    def flash_param(self):
        cmd = '%s DI -p %s/parameter.txt' % (self.flash_tool, self.out_path)
        return edge_cmd(cmd, None)

    def flash_part(self, part):
        if part in ('uboot', 'trust'):
            cmd = '%s DI -%s %s/%s.img %s/parameter.txt' % (self.flash_tool, part, self.out_path, part, self.out_path)
        else:
            cmd = '%s DI -%s %s/%s.img' % (self.flash_tool, part, self.out_path, part)
        return edge_cmd(cmd, None)

    def flash(self, flash_list):
        if flash_list[0] == 'query':
            self.query()
            sys.exit(0)

        if self.flash_loader() != 0:
            EDGE_ERR("Flash miniloader failed")
            sys.exit(1)
        if self.flash_param() != 0:
            EDGE_ERR("Flash parameter failed")
            sys.exit(1)

        for list in flash_list:
            if self.flash_part(list) != 0:
               EDGE_ERR("Flash %s failed" % list)
               sys.exit(1)

        if self.reboot() != 0:
            EDGE_ERR("Flash reboot failed")
            sys.exit(1)

        EDGE_INFO('Flash all successfully!')
        return 0
