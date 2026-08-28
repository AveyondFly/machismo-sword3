#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
SOURCE="$ROOT/extern/llvm-project/runtimes"
BUILD_DIR=${LIBCXX_BUILD_DIR:-"$ROOT/build-libcxx-rocknix"}
TOOLCHAIN_FILE=${TOOLCHAIN_FILE:-"$ROOT/cmake/rocknix-rk3326.cmake"}
TOOLCHAIN_ROOT=${ROCKNIX_TOOLCHAIN_ROOT:-/home/ubuntu/distribution/build.ROCKNIX-RK3326.aarch64/toolchain}
CC="$TOOLCHAIN_ROOT/bin/aarch64-rocknix-linux-gnu-gcc"
AR="$TOOLCHAIN_ROOT/bin/aarch64-rocknix-linux-gnu-ar"
COMPAT_OBJECT="$BUILD_DIR/gcc_cxa_compat.o"
COMPAT_ARCHIVE="$BUILD_DIR/libgcc_cxa_compat.a"

if [ ! -d "$SOURCE/../libcxx" ]; then
    echo "Missing extern/llvm-project; initialize that submodule first." >&2
    exit 1
fi

mkdir -p "$BUILD_DIR"
"$CC" -O2 -fPIC -c "$ROOT/src/shim/gcc_cxa_compat.c" -o "$COMPAT_OBJECT"
"$AR" rcs "$COMPAT_ARCHIVE" "$COMPAT_OBJECT"

cmake -G Ninja -S "$SOURCE" -B "$BUILD_DIR" \
    -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN_FILE" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_CXX_STANDARD_LIBRARIES="$COMPAT_ARCHIVE -lgcc -lgcc_s" \
    -DLLVM_ENABLE_RUNTIMES="libcxx;libcxxabi" \
    -DLIBCXXABI_USE_LLVM_UNWINDER=OFF \
    -DLIBCXX_ABI_VERSION=1 \
    -DLIBCXX_ABI_DEFINES="_LIBCPP_ABI_ALTERNATE_STRING_LAYOUT;_LIBCPP_ABI_DARWIN_MBSTATE_COMPAT" \
    -DLIBCXX_ENABLE_SHARED=ON \
    -DLIBCXX_ENABLE_STATIC=OFF \
    -DLIBCXX_INCLUDE_TESTS=OFF \
    -DLIBCXX_INCLUDE_BENCHMARKS=OFF \
    -DLIBCXXABI_ENABLE_SHARED=ON \
    -DLIBCXXABI_ENABLE_STATIC=OFF \
    -DLIBCXXABI_INCLUDE_TESTS=OFF

cmake --build "$BUILD_DIR" --target cxx cxxabi --parallel "${JOBS:-2}"
file "$BUILD_DIR/lib/libc++.so.1" "$BUILD_DIR/lib/libc++abi.so.1"
