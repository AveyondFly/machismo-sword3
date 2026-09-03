#!/bin/sh
# Apple-ABI libc++ for EmuELEC / old glibc. Compile with GCC 11 against the
# Linaro 6.3.1 sysroot; Linaro g++ 6.3 cannot build this libc++ (C++20).
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT=$(CDPATH= cd -- "$ROOT/.." && pwd)
SOURCE="$ROOT/extern/llvm-project/runtimes"
BUILD_DIR=${LIBCXX_BUILD_DIR:-"$ROOT/build-libcxx-linaro"}
TOOLCHAIN_FILE=${TOOLCHAIN_FILE:-"$ROOT/cmake/linaro-libcxx.cmake"}
LINARO_ROOT=${LINARO_ROOT:-/opt/toolchains/gcc-linaro-6.3.1-2017.05-x86_64_aarch64-linux-gnu}

if [ ! -d "$SOURCE/../libcxx" ]; then
    echo "Missing extern/llvm-project; initialize that submodule first." >&2
    exit 1
fi

cmake -G Ninja -S "$SOURCE" -B "$BUILD_DIR" \
    -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN_FILE" \
    -DCMAKE_BUILD_TYPE=Release \
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

# EmuELEC aarch64 has no libatomic.so.1; ship Linaro's (GLIBC_2.17).
cp -a "$LINARO_ROOT/aarch64-linux-gnu/libc/lib/libatomic.so.1.2.0" \
    "$BUILD_DIR/lib/libatomic.so.1.2.0"
ln -sfn libatomic.so.1.2.0 "$BUILD_DIR/lib/libatomic.so.1"
ln -sfn libatomic.so.1.2.0 "$BUILD_DIR/lib/libatomic.so"

READELF=$LINARO_ROOT/bin/aarch64-linux-gnu-readelf
OBJDUMP=$LINARO_ROOT/bin/aarch64-linux-gnu-objdump
for bin in "$BUILD_DIR/lib/libc++.so.1" "$BUILD_DIR/lib/libc++abi.so.1"; do
    echo "== $(basename "$bin") .comment =="
    "$READELF" -p .comment "$bin" || true
    echo "== $(basename "$bin") GLIBC =="
    "$OBJDUMP" -T "$bin" | grep -oE 'GLIBC_[0-9.]+' | sort -u || true
done
file "$BUILD_DIR/lib/libc++.so.1" "$BUILD_DIR/lib/libc++abi.so.1" \
    "$BUILD_DIR/lib/libatomic.so.1"
