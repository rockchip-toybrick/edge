#!/usr/bin/env python3

#encoding=utf-8
__author__ = 'addy.ke@rock-chips.com'

import traceback
import os
import sys
import json
import os.path

from utils import *
from config import Config

def SelVendorConfig(root_path):
    index = 0
    vendor_path = '%s/vendor' % root_path
    configfiles = []
    EDGE_DBG('Board list:')
    for dirname in os.listdir(vendor_path):
        if dirname[0:2] == 'rk':
            print('> %s' % dirname)
            chip_path = '%s/%s' % (vendor_path, dirname)
            for subdirname in os.listdir(chip_path):
                json_file = '%s/%s/config.json' % (chip_path, subdirname)
                if os.path.isfile(json_file):
                    configfiles.append(json_file)
                    print('  %s. %s' % (index, subdirname))
                    index += 1
    num = input('Enter the number of the board: ')
    return configfiles[int(num)]

class Env:
    def __init__(self, root_path):
        self.root_path = root_path
        self.config = Config(root_path)

    def show(self):
        self.config.show()

    def set(self):    
        json_file = SelVendorConfig(self.root_path)
        EDGE_DBG('config: %s' % json_file)
        self.config.set(json_file)
        conf = self.config.get()
        root_path = self.root_path
        out_path = conf['out_path']
        edge_cmd('mkdir -p %s' % out_path, None)
        edge_cmd('rm -rf %s/out/Images' % root_path, None)
        edge_cmd('ln -s %s %s/out/Images' % (out_path, root_path), None)

