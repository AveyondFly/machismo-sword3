#!/bin/sh
# Cross-build with Linaro 6.3.1 so GLIBC_ stays at 2.17. See
# /home/ubuntu/sword3/sword3/linaro_build.md for the compiler vs extra split.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
BUILD_DIR=${BUILD_DIR:-"$ROOT/build-linaro"}
TOOLCHAIN_FILE=${TOOLCHAIN_FILE:-"$ROOT/cmake/linaro-aarch64.cmake"}
TC=${LINARO_ROOT:-/opt/toolchains/gcc-linaro-6.3.1-2017.05-x86_64_aarch64-linux-gnu}
HOST_SYSROOT=${LINARO_HOST_SYSROOT:-}
if [ -z "$HOST_SYSROOT" ] &&
    [ -d /home/ubuntu/distribution/build.ROCKNIX-RK3326.aarch64/toolchain/aarch64-rocknix-linux-gnu/sysroot ]; then
	HOST_SYSROOT=/home/ubuntu/distribution/build.ROCKNIX-RK3326.aarch64/toolchain/aarch64-rocknix-linux-gnu/sysroot
fi

cmake -S "$ROOT" -B "$BUILD_DIR" \
    -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN_FILE" \
    -DLINARO_HOST_SYSROOT="$HOST_SYSROOT" \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DBUILD_WRAPGEN=OFF \
    -DBUILD_TESTS=OFF
cmake --build "$BUILD_DIR" --parallel "${JOBS:-2}"

READELF=$TC/bin/aarch64-linux-gnu-readelf
OBJDUMP=$TC/bin/aarch64-linux-gnu-objdump
for bin in "$BUILD_DIR/machismo" \
    "$BUILD_DIR/libsystem_shim.so" \
    "$BUILD_DIR/libsword3_host.so"; do
	[ -f "$bin" ] || continue
	echo "== $(basename "$bin") .comment =="
	"$READELF" -p .comment "$bin" || true
	echo "== $(basename "$bin") GLIBC =="
	"$OBJDUMP" -T "$bin" | grep GLIBC | sed 's/.*GLIBC_/GLIBC_/' |
	    sort -u || true
done
file "$BUILD_DIR/machismo" "$BUILD_DIR/libsystem_shim.so"
