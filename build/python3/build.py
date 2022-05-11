#!/usr/bin/env python3

#encoding=utf-8
__author__ = 'addy.ke@rock-chips.com'

import traceback
import os
import sys
import getopt
import json
import platform
import time
from multiprocessing import cpu_count

from utils import *
from config import Config

class Build:
    def __init__(self, root_path):
        self.root_path = root_path
        self.config = Config(root_path)
        self.rootfs_uuid = '614e0000-0000-4b53-8000-1d28000054a9'

    def build_help(self):
        conf = self.config.get()
        bootmode = conf['bootmode']

        text = 'Usage: %s build [options]\n' % EDGE_NAME
        text += '\n'
        text += 'Options:\n'
        text += '  -h, --help               Show this help message and exit\n'
        text += '  -p, --parameter          Build parameter(parameter.txt)\n'
        text += '  -u, --uboot              Build uboot(MiniLoader.bin, uboot.bin)\n'
        if bootmode in ('extlinux'):
            text += '  -k, --kernel             Build kernel(source.img, boot_linux.img)\n'
        elif bootmode in ('fit', 'flash'):
            text += '  -k, --kernel             Build kernel(boot.img and recovery.img)\n'
        else:
            sys.exit(1)

        text += '  -a, --all                Build all images\n'
        text += '  -b, --androidboot ANDROID_DTB   Build boot image for android(boot.img)\n'
        text += '                                  Note: you should cp boot_android.img to kernel source directory first\n'
        text += '\n'
        text += 'e.g.\n'
        text += '  %s build -uk             Build uboot and kernel images\n' % EDGE_NAME
        text += '  %s build -a\n' % EDGE_NAME
        text +='\n'

        EDGE_DBG(text)

    def build_parse_args(self, argv):
        conf = self.config.get()
        bootmode = conf['bootmode']
        short_optarg = 'hpkuab:'
        long_optarg = ['help', 'parameter', 'kernel', 'uboot', 'all', 'androidboot']
        build_list_all = ['parameter', 'kernel', 'uboot']

        build_list = []
        build_args = {}
        try:
            options,args = getopt.getopt(argv, short_optarg, long_optarg)
        except getopt.GetoptError:
            self.build_help()
            sys.exit(1)

        if len(options) == 0:
            self.build_help()
            sys.exit(1)

        for option, param in options:
            if option in ('-h', '--help'):
                self.build_help()
                sys.exit(1)
            elif option in ('-a', '--all'):
                build_list = build_list_all
                break
            else:
                if option in ('-p', '--parameter'):
                    build_list.append('parameter')
                if option in ('-u', '--uboot'):
                    build_list.append('uboot')
                if option in ('-k', '--kernel'):
                    build_list.append('kernel')
                if option in ('-b', '--boot'):
                    build_list.append('boot')
                    build_args['android_dtb'] = param

        return build_list,build_args

    def build_parameter(self):
        conf = self.config.get()
        bootmode = conf['bootmode']
        EDGE_DBG('Start build parameter ...')
        parameter_file = '%s/parameter.txt' % conf['out_path']
        f = open(parameter_file, 'w+')

        line = 'FIRMWARE_VER:1.0\n'
        line += 'MACHINE_MODEL:%s\n' % conf['chip']
        line += 'MACHINE_ID:007\n'
        line += 'MANUFACTURER: rockchip\n'
        line += 'MAGIC: 0x5041524B\n'
        line += 'ATAG: 0x00200800\n'
        line += 'MACHINE: 0xFFFFFFFF\n'
        line += 'CHECK_MASK: 0x80\n'
        line += 'PWR_HLD: 0,0,A,0,1\n'
        line += 'TYPE: GPT\n'
        f.write(line)

        line = 'CMDLINE:mtdparts=rk29xxnand:%s\n' % self.config.part_cmdline()
        f.write(line)

        line = 'uuid:rootfs=%s\n' % self.rootfs_uuid
        f.write(line)

        f.close()

        EDGE_INFO('Build parameter successfully!')
    
    def build_uboot(self):
        conf = self.config.get()
        uboot_path = '%s/uboot' % self.root_path
        rkbin_path = '%s/rkbin' % self.root_path
        out_path = conf['out_path']
        chip = conf['chip']
        bootmode = conf['bootmode']
        key_path = '%s/keys' % uboot_path

        EDGE_DBG('Start build uboot ...')

        uboot_config = conf['uboot_config']
        cmd = './make.sh %s' % uboot_config
        if edge_cmd(cmd, uboot_path):
            EDGE_ERR('Build uboot failed, cmd: %s' % cmd)
            sys.exit(1)

        # Copy uboot.img to output directory
        cmd = 'cp %s/uboot.img %s/' % (uboot_path, out_path)
        if edge_cmd(cmd, None) != 0:
            EDGE_ERR('Copy uboot.img failed')
            sys.exit(1)

        # Copy MinLoader to output directory
        if chip == 'rk3568' or chip == 'rk3566':
            cmd = 'cp %s/%s_*.bin %s/MiniLoaderAll.bin' % (uboot_path, 'rk356x', out_path)
        else:
            cmd = 'cp %s/%s_*.bin %s/MiniLoaderAll.bin' % (uboot_path, chip, out_path)
        if edge_cmd(cmd, None) != 0:
            EDGE_ERR('Copy MiniLoaderAll.bin failed')
            sys.exit(1)
        
        EDGE_INFO('Build uboot successfully!')

    def build_android_boot(self, build_args):
        conf = self.config.get()
        cpus = cpu_count()
        root_path = self.root_path
        arch = conf['arch']
        host_arch = platform.machine()
        kernel_version = conf['kernel_version']
        kernel_path = '%s/kernel/linux-%s' % (root_path, kernel_version)
        gcc_v6_3_1='gcc-linaro-6.3.1-2017.05-x86_64_aarch64-linux-gnu/bin/aarch64-linux-gnu-'
        gcc_v10_3='gcc-arm-10.3-2021.07-x86_64-aarch64-none-linux-gnu/bin/aarch64-none-linux-gnu-'

        if os.uname().machine == 'x86_64':
            cross_compile = '%s/prebuilts/gcc/linux-x86/aarch64/%s' % (root_path, gcc_v10_3)
            make_cmd = 'make CROSS_COMPILE=%s LLVM=1 LLVM_IAS=1 -j %s' % (cross_compile, cpus)
        else:
            cross_compile = '/usr/bin/aarch64-linux-gnu-'
            make_cmd = 'make CROSS_COMPILE=%s -j %s' % (cross_compile, cpus)

        if arch == 'arm64':
            dtb_path = '%s/arch/arm64/boot/dts/rockchip' % kernel_path
        else:
            dtb_path = '%s/arch/arm/boot/dts' % kernel_path

        # Set env: PATH
        if os.uname().machine == 'x86_64':
            os.environ['PATH'] = '%s/prebuilts/clang/bin:%s/prebuilts/bin:%s' % (root_path, root_path, os.environ['PATH'])

        EDGE_DBG('Start build kernel ...')
        # Make config
        if os.path.exists('%s/boot_android.img' % kernel_path) == False:
            EDGE_ERR('%s/boot_android.img does NOT exist' % kernel_path)
            sys.exit(1)
        cmd = '%s ARCH=%s rockchip_defconfig android-11.config' % (make_cmd, arch)
        if edge_cmd(cmd, kernel_path):
            EDGE_ERR('Make kernel config failed')
            sys.exit(1)

        # Build kernel
        android_dtb = build_args['android_dtb']
        cmd = '%s ARCH=arm64 BOOT_IMG=./boot_android.img %s.img' % (make_cmd, android_dtb)
        if edge_cmd(cmd, kernel_path):
            EDGE_ERR('Build kernel failed, cmd: %s' % cmd)
            sys.exit(1)
        else:
            EDGE_INFO('Build kernel(%s/boot.img) successfully!' % kernel_path)
            sys.exit(0)

    def build_boot_linux(self):
        conf = self.config.get()
        cpus = cpu_count()
        root_path = self.root_path
        out_path = conf['out_path']
        kernel_version = conf['kernel_version']
        kernel_dtbname = conf['kernel_dtbname']
        kernel_debug = conf['kernel_debug']
        kernel_config = conf['kernel_config']
        kernel_docker = conf['kernel_docker']
        kernel_size = conf['kernel_size']
        chip = conf['chip']
        arch = conf['arch']
        kernel_path = '%s/kernel/linux-%s' % (root_path, kernel_version)
        host_arch = platform.machine()
        gcc_v6_3_1='gcc-linaro-6.3.1-2017.05-x86_64_aarch64-linux-gnu/bin/aarch64-linux-gnu-'
        gcc_v10_3='gcc-arm-10.3-2021.07-x86_64-aarch64-none-linux-gnu/bin/aarch64-none-linux-gnu-'

        if kernel_size == "auto":
            image_size = 72 * 1024 * 1024
        else:
            image_size = int(kernel_size) * 1024 * 1024
        blocks = 4096
        block_size = image_size / blocks

        if os.uname().machine == 'x86_64':
            cross_compile = '%s/prebuilts/gcc/linux-x86/aarch64/%s' % (root_path, gcc_v10_3)
            make_cmd = 'make CROSS_COMPILE=%s LLVM=1 LLVM_IAS=1 -j %s' % (cross_compile, cpus)
        else:
            cross_compile = '/usr/bin/aarch64-linux-gnu-'
            make_cmd = 'make CROSS_COMPILE=%s -j %s' % (cross_compile, cpus)
        toybrick_dtb = 'toybrick.dtb'

        boot_linux_path = '%s/boot_linux' % kernel_path
        if arch == 'arm64':
            dtb_path = '%s/arch/arm64/boot/dts/rockchip' % kernel_path
        else:
            dtb_path = '%s/arch/arm/boot/dts' % kernel_path

        # Set env: PATH
        if os.uname().machine == 'x86_64':
            os.environ['PATH'] = '%s/prebuilts/clang/bin:%s/prebuilts/bin:%s' % (root_path, root_path, os.environ['PATH'])

        EDGE_DBG('Start build boot linux ...')

        # Make config
        if kernel_docker:
            cmd = '%s ARCH=%s rockchip_linux_defconfig %s rockchip_docker.config' % (make_cmd, arch, kernel_config)
        else:
            cmd = '%s ARCH=%s rockchip_linux_defconfig %s' % (make_cmd, arch, kernel_config)
        if edge_cmd(cmd, kernel_path):
            EDGE_ERR('Make kernel config failed')
            sys.exit(1)

        cmd = '%s ARCH=arm64 %s.img' % (make_cmd, kernel_dtbname)
        if edge_cmd(cmd, kernel_path):
            EDGE_ERR('Build kernel failed, cmd: %s' % cmd)
            sys.exit(1)
        # mkdir boot_linux/extliux
        cmd = 'rm -rf %s; mkdir -p %s/extlinux' % (boot_linux_path, boot_linux_path)
        edge_cmd(cmd, None)

        # Copy DTB file to boot_linux/extlinux/toybrick.dtb
        cmd = 'cp %s/%s.dtb %s/extlinux/%s' % (dtb_path, kernel_dtbname, boot_linux_path, toybrick_dtb)
        if edge_cmd(cmd, None) != 0:
            EDGE_ERR('Copy %s.dtb failed' % kernel_dtbname)
            sys.exit(1)

        # Copy Image to boot_linux/extlinux/Image
        cmd = 'cp %s/arch/%s/boot/Image %s/extlinux/' % (kernel_path, arch,  boot_linux_path)
        if edge_cmd(cmd, None) != 0:
            EDGE_ERR('Copy %s.dtb failed' % kernel_dtbname)
            sys.exit(1)

        # Make extlinux.conf
        extlinux_file = '%s/extlinux/extlinux.conf' % boot_linux_path
        f = open(extlinux_file, 'w+')
        line = 'label rockchip-kernel-%s\n' % kernel_version
        line += '  kernel /extlinux/Image\n'
        line += '  fdt /extlinux/%s\n' % toybrick_dtb
        line += '  append earlycon=uart8250,mmio32,%s root=PARTUUID=%s rw rootwait rootfstype=ext4\n' % (kernel_debug, self.rootfs_uuid)
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

        if kernel_size == "auto":
            msize=int(edge_cmd_result('sudo %s/build/scripts/getsize.sh %s/boot_linux' % (root_path, kernel_path)))
            msize = msize + 4
            EDGE_INFO('Resize boot_linux.img to %dM' % msize)
            # Auto-Resize boot_linux.img
            cmd = '%s/build/scripts/resize.sh %s/boot_linux.img %dM' % (root_path, out_path, msize)
            if edge_cmd(cmd, None) != 0:
                EDGE_ERR('Auto-Resize boot_linux.img failed')
                sys.exit(1)

        EDGE_INFO('Build boot linux successfully!')

    def build_boot_image(self, target_name, initrd, lz4_compress):
        conf = self.config.get()
        cpus = cpu_count()
        root_path = self.root_path
        out_path = conf['out_path']
        arch = conf['arch']
        bootmode = conf['bootmode']
        kernel_dtbname = conf['kernel_dtbname']
        kernel_version = conf['kernel_version']
        kernel_path = '%s/kernel/linux-%s' % (root_path, kernel_version)
        initrd_path = "%s/rootfs/images/%s" % (root_path, arch)
        if arch == 'arm64':
            dtb_path = '%s/arch/arm64/boot/dts/rockchip' % kernel_path
        else:
            dtb_path = '%s/arch/arm/boot/dts' % kernel_path

        EDGE_DBG('Start boot image ...')

        boot_its = '%s/boot_its' % out_path
        f = open(boot_its, 'w+')
        line = '/*\n'
        line += ' * Copyright (C) 2021 Rockchip Electronics Co., Ltd\n'
        line += ' *\n'
        line += ' * SPDX-License-Identifier: GPL-2.0\n'
        line += ' */\n'
        line += '\n'
        line += '/dts-v1/;\n'
        line += '/ {\n'
        line += '    description = "FIT image with Linux kernel, FDT blob and resource";\n'
        line += '\n'
        line += '    images {\n'
        line += '        fdt {\n'
        line += '            data = /incbin/("%s/%s.dtb");\n' % (dtb_path, kernel_dtbname)
        line += '            type = "flat_dt";\n'
        line += '            arch = "%s";\n' % arch
        line += '            compression = "none";\n'
        line += '            load = <0xffffff00>;\n'
        line += '\n'
        line += '            hash {\n'
        line += '                algo = "sha256";\n'
        line += '            };\n'
        line += '        };\n'
        line += '\n'
        line += '        kernel {\n'
        if lz4_compress:
            line += '            data = /incbin/("%s/arch/%s/boot/Image.lz4");\n' % (kernel_path, arch)
            line += '            compression = "lz4";\n'
        else:
            line += '            data = /incbin/("%s/arch/%s/boot/Image");\n' % (kernel_path, arch)
            line += '            compression = "none";\n'
        line += '            type = "kernel";\n'
        line += '            arch = "%s";\n' % arch
        line += '            os = "linux";\n'
        line += '            entry = <0xffffff01>;\n'
        line += '            load = <0xffffff01>;\n'
        line += '\n'
        line += '            hash {\n'
        line += '                algo = "sha256";\n'
        line += '            };\n'
        line += '        };\n'
        line += '\n'
        line += '        resource {\n'
        line += '            data = /incbin/("%s/resource.img");\n' % kernel_path
        line += '            type = "multi";\n'
        line += '            arch = "";\n'
        line += '            compression = "none";\n'
        line += '\n'
        line += '            hash {\n'
        line += '                algo = "sha256";\n'
        line += '            };\n'
        line += '        };\n'
        if initrd:
            line += '\n'
            line += '        ramdisk {\n'
            line += '            data = /incbin/("%s/initrd.img");\n' % initrd_path
            line += '            type = "ramdisk";\n'
            line += '            arch = "%s";\n' % arch
            line += '            os = "linux";\n'
            line += '            compression = "lzma";\n'
            line += '            entry = <0xffffff02>;\n'
            line += '            load = <0xffffff02>;\n'
            line += '\n'
            line += '            hash {\n'
            line += '                algo = "sha256";\n'
            line += '            };\n'
            line += '        };\n'
        line += '    };\n'
        line += '\n'
        line += '    configurations {\n'
        line += '        default = "conf";\n'
        line += '\n'
        line += '        conf {\n'
        line += '            rollback-index = <0x00>;\n'
        line += '            fdt = "fdt";\n'
        line += '            kernel = "kernel";\n'
        line += '            multi = "resource";\n'
        if initrd:
            line += '            ramdisk = "ramdisk";\n'
        line += '\n'
        line += '            signature {\n'
        line += '                algo = "sha256,rsa2048";\n'
        line += '                padding = "pss";\n'
        line += '                key-name-hint = "dev";\n'
        if initrd:
            line += '                sign-images = "fdt", "kernel", "multi", "ramdisk";\n'
        else:
            line += '                sign-images = "fdt", "kernel", "multi";\n'
        line += '            };\n'
        line += '        };\n'
        line += '    };\n'
        line += '};\n'
        f.write(line)
        f.close()

        cmd = '%s/rkbin/tools/mkimage -f %s -E -p 0x1000 %s/%s' % (root_path, boot_its, out_path, target_name)
        if edge_cmd(cmd, None) != 0:
            EDGE_ERR("mkimage failed")
            sys.exit(1)
        EDGE_INFO('Build boot image successfully!')

    def build_boot_fit(self):
        self.build_boot_linux()
        self.build_boot_image('boot.img', False, False)
        self.build_boot_image('recovery.img', True, False)
        EDGE_INFO('Build boot fit successfully!')

    def build_boot_flash(self):
        self.build_boot_linux()
        self.build_boot_image('boot.img', True, True)
        EDGE_INFO('Build boot flash successfully!')

    def build_boot_extlinux(self):
        self.build_boot_linux()
        self.build_boot_image('recovery.img', True, False)
        EDGE_INFO('Build boot extlinux successfully!')

    def build(self, argv):
        conf = self.config.get()
        root_path =self.root_path
        out_path = conf['out_path']
        bootmode = conf['bootmode']
        if bootmode in ('fit', 'extlinux'):
            edge_cmd('cp %s/build/bin/misc.img %s' % (root_path, out_path), None)
        build_list,build_args = self.build_parse_args(argv)
        if len(build_list) == 0:
            sys.exit(1)
        for list in build_list:
            if list == 'parameter':
                self.build_parameter()
            elif list == 'uboot':
                self.build_uboot()
            elif list == 'kernel':
                if bootmode in ('fit'):
                    self.build_boot_fit()
                elif bootmode in ('flash'):
                    self.build_boot_flash()
                elif bootmode in ('extlinux'):
                    self.build_boot_extlinux()
                else:
                    sys.exit(1)
            elif list == 'androidboot':
                self.build_android_boot(build_args)

        EDGE_INFO('Build all successfully!')
        return 0
