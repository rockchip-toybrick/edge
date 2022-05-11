#!/bin/bash
PRIVATE_MODULE_DIR=./vendor/lib/modules
PRIVATE_LOAD_FILE=./res/vendor_modules.load
TEMP_MODULES_PATH=./temp/lib/modules/0.0
KERNEL_DRIVERS_PATH=../kernel/drivers
VENDOR_MODULES_CONFIG=./vendor/etc/init.insmod.cfg

export PATH=./bin:./ext4_utils:$PATH

echo "==========================================="
echo "Preparing temp dirs and use placeholder 0.0..."
if [ -f $VENDOR_MODULES_CONFIG ]; then
  rm -rf $VENDOR_MODULES_CONFIG
fi
if [ -d temp ]; then
  rm -rf temp
fi
if [ -d $PRIVATE_MODULE_DIR ]; then
  rm -rf $PRIVATE_MODULE_DIR
fi
if [ -f out/vendor.img ]; then
  rm -rf out/vendor.img
fi
if [ ! -d out ]; then
  mkdir -p out
fi
mkdir -p $TEMP_MODULES_PATH
mkdir -p $PRIVATE_MODULE_DIR
echo "Prepare temp dirs done."
echo "==========================================="
echo -e "\033[33mRead modules list from $PRIVATE_LOAD_FILE\033[0m"
echo "==========================================="
modules_vendor_array=($(cat $PRIVATE_LOAD_FILE))
for MODULE in "${modules_vendor_array[@]}"
do
  echo "Copying $MODULE..."
  module_file=($(find $KERNEL_DRIVERS_PATH -name $MODULE))
  cp $module_file $TEMP_MODULES_PATH/
  cp $module_file $PRIVATE_MODULE_DIR/
  echo "insmod /vendor/lib/modules/$MODULE" >> $VENDOR_MODULES_CONFIG
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
echo "making vendor image..."
build_image ./vendor ./res/vendor_image_info.txt ./out/vendor.img ./system
echo "make vendor image done."
echo "==========================================="
