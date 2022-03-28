#!/usr/bin/env python3

#encoding=utf-8
__author__ = 'addy.ke@rock-chips.com'

import sys
import os.path
import json
import subprocess
import glob

def EDGE_DBG(msg):
    level = 'debug'
    for line in str(msg).splitlines():
        sys.stdout.write(message(level, line, '0;0m'))
        sys.stdout.flush()

def EDGE_INFO(msg):
    level = 'info'
    for line in str(msg).splitlines():
        sys.stdout.write(message(level, line, '0;32m'))
        sys.stdout.flush()

def EDGE_WARN(msg):
    level = 'warning'
    for line in str(msg).splitlines():
        sys.stderr.write(message(level, line, '0;33m'))
        sys.stderr.flush()


def EDGE_ERR(msg):
    level = 'error'
    for line in str(msg).splitlines():
        sys.stderr.write(message(level, line, '0;31m'))
        sys.stderr.flush()


def message(level, msg, color):
    if isinstance(msg, str) and not msg.endswith('\n'):
        msg += '\n'
    return '\033[{}[EDGE {}] {}\033[0m'.format(color, level.upper(), msg)

class EDGEException(Exception):
    pass

# Read json file data
def read_json_file(input_file):
    if not os.path.isfile(input_file):
        return None

    with open(input_file, 'rb') as input_f:
        try:
            data = json.load(input_f)
            return data
        except json.JSONDecodeError:
            return None


def dump_json_file(dump_file, json_data):
    with open(dump_file, 'wt', encoding='utf-8') as json_file:
        json.dump(json_data,
                  json_file,
                  ensure_ascii=False,
                  indent=2)

def edge_cmd(cmd, path):
    EDGE_DBG(cmd)
    if path == None:
        return os.system(cmd)
    else:
        return subprocess.call(cmd, shell=True, cwd=path)

def edge_cmd_result(cmd):
    r = os.popen(cmd)
    text = r.read()
    r.close()
    return text
