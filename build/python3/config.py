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
        self.common_items = ['board', 'chip', 'arch']
        self.uboot_items = ['config']
        self.kernel_items = ['config', 'linuxdtb', 'androiddtb', 'initrd', 'docker', 'debug']

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
        
        # Load uboot items
        json_items = json_data.get('uboot', None)
        if json_items != None:
            self.load_items(json_items, 'uboot', self.uboot_items, conf)

        # Load kernel items
        json_items = json_data.get('kernel', None)
        if json_items != None:
            self.load_items(json_items, 'kernel', self.kernel_items, conf)

        conf['root_path'] = self.root_path
        conf['out_path'] = '%s/out/%s/%s/images' % (self.root_path, conf['chip'], conf['board'])

        return conf

    def show(self):
        conf = self.get()
        EDGE_DBG('root path: %s' % conf['root_path'])
        EDGE_DBG('out path: %s' % conf['out_path'])

        for item in self.common_items:
            EDGE_DBG('%s: %s' % (item, conf[item]))

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
