#!/bin/bash
KERNEL_DRIVERS_PATH=../kernel-5.10
KERNEL_IMAGE=$KERNEL_DRIVERS_PATH/arch/arm64/boot/Image
RAMDISK_DIR=./vendor_ramdisk
PRIVATE_MODULE_DIR=$RAMDISK_DIR/lib/modules
PRIVATE_LOAD_FILE=./res/ramdisk_modules.load
TEMP_MODULES_PATH=./temp/lib/modules/0.0
VENDOR_RAMDISK_FILE=out/vendor_ramdisk.cpio.gz
VENDOR_BOOT_FILE=out/vendor_boot.img

readonly OBJCOPY_BIN=llvm-objcopy
readonly USE_STRIP=1

if [ ! -n "$1" ]; then
  DTB_PATH=$KERNEL_DRIVERS_PATH/arch/arm64/boot/dts/rockchip/rk3566-rk817-tablet.dtb
else
  DTB_PATH=$KERNEL_DRIVERS_PATH/arch/arm64/boot/dts/rockchip/$1.dtb
fi

export PATH=$PATH:./bin

# $1 origin path
# $2 target path
objcopy() {
    if [ ! -f $1 ]; then
        echo "NOT FOUND!"
        return
    fi
    local module_name=`basename -a $1`
    local OBJCOPY_ARGS=""
    if [ $USE_STRIP = "1" ]; then
        OBJCOPY_ARGS="--strip-debug"
    fi
    $OBJCOPY_BIN $OBJCOPY_ARGS $1 $2$module_name
}

clean_file() {
    if [ -f $1 ]; then
        echo "cleaning file $1"
        rm -rf $1
    fi
    if [ -d $1 ]; then
        echo "cleaning dir $1"
        rm -rf $1
    fi
}

create_dir() {
    if [ ! -d $1 ]; then
        mkdir -p $1
    fi
}

echo "==========================================="
echo "Preparing temp dirs and use placeholder 0.0..."
clean_file temp
clean_file $PRIVATE_MODULE_DIR
clean_file $VENDOR_BOOT_FILE
clean_file $VENDOR_RAMDISK_FILE
create_dir system
create_dir out
create_dir $TEMP_MODULES_PATH
create_dir $PRIVATE_MODULE_DIR
echo "Prepare temp dirs done."
echo "==========================================="
echo -e "\033[33mRead modules list from $PRIVATE_LOAD_FILE\033[0m"
echo -e "\033[33mUse DTS as $DTB_PATH\033[0m"
echo "==========================================="
modules_ramdisk_array=($(cat $PRIVATE_LOAD_FILE))
for MODULE in "${modules_ramdisk_array[@]}"
do
  echo "Copying $MODULE..."
  module_file=($(find $KERNEL_DRIVERS_PATH -name $MODULE))
  objcopy $module_file $TEMP_MODULES_PATH/
  objcopy $module_file $PRIVATE_MODULE_DIR/
done
echo "==========================================="
echo "Generating depmod..."
depmod -b temp 0.0
echo "Generate depmod done."

cp $TEMP_MODULES_PATH/modules.alias $PRIVATE_MODULE_DIR/
cp $PRIVATE_LOAD_FILE $PRIVATE_MODULE_DIR/modules.load
cp $TEMP_MODULES_PATH/modules.dep $PRIVATE_MODULE_DIR/
cp $TEMP_MODULES_PATH/modules.softdep $PRIVATE_MODULE_DIR/

echo "==========================================="
echo "making vendor_ramdisk..."
mkbootfs -d ./system $RAMDISK_DIR | minigzip > $VENDOR_RAMDISK_FILE
echo "make vendor_ramdisk done."

echo "==========================================="
echo "making vendor_boot image..."
mkbootimg --dtb $DTB_PATH --vendor_cmdline "console=ttyFIQ0 androidboot.first_stage_console=1 androidboot.baseband=N/A androidboot.wificountrycode=US androidboot.veritymode=enforcing androidboot.hardware=rk30board androidboot.console=ttyFIQ0 androidboot.verifiedbootstate=orange firmware_class.path=/vendor/etc/firmware init=/init rootwait ro loop.max_part=7 androidboot.boot_devices=fe310000.sdhci,fe330000.nandc androidboot.selinux=permissive buildvariant=userdebug" --header_version 3 --vendor_ramdisk $VENDOR_RAMDISK_FILE --vendor_boot $VENDOR_BOOT_FILE
echo "make vendor_boot image done."
echo "==========================================="
