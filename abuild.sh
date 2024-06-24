#!/bin/sh
mkdir -f abuild
OTHER_ARGS="-S .. -B ."
cd abuild && cmake \
    -DCMAKE_TOOLCHAIN_FILE=$NDK/build/cmake/android.toolchain.cmake \
    -DANDROID_ABI=arm64-v8a \
    -DANDROID_PLATFORM=android-$MINSDKVERSION \
    $OTHER_ARGS
