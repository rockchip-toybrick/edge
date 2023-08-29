cmake_minimum_required (VERSION 2.8)
project (rkcrypto C)

################################################################################
# toolchain config
################################################################################
set (CMAKE_SYSTEM_NAME Linux)

if (BUILD STREQUAL 64)
	set (CMAKE_SYSTEM_PROCESSOR aarch64)
else ()
	set(CMAKE_SYSTEM_PROCESSOR arm)
endif ()

set (TOOLCHAIN_PREBUILTS "${CMAKE_CURRENT_SOURCE_DIR}/../../prebuilts")
set (TOOLCHAIN_PATH_ARM32 "gcc/linux-x86/arm/gcc-arm-10.3-2021.07-x86_64-arm-none-linux-gnueabihf/bin")
set (TOOLCHAIN_PATH_AARCH64 "gcc/linux-x86/aarch64/gcc-arm-10.3-2021.07-x86_64-aarch64-none-linux-gnu/bin")
set (CROSS_COMPILE32 "arm-none-linux-gnueabihf-gcc")
set (CROSS_COMPILE64 "aarch64-none-linux-gnu-gcc")
set (CROSS_COMPILE32_GXX "arm-none-linux-gnueabihf-g++")
set (CROSS_COMPILE64_GXX "aarch64-none-linux-gnu-g++")
set (AARCH64_TOOLCHAIN_GCC "${TOOLCHAIN_PREBUILTS}/${TOOLCHAIN_PATH_AARCH64}/${CROSS_COMPILE64}")
set (AARCH64_TOOLCHAIN_GXX "${TOOLCHAIN_PREBUILTS}/${TOOLCHAIN_PATH_AARCH64}/${CROSS_COMPILE64_GXX}")
set (ARM32_TOOLCHAIN_GCC "${TOOLCHAIN_PREBUILTS}/${TOOLCHAIN_PATH_ARM32}/${CROSS_COMPILE32}")
set (ARM32_TOOLCHAIN_GXX "${TOOLCHAIN_PREBUILTS}/${TOOLCHAIN_PATH_ARM32}/${CROSS_COMPILE32_GXX}")

if (CMAKE_SYSTEM_PROCESSOR STREQUAL aarch64)
	set (CMAKE_C_COMPILER ${AARCH64_TOOLCHAIN_GCC})
	set (CMAKE_CXX_COMPILER ${AARCH64_TOOLCHAIN_GXX})
	set (ARM_PLATFORM "arm64")
else ()
	set (CMAKE_C_COMPILER ${ARM32_TOOLCHAIN_GCC})
	set (CMAKE_CXX_COMPILER ${ARM32_TOOLCHAIN_GXX})
	set (ARM_PLATFORM "arm")
endif ()

################################################################################
# compile flags
################################################################################
add_compile_options(-Wall -Werror)
add_compile_options(-Wno-unused-function)
add_compile_options(-Wno-unused-parameter)
add_compile_options(-Wno-format-truncation)
add_compile_options(-Wno-maybe-uninitialized)

if(NOT CMAKE_BUILD_TYPE)
	set(CMAKE_BUILD_TYPE "Release" CACHE STRING
	"Choose the type of build, options are: Debug Release RelWithDebInfo MinSizeRel."
	FORCE)
endif(NOT CMAKE_BUILD_TYPE)

################################################################################
# librkcrypto files
################################################################################
include_directories(include)
file(GLOB SOURCES "src/*.c")

################################################################################
# libdrm dependencies
################################################################################
include_directories(third_party/libdrm/include)
include_directories(third_party/libdrm/include/drm)
file(GLOB SOURCES_DRM "third_party/libdrm/src/*.c")
add_definitions(-DMAJOR_IN_SYSMACROS=1 -D_GNU_SOURCE)

################################################################################
# libteec dependencies
################################################################################
add_definitions(-DBINARY_PREFIX=\"TEEC\")
set(TEEC_PATH third_party/optee_client/libteec)
include_directories(${TEEC_PATH}/../public ${TEEC_PATH}/include)
file(GLOB SOURCES_TEEC
	${TEEC_PATH}/src/tee_client_api.c
	${TEEC_PATH}/src/teec_trace.c)

################################################################################
# build librkcrypto shared library
################################################################################
add_library(rkcrypto SHARED ${SOURCES} ${SOURCES_DRM} ${SOURCES_TEEC})

################################################################################
# build librkcrypto static library
################################################################################
add_library(rkcrypto_static STATIC ${SOURCES} ${SOURCES_DRM} ${SOURCES_TEEC})
SET_TARGET_PROPERTIES (rkcrypto_static PROPERTIES OUTPUT_NAME "rkcrypto")

################################################################################
# build other components
################################################################################
add_subdirectory(test)
