#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
BUILD_DIR=${BUILD_DIR:-"$ROOT/build-rocknix"}
LIBCXX_BUILD_DIR=${LIBCXX_BUILD_DIR:-"$ROOT/build-libcxx-rocknix"}
OUTPUT=${OUTPUT:-"$ROOT/dist/paladin2-ios"}

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
   "$ROOT/run-rocknix.sh" \
   "$OUTPUT.new/"
cat >"$OUTPUT.new/paladin2-ios.sh" <<'EOF'
#!/bin/sh
GAMEDIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
cd "$GAMEDIR" || exit 1
exec ./run-rocknix.sh "$@"
EOF
chmod +x "$OUTPUT.new/machismo" "$OUTPUT.new/run-rocknix.sh" "$OUTPUT.new/paladin2-ios.sh"

rm -rf "$OUTPUT"
mv "$OUTPUT.new" "$OUTPUT"
echo "ROCKNIX package staged at $OUTPUT"
echo "Copy your owned paladin2.ipa into that directory before launching."
