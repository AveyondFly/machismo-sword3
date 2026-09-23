#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
BUILD_DIR=${BUILD_DIR:-"$ROOT/build-rocknix"}
LIBCXX_BUILD_DIR=${LIBCXX_BUILD_DIR:-"$ROOT/build-libcxx-rocknix"}
OUTPUT=${OUTPUT:-"$ROOT/dist/newpal-ios"}
LAUNCHER=${LAUNCHER:-"$(dirname "$OUTPUT")/newpal.sh"}

for path in \
    "$BUILD_DIR/machismo" \
    "$BUILD_DIR/libsystem_shim.so" \
    "$BUILD_DIR/libsword3_ios_shim.so" \
    "$BUILD_DIR/libsword3_objc_shim.so" \
    "$BUILD_DIR/libsword3_gl_bridge.so" \
    "$BUILD_DIR/libsword3_host.so" \
    "$LIBCXX_BUILD_DIR/lib/libc++.so.1" \
    "$LIBCXX_BUILD_DIR/lib/libc++abi.so.1"; do
    [ -f "$path" ] || {
        echo "Missing build artifact: $path" >&2
        exit 1
    }
done

rm -rf "$OUTPUT.new"
mkdir -p "$OUTPUT.new"
cp "$BUILD_DIR/machismo" \
   "$BUILD_DIR/libsystem_shim.so" \
   "$BUILD_DIR/libsword3_ios_shim.so" \
   "$BUILD_DIR/libsword3_objc_shim.so" \
   "$BUILD_DIR/libsword3_gl_bridge.so" \
   "$BUILD_DIR/libsword3_host.so" \
   "$OUTPUT.new/"
cp -L "$LIBCXX_BUILD_DIR/lib/libc++.so.1" \
      "$LIBCXX_BUILD_DIR/lib/libc++abi.so.1" \
      "$OUTPUT.new/"
cp "$ROOT/configs/newpal/machismo.conf" \
   "$ROOT/configs/newpal/machismo-partial-sdl.conf" \
   "$ROOT/configs/newpal/machismo-runtime-audit.conf" \
   "$ROOT/configs/newpal/dylib_map.conf" \
   "$ROOT/configs/newpal/address-hooks.conf" \
   "$ROOT/configs/newpal/patches.conf" \
   "$ROOT/configs/newpal/sdl-hook-report.json" \
   "$OUTPUT.new/"
chmod +x "$OUTPUT.new/machismo"

rm -rf "$OUTPUT"
mv "$OUTPUT.new" "$OUTPUT"
cp "$ROOT/newpal.sh" "$LAUNCHER.new"
chmod +x "$LAUNCHER.new"
mv -f "$LAUNCHER.new" "$LAUNCHER"
echo "NewPAL runtime staged at $OUTPUT"
echo "NewPAL launcher staged at $LAUNCHER"
