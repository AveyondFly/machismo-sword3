#!/bin/sh
# Assemble a PortMaster-style port package from build-docker/ artifacts.
#
# Package layout mirrors what the launchers expect on device:
#   newpal / paladin2: <port>.sh sits BESIDE the <port>-ios/ runtime directory
#   sword3 (master):   run-rocknix.sh sits INSIDE sword3-ios/
# The zip unpacks straight into roms/ports/.
#
# libs.aarch64/ ships empty on purpose — PortMaster rule: only bundle libs
# proven missing on the target firmware with on-device ldd. Never ship
# SDL2/libstdc++/glibc from the build image (device-specific breakage), and
# this build needs FFmpeg 4.x sonames from the firmware.
#
# Usage: package-portmaster.sh <newpal|paladin2|sword3>
set -eu

PORT=${1:-}
case "$PORT" in
    newpal)
        PORT_DIR=newpal-ios
        LAUNCHER=newpal.sh
        CFG=newpal
        TITLE="NewPAL (iOS loader)"
        DESC="NewPAL 的 aarch64 本地装载器（machismo），从 iOS IPA 提取游戏数据。基于 glibc 2.17 工具链构建，面向老固件掌机。"
        IPA=newpal.ipa
        ;;
    paladin2)
        PORT_DIR=paladin2-ios
        LAUNCHER=paladin2.sh
        CFG=paladin2
        TITLE="Paladin2 (iOS loader)"
        DESC="Paladin2 的 aarch64 本地装载器（machismo），从 iOS IPA 提取游戏数据。基于 glibc 2.17 工具链构建，面向老固件掌机。"
        IPA=paladin2.ipa
        ;;
    sword3)
        PORT_DIR=sword3-ios
        LAUNCHER=run-rocknix.sh
        CFG=sword3
        TITLE="Sword3 (iOS loader)"
        DESC="Sword3 的 aarch64 本地装载器（machismo），从 iOS IPA 提取游戏数据。基于 glibc 2.17 工具链构建，面向老固件掌机。"
        IPA=sword3.ipa
        ;;
    *)
        echo "usage: package-portmaster.sh <newpal|paladin2|sword3>" >&2
        exit 1
        ;;
esac

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
BUILD_DIR=${BUILD_DIR:-"$ROOT/build-docker"}
DIST=${DIST:-"$ROOT/dist"}
STAGE="$DIST/.stage-$PORT_DIR"

for path in \
    "$BUILD_DIR/machismo" \
    "$BUILD_DIR/libsystem_shim.so" \
    "$BUILD_DIR/libsword3_ios_shim.so" \
    "$BUILD_DIR/libsword3_objc_shim.so" \
    "$BUILD_DIR/libsword3_gl_bridge.so" \
    "$BUILD_DIR/libsword3_host.so" \
    "$ROOT/$LAUNCHER" \
    "$ROOT/configs/$CFG/machismo-partial-sdl.conf"; do
    [ -f "$path" ] || {
        echo "package-portmaster: missing $path" >&2
        exit 1
    }
done

rm -rf "$STAGE"
PKG="$STAGE/$PORT_DIR"
mkdir -p "$PKG"

if [ "$PORT" = sword3 ]; then
    cp "$ROOT/$LAUNCHER" "$PKG/"
else
    cp "$ROOT/$LAUNCHER" "$STAGE/"
fi

cp "$BUILD_DIR/machismo" \
   "$BUILD_DIR/libsystem_shim.so" \
   "$BUILD_DIR/libsword3_ios_shim.so" \
   "$BUILD_DIR/libsword3_objc_shim.so" \
   "$BUILD_DIR/libsword3_gl_bridge.so" \
   "$BUILD_DIR/libsword3_host.so" \
   "$PKG/"
chmod +x "$PKG/machismo"

