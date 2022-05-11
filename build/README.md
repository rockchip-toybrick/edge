# Edge Usage

## 1 Help

```
toybrick@debian11:~/work/edge$ ./edge

[EDGE DEBUG] Usage: edge {flash, build, set, env}
[EDGE DEBUG]
[EDGE DEBUG] Edge Compute SDK Build System Version: 0.1.0
[EDGE DEBUG]
[EDGE DEBUG] Arguments:
[EDGE DEBUG]   flash                    Flash images
[EDGE DEBUG]   build                    Build source code
[EDGE DEBUG]   set                      Set build env
[EDGE DEBUG]   env                      Show build env
```

## 2 Build environment

### 2.1. Set enviroment

```
toybrick@debian11:~/work/edge$ ./edge set

[EDGE DEBUG] Board list:
> rk3588
  0. RK3588-EVB1
  1. TB-RK3588X0
> rk3568
  2. TB-RK3568X0
Enter the number of the board: 0
```

### 2.2. Show enviroment

```
toybrick@debian11:~/work/edge$ ./edge env

[EDGE DEBUG] root path: /home/toybrick/work/edge
[EDGE DEBUG] out path: /home/toybrick/work/edge/out/rk3588/RK3588-EVB1/images
[EDGE DEBUG] board: RK3588-EVB1
[EDGE DEBUG] chip: rk3588
[EDGE DEBUG] arch: arm64
[EDGE DEBUG] > Partition:
[EDGE DEBUG]   uboot: ['0x00002000', '0x00002000']
[EDGE DEBUG]   trust: ['0x00004000', '0x00002000']
[EDGE DEBUG]   resource: ['0x00006000', '0x00002000']
[EDGE DEBUG]   boot_linux:bootable: ['0x00008000', '0x00030000']
[EDGE DEBUG]   rootfs:grow: ['0x00038000', '-']
[EDGE DEBUG] > Uboot:
[EDGE DEBUG]   config: rk3588-toybrick
[EDGE DEBUG] > Kernel:
[EDGE DEBUG]   version: 5.10
[EDGE DEBUG]   uuid: a2d37d82-51e0-420d-83f5-470db993dd35
[EDGE DEBUG]   config: rockchip_linux_defconfig
[EDGE DEBUG]   dtbname: rk3588-evb1-lp4-v10-linux
[EDGE DEBUG]   initrd: True
[EDGE DEBUG]   debug: 0xfeb50000
[EDGE DEBUG] > Rootfs:
[EDGE DEBUG]   osname: debian
[EDGE DEBUG]   version: 11
[EDGE DEBUG]   alias: bullseye
[EDGE DEBUG]   type: gnome
[EDGE DEBUG]   uuid: 614e0000-0000-4b53-8000-1d28000054a9
[EDGE DEBUG]   user: rockchip
[EDGE DEBUG]   size: 5G
```

## 3. Build Source Code

### 3.1. Build Help

```
toybrick@debian11:~/work/edge$ ./edge build

[EDGE DEBUG] Usage: edge build [options]
[EDGE DEBUG]
[EDGE DEBUG] Options:
[EDGE DEBUG]   -h, --help               Show this help message and exit
[EDGE DEBUG]   -p, --parameter          Build parameter(parameter.txt)
[EDGE DEBUG]   -u, --uboot              Build uboot(MiniLoader.bin, uboot.bin)
[EDGE DEBUG]   -k, --kernel             Build kernel(source.img, boot_linux.img)
[EDGE DEBUG]   -r, --rootfs             Build rootfs(rootfs.img)
[EDGE DEBUG]   -U, --update             Build update(update.img)
[EDGE DEBUG]   -P, --patch              Apply all patches in patches directory
[EDGE DEBUG]   -a, --all                Build all images
[EDGE DEBUG]
[EDGE DEBUG] e.g.
[EDGE DEBUG]   edge build -kP             Apply all kernel patches and build it
[EDGE DEBUG]   edge build -a
[EDGE DEBUG]
```

### 3.2. Build parameter

