#!/usr/bin/env python3

#encoding=utf-8
__author__ = 'addy.ke@rock-chips.com'

import traceback
import os
import sys
import json
import os.path

from utils import *

class Config:
    def __init__(self, root_path):
        self.root_path = root_path
        self.common_items = ['board', 'chip', 'arch', 'bootmode']
        self.secureboot_items = ['enable', 'rollback', 'burnkey']
        self.part_extlinux_items = ['uboot', 'misc', 'boot_linux:bootable', 'recovery', 'resource', 'rootfs:grow']
        self.part_fit_items = ['uboot', 'misc', 'boot', 'recovery', 'backup', 'rootfs', 'oem', 'userdata:grow']
        self.part_flash_items = ['vnvm', 'uboot', 'boot']
        self.uboot_items = ['config']
        self.kernel_items = ['version', 'config', 'dtbname', 'size', 'docker', 'debug']
        self.rootfs_items = ['osname', 'version', 'type', 'apturl', 'uuid', 'size', 'user', 'password', 'relver']

        self.default_file = '%s/vendor/common/config.json' % root_path
        self.edge_file = '%s/edge_config.json' % root_path

        self.conf = {}
        self.load(self.default_file, self.conf)
        if self.load(self.edge_file, self.conf) == None:
            self.conf_set = False
        else:
            self.conf_set = True

    def load_items(self, json_items, key, items, conf):
        for item in items:
            val = json_items.get(item, None)
            if val != None:
                if key == None:
                    conf[item] = val
                else:
                    conf['%s_%s' % (key, item)] = val
        return conf

    def load(self, json_file, conf):
        json_data = read_json_file(json_file)
        if json_data == None:
            return None

        # Load common items
        self.load_items(json_data, None, self.common_items, conf)
        
        # Load secureboot items
        json_items = json_data.get('secureboot', None)
        if json_items != None:
            self.load_items(json_items, 'secureboot', self.secureboot_items,conf)

        # Load part items
        json_items = json_data.get('part-extlinux', None)
        if json_items != None:
            self.load_items(json_items, 'part-extlinux', self.part_extlinux_items,conf)

        json_items = json_data.get('part-fit', None)
        if json_items != None:
            self.load_items(json_items, 'part-fit', self.part_fit_items,conf)

        json_items = json_data.get('part-flash', None)
        if json_items != None:
            self.load_items(json_items, 'part-flash', self.part_flash_items,conf)

        # Load uboot items
        json_items = json_data.get('uboot', None)
        if json_items != None:
            self.load_items(json_items, 'uboot', self.uboot_items, conf)

        # Load kernel items
        json_items = json_data.get('kernel', None)
        if json_items != None:
            self.load_items(json_items, 'kernel', self.kernel_items, conf)

        # Load rootfs items
        json_items = json_data.get('rootfs', None)
        if json_items != None:
            self.load_items(json_items, 'rootfs', self.rootfs_items, conf)

        conf['root_path'] = self.root_path
        conf['out_path'] = '%s/out/%s/%s/images' % (self.root_path, conf['chip'], conf['board'])

        if conf['bootmode'] in ('extlinux', 'flash') and conf['secureboot_enable']:
            EDGE_ERR('Only boot mode <fit> can enable secure boot')
            sys.exit(1)

        return conf

    def show(self):
        conf = self.get()
        bootmode = conf['bootmode']
        EDGE_DBG('root path: %s' % conf['root_path'])
        EDGE_DBG('out path: %s' % conf['out_path'])

        for item in self.common_items:
            EDGE_DBG('%s: %s' % (item, conf[item]))

        EDGE_DBG('> Secureboot:')
        for item in self.secureboot_items:
            EDGE_DBG('  %s: %s' % (item, conf['secureboot_%s' % item]))
        
        EDGE_DBG('> Partition:')
        if bootmode in ('extlinux'):
            for item in self.part_extlinux_items:
                EDGE_DBG('  %s: %s' % (item, conf['part-extlinux_%s' % item]))
        elif bootmode in ('fit'):
            for item in self.part_fit_items:
                EDGE_DBG('  %s: %s' % (item, conf['part-fit_%s' % item]))
        elif bootmode in ('flash'):
            for item in self.part_flash_items:
                EDGE_DBG('  %s: %s' % (item, conf['part-flash_%s' % item]))
        else:
            EDGE_ERR('boot mode <%s> is not supported' % bootmode)
            sys.exit(1)
        
        EDGE_DBG('> Uboot:')
        for item in self.uboot_items:
            EDGE_DBG('  %s: %s' % (item, conf['uboot_%s' % item]))
        
        EDGE_DBG('> Kernel:')
        for item in self.kernel_items:
            EDGE_DBG('  %s: %s' % (item, conf['kernel_%s' % item]))
        
    def get(self):
        if self.conf_set == False:
            EDGE_ERR('Build env is not set')
            sys.exit(1)
        return self.conf

    def set(self, json_file):
        json_data = read_json_file(json_file)
        dump_json_file(self.edge_file, json_data)
        self.conf = {}
        self.load(self.default_file, self.conf)
        if self.load(self.edge_file, self.conf) == None:
            self.conf_set = False
        else:
            self.conf_set = True

        if self.conf['secureboot_burnkey']:
            EDGE_WARN('<burnkey> is enabled, this will burn the sign key to efuse at first boot!')
            EDGE_WARN('The system will only boot the singed images with this key!')
            EDGE_WARN('Please keep the key properly!!!')

        edge_cmd('rm -rf config.cfg; ln -s config-%s.cfg config.cfg' % self.conf['bootmode'], "%s/tools/RKDevTool_Release" % self.root_path)

    def part_cmdline(self):
        cmdline = ''
        bootmode = self.conf['bootmode']
        if bootmode in ('extlinux'):
            part_items = self.part_extlinux_items
        elif bootmode in ('fit'):
            part_items = self.part_fit_items
        elif bootmode in ('flash'):
            part_items = self.part_flash_items
        else:
            EDGE_ERR('boot mode <%s> is not supported' % bootmode)
            sys.exit(1)
        for item in part_items:
            val = self.conf['part-%s_%s' % (bootmode, item)]
            if val[1] == '-':
                cmdline += "%s@%s(%s)" % (val[1], val[0], item)
                break
            elif int(val[1], 16) == 0:
                continue
            else:
                cmdline += "%s@%s(%s)," % (val[1], val[0], item)
        return cmdline
