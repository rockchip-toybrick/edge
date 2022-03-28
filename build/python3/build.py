#!/usr/bin/env python3

#encoding=utf-8
__author__ = 'addy.ke@rock-chips.com'

import traceback
import os
import sys
import getopt
import json
import platform
from multiprocessing import cpu_count

from utils import *
from config import Config

class Build:
    def __init__(self, root_path):
        self.root_path = root_path
        self.config = Config(root_path)
      
    def build_uboot(self):
        conf = self.config.get()
        uboot_path = '%s/uboot' % self.root_path
        rkbin_path = '%s/rkbin' % self.root_path
        out_path = conf['out_path']
        arch = conf['arch']
        chip = conf['chip']

        EDGE_DBG('Start build uboot ...')

        # Build uboot
        cmd = './make.sh %s' % conf['uboot_config']
        if edge_cmd(cmd, uboot_path):
            EDGE_ERR('Build uboot failed, cmd: %s' % cmd)
            sys.exit(1)

        # Copy uboot.img to output directory
        cmd = 'cp %s/uboot.img %s/' % (uboot_path, out_path)
        if edge_cmd(cmd, None) != 0:
            EDGE_ERR('Copy uboot.img failed')
            sys.exit(1)

        cmd = 'cp %s/%s_*.bin %s/MiniLoaderAll.bin' % (uboot_path, chip, out_path)
        if edge_cmd(cmd, None) != 0:
            EDGE_ERR('Copy MiniLoaderAll.bin failed')
            sys.exit(1)

        EDGE_INFO('Build uboot successfully!')

    def build_kernel(self, boot_android):
        conf = self.config.get()
        cpus = cpu_count()
        root_path = self.root_path
        out_path = conf['out_path']
        kernel_version = '5.10'
        linux_dtbname = conf['kernel_linuxdtb']
        android_dtbname = conf['kernel_androiddtb']
        kernel_debug = conf['kernel_debug']
        kernel_config = conf['kernel_config']
        kernel_docker = conf['kernel_docker']
        kernel_initrd = conf['kernel_initrd']
        rootfs_uuid = '614e0000-0000-4b53-8000-1d28000054a9'
        chip = conf['chip']
        arch = conf['arch']
        kernel_path = '%s/kernel/linux-%s' % (root_path, kernel_version)
        host_arch = platform.machine()

        image_size = 72 * 1024 * 1024
        blocks = 4096
        block_size = image_size / blocks

        if os.uname().machine == 'x86_64':
            cross_compile = '%s/prebuilts/gcc/linux-x86/aarch64/gcc-linaro-6.3.1-2017.05-x86_64_aarch64-linux-gnu/bin/aarch64-linux-gnu-' % root_path
            make_cmd = 'make CROSS_COMPILE=%s LLVM=1 LLVM_IAS=1 -j %s' % (cross_compile, cpus)
        else:
            cross_compile = '/usr/bin/aarch64-linux-gnu-'
            make_cmd = 'make CROSS_COMPILE=%s -j %s' % (cross_compile, cpus)
        toybrick_dtb = 'toybrick.dtb'

        boot_linux_path = '%s/boot_linux' % kernel_path
        dtb_path = '%s/arch/arm64/boot/dts/rockchip' % kernel_path

        # Set env: PATH
        if os.uname().machine == 'x86_64':
            os.environ['PATH'] = '%s/prebuilts/clang/bin:%s/prebuilts/bin:%s' % (root_path, root_path, os.environ['PATH'])

        EDGE_DBG('Start build kernel ...')

        # Make config
        if boot_android:
            if os.path.exists('%s/boot_android.img' % kernel_path) == False:
                EDGE_ERR('%s/boot_android.img does NOT exist' % kernel_path)
                sys.exit(1)
            cmd = '%s ARCH=%s rockchip_defconfig %s android-11.config' % (make_cmd, arch, kernel_config)
        elif kernel_docker:
            cmd = '%s ARCH=%s rockchip_linux_defconfig %s rockchip_docker.config' % (make_cmd, arch, kernel_config)
        else:
            cmd = '%s ARCH=%s rockchip_linux_defconfig %s' % (make_cmd, arch, kernel_config)
        if edge_cmd(cmd, kernel_path):
            EDGE_ERR('Make kernel config failed')
            sys.exit(1)
        # Build kernel
        if boot_android:
            cmd = '%s ARCH=arm64 BOOT_IMG=./boot_android.img %s.img' % (make_cmd, android_dtbname)
            if edge_cmd(cmd, kernel_path):
                EDGE_ERR('Build kernel failed, cmd: %s' % cmd)
                sys.exit(1)
            else:
                # Copy boot.img to output directory
                cmd = 'cp %s/boot.img %s/' % (kernel_path, out_path)
                if edge_cmd(cmd, None) != 0:
                    EDGE_ERR('Copy boot.img failed')
                    sys.exit(1)
                EDGE_INFO('Build kernel(%s/boot.img) successfully!' % kernel_path)
                sys.exit(0)

        cmd = '%s ARCH=arm64 %s.img' % (make_cmd, linux_dtbname)
        if edge_cmd(cmd, kernel_path):
            EDGE_ERR('Build kernel failed, cmd: %s' % cmd)
            sys.exit(1)
        # mkdir boot_linux/extliux
        cmd = 'rm -rf %s; mkdir -p %s/extlinux' % (boot_linux_path, boot_linux_path)
        edge_cmd(cmd, None)

        # Copy DTB file to boot_linux/extlinux/toybrick.dtb
        cmd = 'cp %s/%s.dtb %s/extlinux/%s' % (dtb_path, linux_dtbname, boot_linux_path, toybrick_dtb)
        if edge_cmd(cmd, None) != 0:
            EDGE_ERR('Copy %s.dtb failed' % linux_dtbname)
            sys.exit(1)

        # Copy Image to boot_linux/extlinux/Image
        cmd = 'cp %s/arch/%s/boot/Image %s/extlinux/' % (kernel_path, arch,  boot_linux_path)
        if edge_cmd(cmd, None) != 0:
            EDGE_ERR('Copy %s.dtb failed' % linux_dtbname)
            sys.exit(1)

        if kernel_initrd:
            # Copy initrd.img to boot_linux/extlinux/
            cmd = 'cp %s/rootfs/images/%s/initrd.img %s/' % (root_path, arch, boot_linux_path)
            if edge_cmd(cmd, None) != 0:
                EDGE_ERR('Copy initrd.img failed')
                sys.exit(1)

        # Make extlinux.conf
        extlinux_file = '%s/extlinux/extlinux.conf' % boot_linux_path
        f = open(extlinux_file, 'w+')
        line = 'label rockchip-kernel-%s\n' % kernel_version
        f.write(line)
        line = '  kernel /extlinux/Image\n'
        f.write(line)
        line = '  fdt /extlinux/%s\n' % toybrick_dtb
        f.write(line)
        if kernel_initrd:
            line = '  append earlycon=uart8250,mmio32,%s initrd=/initrd.img root=PARTUUID=%s rw rootwait rootfstype=ext4\n' % (kernel_debug, rootfs_uuid)
        else:
            line = '  append earlycon=uart8250,mmio32,%s root=PARTUUID=%s rw rootwait rootfstype=ext4\n' % (kernel_debug, rootfs_uuid)
        f.write(line)
        f.close()

        # Generate boot_linux.img (etx2)
        cmd = 'genext2fs -B %d -b %d -d %s/boot_linux -i 8192 -U %s/boot_linux.img' % (blocks, block_size, kernel_path, kernel_path)
        if edge_cmd(cmd, None) != 0:
            EDGE_ERR('make boot_linux.img failed')
            sys.exit(1)

        # Copy boot_linux.img to output directory
        cmd = 'cp %s/boot_linux.img %s/' % (kernel_path, out_path)
        if edge_cmd(cmd, None) != 0:
            EDGE_ERR('Copy boot_linux.img failed')
            sys.exit(1)

        # Copy resource.img to output directory
        cmd = 'cp %s/resource.img %s/' % (kernel_path, out_path)
        if edge_cmd(cmd, None) != 0:
            EDGE_ERR('Copy resource.img failed')
            sys.exit(1)

        EDGE_INFO('Build kernel successfully!')

    def build(self, build_list):
        apply_patch = False
        conf = self.config.get()
        root_path =self.root_path
        out_path = conf['out_path']

        for list in build_list:
            if list == 'uboot':
                self.build_uboot()
            elif list == 'kernel':
                self.build_kernel(False)
            elif list == 'boot':
                self.build_kernel(True)

        EDGE_INFO('Build all successfully!')
        return 0
