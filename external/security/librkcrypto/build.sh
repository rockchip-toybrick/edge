#/bin/bash

set -e

collect_bin()
{
	cp $BUILD_DIR/librkcrypto.so $TARGET_LIB_DIR
	cp $BUILD_DIR/librkcrypto.a $TARGET_LIB_DIR
	cp $BUILD_DIR/test/librkcrypto_test $TARGET_BIN_DIR
	echo "copy target files to $TARGET_LIB_DIR success"

	# export head files
	cp $SCRIPT_DIR/include/rkcrypto_otp_key.h $TARGET_INCLUDE_DIR
	cp $SCRIPT_DIR/include/rkcrypto_common.h $TARGET_INCLUDE_DIR
	cp $SCRIPT_DIR/include/rkcrypto_core.h $TARGET_INCLUDE_DIR
	cp $SCRIPT_DIR/include/rkcrypto_mem.h $TARGET_INCLUDE_DIR
	echo "copy head files to $TARGET_INCLUDE_DIR success"
}

build()
{
	echo "build $ARM_BIT libraries and binaries"
	TARGET_LIB_DIR=$SCRIPT_DIR/out/target/lib/$ARM_BIT/
	TARGET_BIN_DIR=$SCRIPT_DIR/out/target/bin/$ARM_BIT/
	TARGET_INCLUDE_DIR=$SCRIPT_DIR/out/target/include/
	BUILD_DIR=$SCRIPT_DIR/out/build/$ARM_BIT/
	mkdir -p $TARGET_LIB_DIR
	mkdir -p $TARGET_BIN_DIR
	mkdir -p $TARGET_INCLUDE_DIR
	mkdir -p $BUILD_DIR
	cd $BUILD_DIR
	cmake $SCRIPT_DIR $DBUILD
	make -j12
}

BUILD_PARA="$1"
SCRIPT_DIR=$(pwd)

if [ $# -eq 0 ]; then
	# build both 32-bit and 64-bit
	DBUILD="-DBUILD=32"
	ARM_BIT="arm"
	build
	collect_bin

	DBUILD="-DBUILD=64"
	ARM_BIT="arm64"
	build
	collect_bin
else
	if [ $BUILD_PARA == "32" ]; then
		DBUILD="-DBUILD=32"
		ARM_BIT="arm"
	else
		DBUILD="-DBUILD=64"
		ARM_BIT="arm64"
	fi

	build
	collect_bin
fi