```
toybrick@debian11:~/work/edge$ ./edge build -p

[EDGE DEBUG] mkdir -p /home/toybrick/work/edge/out/rk3588/RK3588-EVB1/images
[EDGE DEBUG] rm -rf /home/toybrick/work/edge/out/Images
[EDGE DEBUG] ln -s /home/toybrick/work/edge/out/rk3588/RK3588-EVB1/images /home/toybrick/work/edge/out/Images
[EDGE DEBUG] Start build parameter ...
[EDGE DEBUG] Build parameter successfully!
[EDGE INFO] Build all successfully!
```

### 3.3. Build uboot

```
toybrick@debian11:~/work/edge$ ./edge build -u

[EDGE DEBUG] mkdir -p /home/toybrick/work/edge/out/rk3588/RK3588-EVB1/images
[EDGE DEBUG] rm -rf /home/toybrick/work/edge/out/Images
[EDGE DEBUG] ln -s /home/toybrick/work/edge/out/rk3588/RK3588-EVB1/images /home/toybrick/work/edge/out/Images
[EDGE DEBUG] Start build uboot ...
[EDGE DEBUG] ./make.sh rk3588-toybrick
......
[EDGE DEBUG] cp /home/toybrick/work/edge/uboot/uboot.img /home/toybrick/work/edge/out/rk3588/RK3588-EVB1/images/
[EDGE DEBUG] cp /home/toybrick/work/edge/uboot/rk3588_*.bin /home/toybrick/work/edge/out/rk3588/RK3588-EVB1/images/MiniLoaderAll.bin
[EDGE DEBUG] Build uboot successfully!
[EDGE INFO] Build all successfully!
```

### 3.4 Build kernel

```
toybrick@debian11:~/work/edge$ ./edge build -k

[EDGE DEBUG] mkdir -p /home/toybrick/work/edge/out/rk3588/RK3588-EVB1/images
[EDGE DEBUG] rm -rf /home/toybrick/work/edge/out/Images
[EDGE DEBUG] ln -s /home/toybrick/work/edge/out/rk3588/RK3588-EVB1/images /home/toybrick/work/edge/out/Images
[EDGE DEBUG] Start build kernel ...
[EDGE DEBUG] make CROSS_COMPILE=/home/toybrick/work/edge/prebuilts/gcc/linux-x86/aarch64/gcc-linaro-6.3.1-2017.05-x86_64_aarch64-linux-gnu/bin/aarch64-linux-gnu- LLVM=1 LLVM_IAS=1 -j 4 ARCH=arm64 rockchip_linux_defconfig
[EDGE DEBUG] make CROSS_COMPILE=/home/toybrick/work/edge/prebuilts/gcc/linux-x86/aarch64/gcc-linaro-6.3.1-2017.05-
......
[EDGE DEBUG] rm -rf /home/toybrick/work/edge/kernel/linux-5.10/boot_linux; mkdir -p /home/toybrick/work/edge/kernel/linux-5.10/boot_linux/extlinux
[EDGE DEBUG] cp /home/toybrick/work/edge/kernel/linux-5.10/arch/arm64/boot/dts/rockchip/rk3588-evb1-lp4-v10-linux.dtb /home/toybrick/work/edge/kernel/linux-5.10/boot_linux/extlinux/toybrick.dtb
[EDGE DEBUG] cp /home/toybrick/work/edge/kernel/linux-5.10/arch/arm64/boot/Image /home/toybrick/work/edge/kernel/linux-5.10/boot_linux/extlinux/
[EDGE DEBUG] cp /home/toybrick/work/edge/rootfs/images/arm64/initrd.img /home/toybrick/work/edge/kernel/linux-5.10/boot_linux/
[EDGE DEBUG] genext2fs -B 4096 -b 16384 -d /home/toybrick/work/edge/kernel/linux-5.10/boot_linux -i 8192 -U /home/toybrick/work/edge/kernel/linux-5.10/boot_linux.img
copying from directory /home/toybrick/work/edge/kernel/linux-5.10/boot_linux
[EDGE DEBUG] cp /home/toybrick/work/edge/kernel/linux-5.10/boot_linux.img /home/toybrick/work/edge/out/rk3588/RK3588-EVB1/images/
[EDGE DEBUG] cp /home/toybrick/work/edge/kernel/linux-5.10/resource.img /home/toybrick/work/edge/out/rk3588/RK3588-EVB1/images/
[EDGE DEBUG] Build kernel successfully!
[EDGE INFO] Build all successfully!
```

