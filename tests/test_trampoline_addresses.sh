#!/bin/bash
# Unit test for stripped Mach-O address hooks using a synthetic mapped image.
set -e
cd "$(dirname "$0")/.."

MACHISMO_ROOT="${MACHISMO_ROOT:-$(pwd)}"
BUILD_DIR="${BUILD_DIR:-$MACHISMO_ROOT/build}"
mkdir -p "$BUILD_DIR"

cc -shared -fPIC \
    -o "$BUILD_DIR/libaddress_hook_test.so" \
    tests/fixtures/libtest_native.c

cc -D_GNU_SOURCE -Wall -Wextra -Werror -O2 -g -rdynamic \
    -Isrc \
    -o "$BUILD_DIR/test_trampoline_addresses" \
    tests/test_trampoline_addresses.c src/trampoline.c src/config.c \
    -ldl

"$BUILD_DIR/test_trampoline_addresses" \
    "$BUILD_DIR/libaddress_hook_test.so"
