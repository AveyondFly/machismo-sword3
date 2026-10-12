#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
BUILD_DIR=${BUILD_DIR:-"$ROOT/build-rocknix"}
# Apple-ABI libc++: a locally rebuilt set wins, otherwise the tracked prebuilt
# pair under prebuilt/aarch64-apple-libcxx/ is used (see the README there).
# Only the pair goes in — ROCKNIX firmware has a 64-bit libatomic of its own.
if [ -n "${LIBCXX_BUILD_DIR:-}" ]; then
    LIBCXX_LIB_DIR="$LIBCXX_BUILD_DIR/lib"
elif [ -f "$ROOT/build-libcxx-rocknix/lib/libc++.so.1" ]; then
    LIBCXX_LIB_DIR="$ROOT/build-libcxx-rocknix/lib"
else
    LIBCXX_LIB_DIR="$ROOT/prebuilt/aarch64-apple-libcxx"
fi
OUTPUT=${OUTPUT:-"$ROOT/dist/paladin2-ios"}

required="
$BUILD_DIR/machismo
$BUILD_DIR/libsystem_shim.so
$BUILD_DIR/libsword3_ios_shim.so
$BUILD_DIR/libsword3_objc_shim.so
$BUILD_DIR/libsword3_gl_bridge.so
$BUILD_DIR/libsword3_host.so
$LIBCXX_LIB_DIR/libc++.so.1
$LIBCXX_LIB_DIR/libc++abi.so.1
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
cp -L "$LIBCXX_LIB_DIR/libc++.so.1" \
      "$LIBCXX_LIB_DIR/libc++abi.so.1" \
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
# The host overlay strings are simplified Chinese *and* ASCII (digits in the
# money line, the "A 确定 B/Y 关闭" hint), so the font must cover both.  The build
# host's DroidSansFallbackFull.ttf is the CJK-only build of Droid Sans Fallback
# (28601 cmap entries, no Latin range) and rendered every digit and letter as a
# box, so the font is tracked in the repo rather than resolved from the host.
CJK_FONT="$ROOT/assets/host_menu/cjk.ttf"
[ -f "$CJK_FONT" ] || {
    echo "Missing host overlay font: $CJK_FONT" >&2
    exit 1
}
mkdir -p "$OUTPUT.new/assets/host_menu"
cp "$CJK_FONT" "$OUTPUT.new/assets/host_menu/cjk.ttf"
cp "$ROOT/assets/host_menu/LICENSE-cjk.txt" "$OUTPUT.new/assets/host_menu/"
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