for f in "$ROOT/configs/$CFG"/*.conf "$ROOT/configs/$CFG"/*.json; do
    [ -f "$f" ] && cp "$f" "$PKG/"
done
if [ -f "$ROOT/configs/$CFG/$PORT.gptk" ]; then
    cp "$ROOT/configs/$CFG/$PORT.gptk" "$PKG/"
fi

mkdir -p "$PKG/libs.aarch64"
cat > "$PKG/libs.aarch64/README.txt" <<'EOF'
This directory ships EMPTY by design.

PortMaster rule: only place libraries here after on-device ldd proves the
firmware lacks them. Never bundle SDL2, libstdc++, glibc, EGL/GLES or driver
libs from the build image — they shadow the firmware's patched versions and
break input/display. Check with:
    ldd $GAMEDIR/machismo && ldd $GAMEDIR/libsword3_host.so
Copy only the reported missing .so files here (with any of THEIR missing
deps), then re-test.
EOF

cat > "$PKG/port.json" <<EOF
{
    "version": 4,
    "name": "$PORT_DIR.zip",
    "items": [
        "$LAUNCHER",
        "$PORT_DIR"
    ],
    "items_opt": null,
    "attr": {
        "title": "$TITLE",
        "porter": [
            "Windstarry"
        ],
        "desc": "$DESC",
        "desc_md": null,
        "inst": "将本包解压到 roms/ports/（或手动放入端口目录），把自备的 $IPA 放入 $PORT_DIR/ 目录，首次运行会自动提取 game/。固件需提供 FFmpeg 4.x 运行库（libavformat.so.58 等）；更新固件（ROCKNIX 等）请使用对应工具链构建的版本。",
        "inst_md": null,
        "genres": [],
        "image": {},
        "rtr": false,
        "exp": false,
        "runtime": [],
        "store": [],
        "availability": "paid",
        "reqs": [],
        "arch": [
            "aarch64"
        ],
        "min_glibc": "2.17"
    }
}
EOF

cat > "$PKG/README.md" <<EOF
# $TITLE

aarch64 engine binaries built with the PortMaster builder image
(ghcr.io/monkeyx-net/portmaster-build-templates/portmaster-builder:aarch64-latest,
Ubuntu 20.04 arm64 toolchain). GLIBC symbol floor: 2.17.

## Install

Unzip into \`roms/ports/\` so that \`$LAUNCHER\` and \`$PORT_DIR/\` land together,
place your own $IPA inside \`$PORT_DIR/\`, and launch.

## Libraries

- \`libs.aarch64/\` is empty: add only libs proven missing via on-device \`ldd\`.
- The engine needs FFmpeg 4.x sonames (libavformat.so.58, libavcodec.so.58,
  libavutil.so.56, libswscale.so.5) from the firmware. Older CFWs ship them;
  ROCKNIX (FFmpeg 6/7) does not — use the ROCKNIX-toolchain build there.

## Files

- \`machismo\` — Mach-O loader; \`lib*.so\` — Darwin shim / host service layers.
- \`*.conf\`, \`*.json\` — engine configs from \`configs/$CFG/\`.
- Logs land in \`$PORT_DIR/logs/\` (and \`runtime.log\` per launcher).
EOF

CR=$(printf '\r')
if find "$STAGE" -name '*.sh' -type f -exec grep -l "$CR" {} + | grep -q .; then
    echo "package-portmaster: CRLF line endings in launcher script" >&2
    exit 1
fi

mkdir -p "$DIST"
# zip(1) is not always present on Windows hosts; bsdtar can write zip too.
if command -v zip >/dev/null 2>&1; then
    (cd "$STAGE" && zip -qr "$DIST/$PORT_DIR.zip" .)
elif command -v bsdtar >/dev/null 2>&1 || [ -x /c/Windows/System32/tar.exe ]; then
    TAR=bsdtar
    command -v bsdtar >/dev/null 2>&1 || TAR=/c/Windows/System32/tar.exe
    (cd "$STAGE" && "$TAR" --format zip -cf "$DIST/$PORT_DIR.zip" .)
else
    echo "package-portmaster: need zip or bsdtar to write the package" >&2
    exit 1
fi
rm -rf "$STAGE"
echo "package-portmaster: staged $DIST/$PORT_DIR.zip"
