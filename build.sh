#!/usr/bin/env bash
# Build libmodmenu.so for arm64-v8a.
# Requires: Android NDK r26+ and cmake/ninja on PATH.

set -e

: "${ANDROID_NDK_HOME:?set ANDROID_NDK_HOME to your NDK root}"
ABI=arm64-v8a
API=24
OUT=build/$ABI

cmake -S . -B "$OUT" \
  -DCMAKE_TOOLCHAIN_FILE="$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI="$ABI" \
  -DANDROID_PLATFORM=android-$API \
  -DANDROID_STL=c++_static \
  -DCMAKE_BUILD_TYPE=Release

cmake --build "$OUT" -j"$(nproc)"

echo "built: $OUT/lib/$ABI/libmodmenu.so"
