#!/bin/sh
# ABI choices are x86 x86_64 arm64-v8a armeabi-v7a
mkdir -p abuild
OTHER_ARGS="-S .. -B ."
cd abuild && cmake \
    -DCMAKE_TOOLCHAIN_FILE=$NDK/build/cmake/android.toolchain.cmake \
    -DANDROID_ABI=$1 \
    -DANDROID_PLATFORM=android-$MINSDKVERSION \
    $OTHER_ARGS
