#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
BUILD_DIR=${BUILD_DIR:-"$ROOT/build-rocknix"}
TOOLCHAIN_FILE=${TOOLCHAIN_FILE:-"$ROOT/cmake/rocknix-rk3326.cmake"}

cmake -S "$ROOT" -B "$BUILD_DIR" \
    -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN_FILE" \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DBUILD_WRAPGEN=OFF \
    -DBUILD_TESTS=OFF
cmake --build "$BUILD_DIR" --parallel "${JOBS:-2}"

file "$BUILD_DIR/machismo" "$BUILD_DIR/libsystem_shim.so"
