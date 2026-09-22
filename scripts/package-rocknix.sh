#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
BUILD_DIR=${BUILD_DIR:-"$ROOT/build-rocknix"}
LIBCXX_BUILD_DIR=${LIBCXX_BUILD_DIR:-"$ROOT/build-libcxx-rocknix"}
OUTPUT=${OUTPUT:-"$ROOT/dist/paladin2-ios"}
LAUNCHER=${LAUNCHER:-"$(dirname "$OUTPUT")/paladin2.sh"}

required="
$BUILD_DIR/machismo
$BUILD_DIR/libsystem_shim.so
$BUILD_DIR/libsword3_ios_shim.so
$BUILD_DIR/libsword3_objc_shim.so
$BUILD_DIR/libsword3_gl_bridge.so
$BUILD_DIR/libsword3_host.so
$LIBCXX_BUILD_DIR/lib/libc++.so.1
$LIBCXX_BUILD_DIR/lib/libc++abi.so.1
"
for path in $required; do
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
cp "$ROOT/configs/paladin2/machismo.conf" \
   "$ROOT/configs/paladin2/machismo-partial-sdl.conf" \
   "$ROOT/configs/paladin2/machismo-runtime-audit.conf" \
   "$ROOT/configs/paladin2/dylib_map.conf" \
   "$ROOT/configs/paladin2/address-hooks.conf" \
   "$ROOT/configs/paladin2/patches.conf" \
   "$ROOT/configs/paladin2/sdl-hook-report.json" \
   "$ROOT/configs/paladin2/paladin2.gptk" \
   "$OUTPUT.new/"
CJK_FONT=/usr/share/fonts/truetype/droid/DroidSansFallbackFull.ttf
if [ -f "$CJK_FONT" ]; then
    mkdir -p "$OUTPUT.new/assets/host_menu"
    cp "$CJK_FONT" "$OUTPUT.new/assets/host_menu/cjk.ttf"
fi
chmod +x "$OUTPUT.new/machismo"

rm -rf "$OUTPUT"
mv "$OUTPUT.new" "$OUTPUT"
cp "$ROOT/paladin2.sh" "$LAUNCHER.new"
chmod +x "$LAUNCHER.new"
mv -f "$LAUNCHER.new" "$LAUNCHER"
echo "ROCKNIX package staged at $OUTPUT"
echo "ROCKNIX launcher staged at $LAUNCHER"
echo "Copy your owned paladin2.ipa into that directory before launching."