### 3.5 Apply all kernel patches and build it

```
toybrick@debian11:~/work/edge$ ./edge build -kP

[EDGE DEBUG] mkdir -p /home/toybrick/work/edge/out/rk3588/RK3588-EVB1/images
[EDGE DEBUG] rm -rf /home/toybrick/work/edge/out/Images
[EDGE DEBUG] ln -s /home/toybrick/work/edge/out/rk3588/RK3588-EVB1/images /home/toybrick/work/edge/out/Images
[EDGE DEBUG] Start build kernel ...
[EDGE DEBUG] git reset --hard 44f46d2c0657b7b205ba337dac32b592ec5d88ef
HEAD is now at 44f46d2c0657 arm64: dts: rockchip: add rk3588 dmc relate node
[EDGE DEBUG] git am /home/toybrick/work/edge/patches/kernel/linux-5.10/0001-arm64-boot-dts-rk3588-linux-modify-bootarg-for-extli.patch
Applying: arm64: boot: dts: rk3588-linux: modify bootarg for extlinux
[EDGE DEBUG] git am /home/toybrick/work/edge/patches/kernel/linux-5.10/0003-arm64-dts-rockchip-add-dts-for-RK3588-IR88MX01.patch
Applying: arm64: dts: rockchip: add dts for RK3588-IR88MX01
[EDGE DEBUG] git am /home/toybrick/work/edge/patches/kernel/linux-5.10/0004-arm64-dts-rockchip-add-dts-for-TB-RK3568X.patch
Applying: arm64: dts: rockchip: add dts for TB-RK3568X
[EDGE DEBUG] git am /home/toybrick/work/edge/patches/kernel/linux-5.10/0005-arm64-dts-rockchip-add-dts-for-TB-RK3588X.patch
Applying: arm64: dts: rockchip: add dts for TB-RK3588X
[EDGE DEBUG] git am /home/toybrick/work/edge/patches/kernel/linux-5.10/0002-arm64-dts-rockchip-rk3588-evb1-lp4-disable-dsi0.patch
Applying: arm64: dts: rockchip: rk3588-evb1-lp4: disable dsi0
[EDGE DEBUG] make CROSS_COMPILE=/home/toybrick/work/edge/prebuilts/gcc/linux-x86/aarch64/gcc-linaro-6.3.1-2017.05-x86_64_aarch64-linux-gnu/bin/aarch64-lin                                                        ux-gnu- LLVM=1 LLVM_IAS=1 -j 4 ARCH=arm64 rockchip_linux_defconfig
......
[EDGE DEBUG] make CROSS_COMPILE=/home/toybrick/work/edge/prebuilts/gcc/linux-x86/aarch64/gcc-linaro-6.3.1-2017.05-x86_64_aarch64-linux-gnu/bin/aarch64-lin                                                        ux-gnu- LLVM=1 LLVM_IAS=1 -j 4 ARCH=arm64 rk3588-evb1-lp4-v10-linux.img
......
[EDGE DEBUG] rm -rf /home/toybrick/work/edge/kernel/linux-5.10/boot_linux; mkdir -p /home/toybrick/work/edge/kernel/linux-5.10/boot_linux/extlinux
[EDGE DEBUG] cp /home/toybrick/work/edge/kernel/linux-5.10/arch/arm64/boot/dts/rockchip/rk3588-evb1-lp4-v10-linux.dtb /home/toybrick/work/edge/kernel/linux-5.                                                        10/boot_linux/extlinux/toybrick.dtb
[EDGE DEBUG] cp /home/toybrick/work/edge/kernel/linux-5.10/arch/arm64/boot/Image /home/toybrick/work/edge/kernel/linux-5.10/boot_linux/extlinux/
[EDGE DEBUG] cp /home/toybrick/work/edge/rootfs/images/arm64/initrd.img /home/toybrick/work/edge/kernel/linux-5.10/boot_linux/
[EDGE DEBUG] genext2fs -B 4096 -b 16384 -d /home/toybrick/work/edge/kernel/linux-5.10/boot_linux -i 8192 -U /home/toybrick/work/edge/kernel/linux-5.10/boot_li                                                        nux.img
copying from directory /home/toybrick/work/edge/kernel/linux-5.10/boot_linux
[EDGE DEBUG] cp /home/toybrick/work/edge/kernel/linux-5.10/boot_linux.img /home/toybrick/work/edge/out/rk3588/RK3588-EVB1/images/
[EDGE DEBUG] cp /home/toybrick/work/edge/kernel/linux-5.10/resource.img /home/toybrick/work/edge/out/rk3588/RK3588-EVB1/images/
[EDGE DEBUG] Build kernel successfully!
[EDGE INFO] Build all successfully!
```

