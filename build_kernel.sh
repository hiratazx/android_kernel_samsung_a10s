#!/bin/bash

export CROSS_COMPILE=/home/starlix/android_kernel_samsung_a10s/aarch64-linux-gnu/bin/aarch64-linux-android-
export CC=/home/starlix/android_kernel_samsung_a10s/clang-9
export CLANG_TRIPLE=/home/starlix/android_kernel_samsung_a10s/aarch64-linux-android/bin/aarch64-linux-gnu-
export ARCH=arm64

export KCFLAGS=-w
export CONFIG_SECTION_MISMATCH_WARN_ONLY=y

make -C $(pwd) O=$(pwd)/out KCFLAGS=-w CONFIG_SECTION_MISMATCH_WARN_ONLY=y a10s_defconfig
make -C $(pwd) O=$(pwd)/out KCFLAGS=-w CONFIG_SECTION_MISMATCH_WARN_ONLY=y -j16
