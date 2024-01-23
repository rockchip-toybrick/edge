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
            text += '  -s, --sign               Create new keys and save to uboot/keys\n'
            text += '  -o, --ota list           Build ota image(update.img) specified by the parameter: list\n'
            text += '                           list is the name of images to be update which separated by commas\n'
        else:
            sys.exit(1)

        text += '  -r, --rootfs             Build rootfs(rootfs.img)\n'
        text += '  -U, --update             Build update(update.img)\n'
        text += '  -g, --guest              Build guest(guest.img)\n'
        text += '  -a, --all                Build all images\n'
        text += '  -d, --debpkg             Build debpkg\n'
        text += '  -m, --module MODULE      Build kernel module in kernel/modules directory\n'
        text += '  -b, --androidboot ANDROID_DTB   Build boot image for android(boot.img)\n'
        text += '                                  Note: you should cp boot_android.img to kernel source directory first\n'
        text += '\n'
        text += 'e.g.\n'
        text += '  %s build -uk             Build uboot and kernel images\n' % EDGE_NAME
        text += '  %s build -o uboot,boot   Build ota update.img, include uboot.img and boot.img to be update\n' % EDGE_NAME
        text += '  %s build -a\n' % EDGE_NAME
        text +='\n'

        EDGE_DBG(text)

    def build_parse_args(self, argv):
        conf = self.config.get()
        short_optarg_common = 'hpukrUgadm:b:'
        long_optarg_common = ['help', 'parameter', 'uboot', 'kernel', 'rootfs', 'update', 'guest', 'all', 'debpkg', 'module', 'androidboot']
        build_list_all = ['parameter', 'kernel', 'uboot', 'rootfs', 'update']
        bootmode = conf['bootmode']
        if bootmode in ('extlinux'):
            short_optarg = short_optarg_common
            long_optarg = long_optarg_common
        elif bootmode in ('fit', 'flash'):
            short_optarg = short_optarg_common + 'so:'
            long_optarg = long_optarg_common + ['sing', 'ota']
        else:
            sys.exit(1)

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
                if option in ('-r', '--rootfs'):
                    build_list.append('rootfs')
                if option in ('-U', '--update'):
                    build_list.append('update')
                if option in ('-m', '--module'):
                    build_list.append('module')
                    build_args['module'] = param
                if option in ('-b', '--androidboot'):
                    build_list.append('androidboot')
                    build_args['android_dtb'] = param
                if option in ('-s', '--sign'):
                    build_list.append('sign')
                if option in ('-g', '--guest'):
                    build_list.append('guest')
                if option in ('-d', '--debpkg'):
                    build_list.append('debpkg')
                if option in ('-o', '--ota'):
                    build_list.append('ota')
                    build_args['ota'] = param

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

        line = 'uuid:rootfs=%s\n' % conf['rootfs_uuid']
        f.write(line)

        f.close()

        EDGE_INFO('Build parameter successfully!')
    
    def build_uboot_secure_boot_config(self, old):
        conf = self.config.get()
        board = conf['board']

        if board.find('SD0') >= 0:
            uboot_path = '%s/uboot-sd0' % self.root_path
        else:
            uboot_path = '%s/uboot' % self.root_path

        # make secure boot config
        new = '%s/configs/%s-s.config' % (uboot_path, old)
        f = open(new, 'w+')
        line = 'CONFIG_BASE_DEFCONFIG="%s.config"\n' % old
        line += 'CONFIG_FIT_SIGNATURE=y\n'
        line += 'CONFIG_FIT_ROLLBACK_PROTECT=y\n'
        line += 'CONFIG_SPL_FIT_SIGNATURE=y\n'
        line += 'CONFIG_SPL_FIT_ROLLBACK_PROTECT=y\n'
        line += '# CONFIG_OPTEE_ALWAYS_USE_SECURITY_PARTITION is not set\n'
        f.write(line)
        f.close()
        return "%s-s" % old
        
    def build_uboot(self):
        conf = self.config.get()
        board = conf['board']

        if board.find('SD0') >= 0:
            uboot_path = '%s/uboot-sd0' % self.root_path
            rkbin_path = '%s/rkbin-sd0' % self.root_path
        else:
            uboot_path = '%s/uboot' % self.root_path
            rkbin_path = '%s/rkbin' % self.root_path

        out_path = conf['out_path']
        chip = conf['chip']
        bootmode = conf['bootmode']
        secureboot_enable = conf['secureboot_enable']
        secureboot_rollback = conf['secureboot_rollback']
        secureboot_burnkey = conf['secureboot_burnkey']
        key_path = '%s/keys' % uboot_path

        EDGE_DBG('Start build uboot ...')

        uboot_config = conf['uboot_config']
        # Build uboot
        if secureboot_enable:
            ret1 = edge_cmd_result('fdtdump %s/boot.img 2>&1 | grep hashed-strings' % out_path)
            ret2 = edge_cmd_result('fdtdump %s/recovery.img 2>&1 | grep hashed-strings' % out_path)
            if len(ret1) != 0 or len(ret2) != 0:
                EDGE_ERR('boot.img or recovery.img has been signed, please run "./edge build -k" to build a non-singed one first')
                sys.exit(1)
            if os.path.exists('%s/dev.key' % key_path) == False or os.path.exists('%s/dev.crt' % key_path) == False or os.path.exists('%s/dev.pubkey' % key_path) == False:
                EDGE_ERR('Secure boot sign keys <%s/{dev.key, dev.crt, dev.pubkey}> dose NOT exist' % key_path)
                EDGE_ERR('Please copy them to %s, or run "./edge build -s" to generate new keys' % key_path)
                sys.exit(1)

            uboot_config = self.build_uboot_secure_boot_config(uboot_config)
            if os.path.exists('%s/boot.img' % out_path) == False or os.path.exists('%s/recovery.img' % out_path) == False:
                EDGE_ERR('boot.img or recovery.img is NOT in %s' % out_path)
                sys.exit(1)

            cmd = './make.sh %s --spl-new --boot_img %s/boot.img --recovery_img %s/recovery.img' % (uboot_config, out_path, out_path)
            if secureboot_rollback[0] >= 0:
                cmd += ' --rollback-index-uboot %s' % secureboot_rollback[0]
            if secureboot_rollback[1] >= 0:
                cmd += ' --rollback-index-boot %s' % secureboot_rollback[1]
                cmd += ' --rollback-index-recovery %s' % secureboot_rollback[1]
            if secureboot_burnkey:
                cmd += ' --burn-key-hash'
        else:
            cmd = './make.sh %s' % uboot_config
        if edge_cmd(cmd, uboot_path):
            EDGE_ERR('Build uboot failed, cmd: %s' % cmd)
            sys.exit(1)

        # Copy uboot.img to output directory
        if secureboot_enable:
            cmd = 'cp %s/{uboot.img,boot.img,recovery.img} %s/' % (uboot_path, out_path)
        else:
            cmd = 'cp %s/uboot.img %s/' % (uboot_path, out_path)
        if edge_cmd(cmd, None) != 0:
            EDGE_ERR('Copy uboot.img failed')
            sys.exit(1)

        if os.path.exists('%s/trust.img' % uboot_path) == True:
            edge_cmd('cp %s/trust.img %s/' % (uboot_path, out_path), None)

        # Copy MinLoader to output directory
        if chip == 'rk3568' or chip == 'rk3566':
            cmd = 'cp %s/%s_*.bin %s/MiniLoaderAll.bin' % (uboot_path, 'rk356x', out_path)
        elif chip == 'rk3399pro':
            cmd = 'cp %s/%s_*.bin %s/MiniLoaderAll.bin' % (uboot_path, 'rk3399', out_path)
        else:
            cmd = 'cp %s/%s_*.bin %s/MiniLoaderAll.bin' % (uboot_path, chip, out_path)
        if edge_cmd(cmd, None) != 0:
            EDGE_ERR('Copy MiniLoaderAll.bin failed')
            sys.exit(1)
        
        EDGE_INFO('Build uboot successfully!')

    def build_kernel(self, target, dtbname, module):
        conf = self.config.get()
        cpus = cpu_count() * 2
        root_path = self.root_path
        out_path = conf['out_path']
        kernel_version = conf['kernel_version']
        kernel_config = conf['kernel_config']
        kernel_docker = conf['kernel_docker']
        chip = conf['chip']
        arch = conf['arch']
        hyper = conf['hyper']
        kernel_path = '%s/kernel/linux-%s' % (root_path, kernel_version)
        hyper_path = '%s/hyper/%s/host' % (root_path, hyper)
        module_path = '%s/kernel/modules' % root_path

        host_arch = platform.machine()
        gcc_v6_3_1='gcc-linaro-6.3.1-2017.05-x86_64_aarch64-linux-gnu/bin/aarch64-linux-gnu-'
        gcc_v10_3='gcc-arm-10.3-2021.07-x86_64-aarch64-none-linux-gnu/bin/aarch64-none-linux-gnu-'

        if host_arch == 'x86_64':
            # Set env: PATH
            os.environ['PATH'] = '%s/prebuilts/clang/host/linux-x86/clang-r416183b/bin:%s' % (root_path, os.environ['PATH'])
            cross_compile = '%s/prebuilts/gcc/linux-x86/aarch64/%s' % (root_path, gcc_v10_3)
            make_cmd = 'make CROSS_COMPILE=%s LLVM=1 LLVM_IAS=1 -j %s' % (cross_compile, cpus)
        else:
            cross_compile = '/usr/bin/aarch64-linux-gnu-'
            make_cmd = 'make CROSS_COMPILE=%s -j %s' % (cross_compile, cpus)

        if kernel_docker:
            kernel_config += ' rockchip_docker.config'

        if hyper != 'not set':
            kernel_config += ' rockchip_%s.config' % hyper

        if kernel_version.find('rt') >= 0:
            kernel_config += ' rockchip_rt.config'

        if dtbname is None:
            dtbname = conf['kernel_dtbname']

        if target in ('android'):
            cmd_config = '%s ARCH=%s rockchip_defconfig %s' % (make_cmd, arch, kernel_config)
            cmd_image = '%s ARCH=%s BOOT_IMG=./boot_android.img %s.img' % (make_cmd, arch, dtbname)
            cmd_module = None
            cmd_debpkg = None
        elif target in ('module'):
            cmd_config = None
            cmd_image = None
            cmd_module = '%s ARCH=%s KSRC=%s -C %s/%s TARGET_ARCH=aarch64' % (make_cmd, arch, kernel_path, module_path, module);
            cmd_debpkg = None
        elif target in ('debpkg'):
            cmd_config = None
            cmd_image = None
            cmd_module = None
            cmd_debpkg = '%s ARCH=%s bindeb-pkg' % (make_cmd, arch)
        else:
            cmd_config = '%s ARCH=%s rockchip_linux_defconfig %s' % (make_cmd, arch, kernel_config)
            cmd_image = '%s ARCH=%s %s.img' % (make_cmd, arch, dtbname)
            cmd_module = None
            cmd_debpkg = None

        EDGE_DBG('Start build kernel ...')

        # Make config
        if edge_cmd(cmd_config, kernel_path):
            EDGE_ERR('Make kernel config failed')
            sys.exit(1)

        if edge_cmd(cmd_image, kernel_path):
            EDGE_ERR('Build kernel image failed')
            sys.exit(1)

        if edge_cmd(cmd_module, kernel_path):
            EDGE_ERR('Build kernel module failed')
            sys.exit(1)

        if edge_cmd(cmd_debpkg, kernel_path):
            EDGE_ERR('Build kernel debpkg failed')
            sys.exit(1)

        image = '%s/arch/%s/boot/Image' % (kernel_path, arch)
        if arch == 'arm64':
            dtb = '%s/arch/arm64/boot/dts/rockchip/%s.dtb' % (kernel_path, dtbname)
        else:
            dtb = '%s/arch/arm/boot/dts/%s.dtb' % (kernel_path, dtbname)

        if hyper in 'xen':
            if edge_cmd('./make.sh %s %s %s' % (root_path, image, chip), hyper_path):
                EDGE_ERR('Build hyper image failed')
                sys.exit(1)
            image = '%s/Image-%s.bin' % (hyper_path, chip)

        EDGE_INFO('Build kernel successfully!')
        return image,dtb

    def build_android_boot(self, build_args):
        android_dtbname = build_args['android_dtb']
        self.build_kernel('android', android_dtbname, None)

    def build_extlinux_image(self, image, dtb):
        conf = self.config.get()
        root_path = self.root_path
        out_path = conf['out_path']
        kernel_version = conf['kernel_version']
        kernel_debug = conf['kernel_debug']
        kernel_size = conf['kernel_size']
        rootfs_uuid = conf['rootfs_uuid']
        kernel_version = conf['kernel_version']
        kernel_path = '%s/kernel/linux-%s' % (root_path, kernel_version)

        EDGE_DBG('Start build extlinux image ...')
        if kernel_size == "auto":
            image_size = 72 * 1024 * 1024
        else:
            image_size = int(kernel_size) * 1024 * 1024
        blocks = 4096
        block_size = image_size / blocks

        toybrick_dtb = 'toybrick.dtb'
        boot_linux_path = '%s/boot_linux' % kernel_path

        # mkdir boot_linux/extliux
        cmd = 'rm -rf %s; mkdir -p %s/extlinux' % (boot_linux_path, boot_linux_path)
        edge_cmd(cmd, None)

        # Copy DTB file to boot_linux/extlinux/toybrick.dtb
        cmd = 'cp %s %s/extlinux/%s' % (dtb, boot_linux_path, toybrick_dtb)
        if edge_cmd(cmd, None) != 0:
            EDGE_ERR('Copy %s failed' % dtb)
            sys.exit(1)

        # Copy Image to boot_linux/extlinux/Image
        cmd = 'cp %s %s/extlinux/Image' % (image,  boot_linux_path)
        if edge_cmd(cmd, None) != 0:
            EDGE_ERR('Copy %s failed' % image)
            sys.exit(1)

        # Make extlinux.conf
        extlinux_file = '%s/extlinux/extlinux.conf' % boot_linux_path
        f = open(extlinux_file, 'w+')
        line = 'label rockchip-kernel-%s\n' % kernel_version
        line += '  kernel /extlinux/Image\n'
        line += '  fdt /extlinux/%s\n' % toybrick_dtb
        line += '  append earlycon=uart8250,mmio32,%s root=PARTUUID=%s rw rootwait rootfstype=ext4\n' % (kernel_debug, rootfs_uuid)
        f.write(line)
        f.close()

        # Generate boot_linux.img (etx2)
        cmd = 'genext2fs -B %d -b %d -d %s/boot_linux -i 8192 -U %s/boot_linux.img' % (blocks, block_size, kernel_path, kernel_path)
        if edge_cmd(cmd, None) != 0:
            EDGE_ERR('make boot_linux.img failed')
            sys.exit(1)

        if kernel_size == "auto":
            msize=int(edge_cmd_result('sudo %s/build/scripts/getsize.sh %s/boot_linux' % (root_path, kernel_path)))
            msize = msize + 4
            EDGE_INFO('Resize boot_linux.img to %dM' % msize)
            # Auto-Resize boot_linux.img
            cmd = '%s/build/scripts/resize.sh %s/boot_linux.img %dM' % (root_path, kernel_path, msize)
            if edge_cmd(cmd, None) != 0:
                EDGE_ERR('Auto-Resize boot_linux.img failed')
                sys.exit(1)

        edge_cmd('mv %s/boot_linux.img %s/' % (kernel_path, out_path), None)
        edge_cmd('cp %s/resource.img %s/' % (kernel_path, out_path), None)
        EDGE_INFO('Build extlinux image successfully!')

    def build_fit_image(self, target_name, image, dtb, initrd, compress_type):
        conf = self.config.get()
        root_path = self.root_path
        board = conf['board']
        out_path = conf['out_path']
        arch = conf['arch']
        kernel_version = conf['kernel_version']
        kernel_path = '%s/kernel/linux-%s' % (root_path, kernel_version)

        if board.find('SD0') >= 0:
            initrd_path = "%s/rootfs/debian/initrd-sd0" % root_path
        else:
            initrd_path = "%s/rootfs/debian/initrd" % root_path

        EDGE_DBG('Start build %s ...' % target_name)

        if compress_type:
            cmd = './make_initrd.sh %s %s' % (arch, compress_type)
        else:
            cmd = './make_initrd.sh %s' % arch
        if edge_cmd(cmd, initrd_path):
            EDGE_ERR('Build initrd failed, cmd: %s' % cmd)
            sys.exit(1)

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
        line += '            data = /incbin/("%s");\n' % dtb
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
        if compress_type:
            line += '            data = /incbin/("%s.%s");\n' % (image, compress_type)
            line += '            compression = "%s";\n' % compress_type
        else:
            line += '            data = /incbin/("%s");\n' % image
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
            line += '            data = /incbin/("%s/initrd-%s.img");\n' % (initrd_path, arch)
            line += '            type = "ramdisk";\n'
            line += '            arch = "%s";\n' % arch
            line += '            os = "linux";\n'
            line += '            compression = "%s";\n' % compress_type
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
        edge_cmd('rm -rf %s/initrd-%s.img %s/boot_its' % (initrd_path, arch, out_path), None)
        EDGE_INFO('Build %s successfully!' % target_name)

    def build_boot_fit(self):
        image,dtb = self.build_kernel('fit', None, None)
        self.build_fit_image('boot.img', image, dtb, False, None)
        self.build_fit_image('recovery.img', image, dtb, True, None)
        EDGE_INFO('Build boot fit successfully!')

    def build_boot_flash(self):
        image,dtb = self.build_kernel('flash', None, None)
        self.build_fit_image('boot.img', image, dtb, True, 'lzma')
        EDGE_INFO('Build boot flash successfully!')

    def build_boot_extlinux(self):
        image,dtb = self.build_kernel('extlinux', None, None)
        self.build_extlinux_image(image, dtb)
        self.build_fit_image('recovery.img', image, dtb, True, None)
        EDGE_INFO('Build boot extlinux successfully!')

    def build_kernel_module(self, build_args):
        module = build_args['module']
        image,dtb = self.build_kernel('module', None, module)
        EDGE_INFO('Build module %s successfully!' % module)

    def build_kernel_debpkg(self):
        image,dtb = self.build_kernel('debpkg', None, None)
        EDGE_INFO('Build debpkg successfully!')

    def build_guest(self):
        EDGE_DBG('Start build guest ...')

        EDGE_INFO('Build guest successfully!')
        
    def build_rootfs(self):
        conf = self.config.get()
        root_path = self.root_path
        out_path = conf['out_path']
        arch = conf['arch']
        chip = conf['chip']
        board = conf['board']
        rootfs_osname = conf['rootfs_osname']
        rootfs_version = conf['rootfs_version']
        rootfs_relver = conf['rootfs_relver']
        rootfs_type = conf['rootfs_type']
        rootfs_size = conf['rootfs_size']
        vendor_path = '%s/vendor/%s/%s' % (root_path, chip, board)
        rootfs_user = conf['rootfs_user']
        rootfs_password = conf['rootfs_password']
        rootfs_uuid = conf['rootfs_uuid']
        rootfs_apturl = conf['rootfs_apturl']
        rootfs_key = conf['rootfs_key']
        
        if arch == 'arm64':
            arch_alias = 'aarch64'
        else:
            arch_alias = 'arm'

        EDGE_DBG('Start build rootfs ...')

        chroot_bin = '/usr/sbin/chroot'
        if os.path.exists(chroot_bin) == False:
            EDGE_ERR('coreutils is not installed')
            sys.exit(1)

        rootfs_base = '%s/rootfs/images/%s/%s%s-base.tar.gz' % (root_path, arch, rootfs_osname, rootfs_version)
        rootfs_taskrel = '%s/rootfs/images/%s/%s%s-%s.tar.gz' % (root_path, arch, rootfs_osname, rootfs_version, rootfs_type.split('-')[0])
	
        build_new = True
        if os.path.exists('%s/rootfs.img' % out_path) == True:
            ret = input('\033[32m[EDGE INFO] Rootfs already exists, do you want to continue?[Y/n] \033[0m')
            if ret == 'N' or ret == 'n':
                EDGE_DBG('Remove old rootfs images and build a new one!')
            elif ret == 'Y' or ret == 'y':
                EDGE_DBG('Continue to build rootfs image!')
                build_new = False
            else:
                EDGE_ERR('Please input N/n to buid a new one or Y/y to continue!')
                sys.exit(1)

        if build_new == True:
            cmd = 'rm -rf rootfs.img;mkdir -p rootfs'
            edge_cmd(cmd, out_path)

            if os.path.isfile(rootfs_taskrel):
                cmd = 'tar zxvf %s -C .' % rootfs_taskrel
            else:
                cmd = 'tar zxvf %s -C .' % rootfs_base
            if edge_cmd(cmd, out_path):
                EDGE_ERR('Unzip rootfs failed')
                sys.exit(1)
        else:
            cmd = 'mkdir -p rootfs'
            edge_cmd(cmd, out_path)

        if rootfs_size == "auto":
            cmd = '%s/build/scripts/resize.sh %s/rootfs.img 20480M' % (root_path, out_path)
        else:
            cmd = '%s/build/scripts/resize.sh %s/rootfs.img %dM' % (root_path, out_path, int(rootfs_size))
        if edge_cmd(cmd, None) != 0:
            EDGE_ERR('Resize rootfs failed')
            sys.exit(1)

        cmd = 'sudo mount rootfs.img rootfs'
        if edge_cmd(cmd, out_path):
            EDGE_ERR('Mount rootfs failed')
            sys.exit(1)

        qemu_bin = '%s/build/scripts/qemu-%s-static' % (root_path, arch_alias)
        cmd = 'sudo cp %s rootfs/usr/bin' % qemu_bin
        edge_cmd(cmd, out_path)

        cmd = 'sudo cp -rf %s/vendor/common/pre-install %s/rootfs/' % (root_path, out_path)
        if edge_cmd(cmd, None) != 0:
            EDGE_ERR('Copy install failed')
            time.sleep(1)
            edge_cmd('sudo umount rootfs', out_path)
            sys.exit(1)

        cmd = 'sudo cp -rf %s/rootfs/%s/packages-local %s/rootfs/usr/share/' % (root_path, rootfs_osname, out_path)
        if edge_cmd(cmd, None) != 0:
            EDGE_ERR('Copy packages-local failed')
            time.sleep(1)
            edge_cmd('sudo umount rootfs', out_path)
            sys.exit(1)

        cmd = 'sudo cp -rf %s/vendor/%s/%s/pre-install/* %s/rootfs/pre-install/' % (root_path, chip, board, out_path)
        if edge_cmd(cmd, None) != 0:
            EDGE_ERR('Copy installi-custom failed')
            time.sleep(1)
            edge_cmd('sudo umount rootfs', out_path)
            sys.exit(1)
        cmd = 'sudo mkdir -p etc/chroot; echo \"rockchip,%s-chroot\" | sudo tee etc/chroot/compatible' % chip
        edge_cmd(cmd, '%s/rootfs' % out_path)

        cmd = '%s/build/scripts/rootfs-install.sh %s %s %s %s %s %s %s %s' % (root_path, rootfs_type, rootfs_user, rootfs_password, board, rootfs_osname, rootfs_relver, rootfs_apturl, rootfs_key)
        if edge_cmd(cmd, out_path):
            EDGE_ERR('Install rootfs package failed')
            time.sleep(1)
            edge_cmd('sudo umount rootfs', out_path)
            sys.exit(1)

        cmd = 'sudo rm -rf pre-install var/log/* root/.bash_history home/%s/.bash_history etc/chroot' % rootfs_user
        edge_cmd(cmd, '%s/rootfs' % out_path)

        if rootfs_size == "auto":
            msize = int(edge_cmd_result('sudo %s/build/scripts/getsize.sh %s/rootfs' % (root_path, out_path)))
            msize = msize + msize / 10

        time.sleep(1)
        edge_cmd("%s/build/scripts/kill_qemu.sh" % root_path, out_path)
        edge_cmd('sudo umount rootfs/dev', out_path)
        if edge_cmd('sudo umount rootfs', out_path):
            EDGE_ERR('umount rootfs failed')
            sys.exit(1)
        time.sleep(1)
        edge_cmd('rm -rf rootfs', out_path)

        if rootfs_size == "auto":
            # Auto-Resize rootfs.img
            EDGE_INFO('Resize root.img to %dM' % msize)
            cmd = '%s/build/scripts/resize.sh %s/rootfs.img %dM' % (root_path, out_path, msize)
            if edge_cmd(cmd, None) != 0:
                EDGE_ERR('Auto-Resize rootfs.img failed')
                sys.exit(1)

        if os.path.exists('%s/userdata.img' % out_path) == False:
            edge_cmd('dd if=/dev/zero of=userdata.img bs=4K count=1K; sudo mkfs.ext4 userdata.img', out_path)
        EDGE_INFO('Build rootfs successfully!')

    def build_update(self, isota, build_args):
        conf = self.config.get()
        root_path = self.root_path
        out_path = conf['out_path']
        chip = conf['chip']
        board = conf['board']
        bootmode = conf['bootmode']
        EDGE_DBG('Start build update ...')
        # Make package-file
        package_file = '%s/package-file' % out_path
        f = open(package_file, 'w+')
        line = 'package-file package-file\n'
        line += 'parameter parameter.txt\n'
        line += 'bootloader MiniLoaderAll.bin\n'
        if isota:
            imagename = 'update-ota.img'
            ota_list = build_args['ota'].split(',')
            for list in ota_list:
                line += '%s %s.img\n' % (list, list) 
        else:
            imagename = 'update.img'
            line += 'uboot uboot.img\n'
            if bootmode in ('extlinux'):
                line += 'misc misc.img\n'
                line += 'boot_linux boot_linux.img\n'
                line += 'recovery recovery.img\n'
                line += 'resource resource.img\n'
                line += 'rootfs rootfs.img\n'
            elif bootmode in ('fit'):
                line += 'misc misc.img\n'
                line += 'boot boot.img\n'
                line += 'recovery recovery.img\n'
                line += 'rootfs rootfs.img\n'
                if os.path.exists('%s/userdata.img' % out_path):
                    line += 'userdata userdata.img\n'
                if os.path.exists('%s/oem.img' % out_path):
                    line += 'oem oem.img\n'
            elif bootmode in ('flash'):
                line += 'vnvm RESERVED\n'
                line += 'boot boot.img\n'
            else:
                f.close()
                sys.exit(1)
        f.write(line)
        f.close()

        afptool = '%s/build/bin/afptool' % root_path
        cmd = '%s -pack . update.img.tmp' % afptool
        if edge_cmd(cmd, out_path):
            EDGE_ERR('afptool pack failed')
            sys.exit(1)

        rkImageMaker = '%s/build/bin/rkImageMaker' % root_path
        cmd = '%s -%s MiniLoaderAll.bin update.img.tmp %s -os_type:androidos' % (rkImageMaker, chip, imagename)
        if edge_cmd(cmd, out_path):
            EDGE_ERR('rkImageMaker pack failed')
            sys.exit(1)
        edge_cmd('rm -rf update.img.tmp package-file', out_path)
        EDGE_INFO('Build update successfully!')

    def build_secureboot_keys(self):
        conf = self.config.get()
        root_path = self.root_path
        uboot_path = '%s/uboot' % self.root_path
        rkbin_path = '%s/rkbin' % self.root_path

        EDGE_DBG('Start build secureboot keys ...')
        edge_cmd('mkdir -p keys', uboot_path)
        cmd = '%s/tools/rk_sign_tool kk --bits 2048' % rkbin_path
        if edge_cmd(cmd, uboot_path):
            EDGE_ERR('rk_sign_tool gernerate dev.key and ev.pubkey failed')
            sys.exit(1)
        edge_cmd('mv %s/rkbin/tools/private_key.pem keys/dev.key; mv %s/rkbin/tools/public_key.pem keys/dev.pubkey' % (root_path, root_path), uboot_path)

        cmd = 'openssl req -batch -new -x509 -key keys/dev.key --out keys/dev.crt'
        if edge_cmd(cmd, uboot_path):
            EDGE_ERR('openssl generate key.crt failed')
            sys.exit(1)

        EDGE_INFO('Build secureboot keys successfully!')
        
    def build(self, argv):
        conf = self.config.get()
        root_path =self.root_path
        out_path = conf['out_path']
        bootmode = conf['bootmode']
        rootfs_relver = conf['rootfs_relver']
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
            elif list == 'rootfs':
                self.build_rootfs()
            elif list == 'update':
                self.build_update(False, build_args)
            elif list == 'ota':
                self.build_update(True, build_args)
            elif list == 'androidboot':
                self.build_android_boot(build_args)
            elif list == 'module':
                self.build_kernel_module(build_args)
            elif list == 'sign':
                self.build_secureboot_keys()
            elif list == 'guest':
                self.build_guest()
            elif list == 'debpkg':
                self.build_kernel_debpkg()

        if os.path.exists('.repo') == True:
            edge_cmd('.repo/repo/repo manifest -r -o Edge_Release_V%s.xml' % rootfs_relver, None)
        EDGE_INFO('Build all successfully!')
        return 0