### 3.6 Build rootfs

```
toybrick@debian11:~/work/edge$ ./edge build -r

[EDGE DEBUG] mkdir -p /home/toybrick/work/edge/out/rk3588/RK3588-EVB1/images
[EDGE DEBUG] rm -rf /home/toybrick/work/edge/out/Images
[EDGE DEBUG] ln -s /home/toybrick/work/edge/out/rk3588/RK3588-EVB1/images /home/toybrick/work/edge/out/Images
[EDGE DEBUG] Start build rootfs ...
[EDGE DEBUG] rm -rf /home/toybrick/work/edge/out/rk3588/RK3588-EVB1/images/rootfs.img
[EDGE DEBUG] ln -s /home/toybrick/work/edge/rootfs/images/arm64/debian11-gnome.img /home/toybrick/work/edge/out/rk3588/RK3588-EVB1/images/rootfs.img
[EDGE DEBUG] Build rootfs successfully!
[EDGE INFO] Build all successfully!
```

### 3.7 Build update

```
toybrick@debian11:~/work/edge$ ./edge build -U

[EDGE DEBUG] mkdir -p /home/toybrick/work/edge/out/rk3588/RK3588-EVB1/images
[EDGE DEBUG] rm -rf /home/toybrick/work/edge/out/Images
[EDGE DEBUG] ln -s /home/toybrick/work/edge/out/rk3588/RK3588-EVB1/images /home/toybrick/work/edge/out/Images
[EDGE DEBUG] Start build update ...
[EDGE DEBUG] /home/toybrick/work/edge/build/bin/afptool -pack . update.img.tmp
......
[EDGE DEBUG] /home/toybrick/work/toybrick/edge/build/bin/rkImageMaker -rk3588 MiniLoaderAll.bin update.img.tmp update.img -os_type:androidos
......
[EDGE DEBUG] rm -rf update.img.tmp package-file
[EDGE DEBUG] Build update successfully!
[EDGE INFO] Build all successfully!
```

### 3.8 Build all

```
toybrick@debian11:~/work/edge$ ./edge build -a
```

### 3.9 Apply all patches and build them

```
toybrick@debian11:~/work/edge$ ./edge build -aP
```

## 4. Flash images

### 4.1 Flash help

```
toybrick@debian11:~/work/edge$ ./edge flash

[EDGE DEBUG] Usage: edge flash [options]
[EDGE DEBUG]
[EDGE DEBUG] Options:
[EDGE DEBUG]   -h, --help               Show this help message and exit
[EDGE DEBUG]   -q, --query              Query board flash mode(none, loader, maskrom)
[EDGE DEBUG]   -u, --uboot              Build uboot(MiniLoader.bin, uboot.bin)
[EDGE DEBUG]   -k, --kernel             Build kernel(source.img, boot_linux.img)
[EDGE DEBUG]   -r, --rootfs             Build rootfs(rootfs.img)
[EDGE DEBUG]   -a, --all                Build all images
[EDGE DEBUG]
[EDGE DEBUG] e.g.
[EDGE DEBUG]   edge build -uk
[EDGE DEBUG]   edge build -a
[EDGE DEBUG]
```

### 4.2 Flash uboot

```
toybrick@debian11:~/work/edge$ ./edge flash -u
```

### 4.3 Flash kernel

```
toybrick@debian11:~/work/edge$ ./edge flash -k
```

### 4.4 Flash rootfs

```
toybrick@debian11:~/work/edge$ ./edge flash -r
```

### 4.5 Flash all

```
toybrick@debian11:~/work/edge$ ./edge flash -a
```

