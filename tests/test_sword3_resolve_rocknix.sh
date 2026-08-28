#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
BINARY=${SWORD3_BINARY:-/tmp/sword3-ipa-extract/Payload/SWD3.app/SWD3}
TOOLCHAIN_ROOT=${ROCKNIX_TOOLCHAIN_ROOT:-/home/ubuntu/distribution/build.ROCKNIX-RK3326.aarch64/toolchain}
SYSROOT="$TOOLCHAIN_ROOT/aarch64-rocknix-linux-gnu/sysroot"
TARGETLIB="$TOOLCHAIN_ROOT/aarch64-rocknix-linux-gnu/lib64"
OUTPUT=$(mktemp)
RUNTIME_OUTPUT=$(mktemp)
trap 'rm -f "$OUTPUT" "$RUNTIME_OUTPUT"' EXIT

[ -f "$BINARY" ] || {
    echo "Sword3 Mach-O not found: $BINARY" >&2
    exit 1
}
[ -x "$ROOT/build-rocknix/machismo" ] || "$ROOT/scripts/build-rocknix.sh"

(
    cd "$ROOT"
    MACHISMO_CONFIG=configs/sword3/machismo-audit.conf \
        qemu-aarch64 -L "$SYSROOT" \
        "$SYSROOT/usr/lib/ld-linux-aarch64.so.1" \
        --library-path "$SYSROOT/usr/lib:$TARGETLIB" \
        build-rocknix/machismo "$BINARY"
) >"$OUTPUT" 2>&1

rg -q '20 dylibs, LC_DYLD_INFO' "$OUTPUT"
rg -q '876 stubbed, 0 failed, 5594 rebases' "$OUTPUT"
rg -q 'audit-only complete; no guest constructors or entry point executed' "$OUTPUT"

"$ROOT/scripts/package-rocknix.sh" >/dev/null
(
    cd "$ROOT/dist/sword3-ios"
    MACHISMO_CONFIG=machismo-runtime-audit.conf \
    MACHISMO_STRICT_BINDS=1 \
        qemu-aarch64 -L "$SYSROOT" \
        "$SYSROOT/usr/lib/ld-linux-aarch64.so.1" \
        --library-path "$PWD:$SYSROOT/usr/lib:$TARGETLIB" \
        ./machismo "$BINARY"
) >"$RUNTIME_OUTPUT" 2>&1

rg -q '907 binds resolved, 0 stubbed, 0 failed, 5594 rebases' "$RUNTIME_OUTPUT"
rg -q 'audit-only complete; no guest constructors or entry point executed' \
    "$RUNTIME_OUTPUT"
echo "Sword3 ROCKNIX stub and real-mapping resolve audits passed"
