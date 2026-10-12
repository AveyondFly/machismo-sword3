#!/bin/sh
# Assemble a PortMaster-style port package from build-docker/ artifacts.
#
# Package layout mirrors what the launchers expect on device:
#   newpal / paladin2: <port>.sh sits BESIDE the <port>-ios/ runtime directory
#   sword3 (master):   run-rocknix.sh sits INSIDE sword3-ios/
# The zip unpacks straight into roms/ports/.
#
# Bundled libraries are limited to what on-device ldd proved the firmware lacks:
# a minimal FFmpeg 4.x, the gcc 9.4 libstdc++, and the Apple-ABI libc++ pair plus a
# 64-bit libatomic.so.1 (old firmware only ships a 32-bit one). Never ship
# SDL2/glibc/EGL/driver libs from the build image — they shadow the firmware's
# patched versions and break input/display per device.
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
# Apple-ABI libc++ plus a 64-bit libatomic.so.1, which libc++.so.1 lists as NEEDED
# and older firmware only ships in a 32-bit flavour. A locally rebuilt set (see
# scripts/build-libcxx.sh) wins when present; otherwise the tracked prebuilt pair
# under prebuilt/aarch64-apple-libcxx/ is used, so a clone can package without an
# LLVM checkout. Either way the floor gate below rejects a libc++ built against a
# newer glibc than the engine — the ROCKNIX cross-toolchain build needs GLIBC_2.38
# and is unusable on old firmware.
if [ -n "${LIBCXX_BUILD_DIR:-}" ]; then
    LIBCXX_LIB_DIR="$LIBCXX_BUILD_DIR/lib"
elif [ -f "$ROOT/build-libcxx/lib/libc++.so.1" ]; then
    LIBCXX_LIB_DIR="$ROOT/build-libcxx/lib"
else
    LIBCXX_LIB_DIR="$ROOT/prebuilt/aarch64-apple-libcxx"
fi
DIST=${DIST:-"$ROOT/dist"}
STAGE="$DIST/.stage-$PORT_DIR"

for path in \
    "$BUILD_DIR/machismo" \
    "$BUILD_DIR/libsystem_shim.so" \
    "$BUILD_DIR/libsword3_ios_shim.so" \
    "$BUILD_DIR/libsword3_objc_shim.so" \
    "$BUILD_DIR/libsword3_gl_bridge.so" \
    "$BUILD_DIR/libsword3_host.so" \
    "$LIBCXX_LIB_DIR/libc++.so.1" \
    "$LIBCXX_LIB_DIR/libc++abi.so.1" \
    "$LIBCXX_LIB_DIR/libatomic.so.1" \
    "$ROOT/$LAUNCHER" \
    "$ROOT/assets/host_menu/cjk.ttf" \
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

# The trainer/menu overlay needs ASCII next to its simplified Chinese strings, so
# the font ships with the package instead of being resolved from the build host or
# from firmware (most of which have no CJK face at all). See assets/host_menu/.
mkdir -p "$PKG/assets/host_menu"
cp "$ROOT/assets/host_menu/cjk.ttf" "$PKG/assets/host_menu/"
cp "$ROOT/assets/host_menu/LICENSE-cjk.txt" "$PKG/assets/host_menu/"

mkdir -p "$PKG/libs.aarch64"

# Apple-ABI libc++: dylib_map.conf maps 'libc++.1 = ./libc++.so.1', and the launcher
# cds to $GAMEDIR before starting the loader, so the pair belongs at the package root
# rather than in libs.aarch64/. libatomic.so.1 is a DT_NEEDED of libc++.so.1 and old
# firmware has only a 32-bit build of it, so it goes into libs.aarch64/ — the
# launcher orders that directory ahead of the /usr/lib32 control.txt appends.
cp -L "$LIBCXX_LIB_DIR/libc++.so.1" \
      "$LIBCXX_LIB_DIR/libc++abi.so.1" \
      "$PKG/"
cp -L "$LIBCXX_LIB_DIR/libatomic.so.1" "$PKG/libs.aarch64/"

# A bundled libc++ that needs a newer glibc than the engine itself is precisely what
# made this port unbootable on an EmuELEC 4.7 unit (glibc 2.36): the dlopen failed and
# strict binds then rejected every libc++ symbol. Gate it where objdump is available.
if command -v objdump >/dev/null 2>&1; then
    glibc_floor()
    {
        objdump -T "$1" 2>/dev/null | grep -oE 'GLIBC_[0-9.]+' | sort -uV | tail -1
    }
    engine_floor=$( { glibc_floor "$PKG/machismo"
        for shim in "$PKG"/lib*.so; do glibc_floor "$shim"; done; } | sort -uV | tail -1)
    for bin in "$PKG/libc++.so.1" "$PKG/libc++abi.so.1" "$PKG/libs.aarch64/libatomic.so.1"; do
        bin_floor=$(glibc_floor "$bin")
        [ -n "$bin_floor" ] || {
            echo "package-portmaster: objdump found but no GLIBC floor readable from $bin" >&2
            exit 1
        }
        [ "$(printf '%s\n%s\n' "$bin_floor" "$engine_floor" | sort -V | tail -1)" = "$engine_floor" ] || {
            echo "package-portmaster: $(basename "$bin") needs $bin_floor but the engine only needs $engine_floor — rebuild it with an old-glibc sysroot via scripts/build-libcxx.sh" >&2
            exit 1
        }
    done
else
    echo "package-portmaster: warning: no objdump on PATH, the bundled libc++ GLIBC floor is NOT checked" >&2
fi

# Bundle the runtime libs the engine needs that firmware does not guarantee:
# a dependency-free minimal FFmpeg matching the engine's 4.x sonames plus the
# gcc 9.4 libstdc++ (GLIBCXX_3.4.28 floor). Never add SDL2/glibc/EGL here —
# they shadow the firmware's patched versions (device-specific breakage).
LIBS_SRC="$BUILD_DIR/bundled-libs"
if find "$LIBS_SRC" -maxdepth 1 -name '*.so*' 2>/dev/null | grep -q .; then
    cp -L "$LIBS_SRC"/*.so* "$PKG/libs.aarch64/"
else
    echo "package-portmaster: warning: no bundled libs at $LIBS_SRC" >&2
fi

cat > "$PKG/libs.aarch64/README.txt" <<'EOF'
Runtime libraries bundled with this port:

- libavformat.so.58 / libavcodec.so.58 / libavutil.so.56 / libswscale.so.5 /
  libswresample.so.3 — minimal FFmpeg 4.4 build (--disable-everything + the
  decoders/demuxers iOS-era game media use; no external deps beyond libc).
- libstdc++.so.6 — gcc 9.4 runtime (the loader needs GLIBCXX_3.4.28; some
  older firmware ships an older libstdc++).
- libatomic.so.1 — 64-bit gcc atomic helpers. Older firmware only has the 32-bit
  build under /usr/lib32, and libc++.so.1 (which sits at the port root) needs a
  64-bit one; the launcher therefore keeps this directory ahead of /usr/lib32.

Rules for touching this directory:

- If on-device ldd reports a missing lib NOT covered here, copy it in from a
  matching toolchain and re-test.
- Never add SDL2, glibc, EGL/GLES or driver libs from a build image: they
  shadow the firmware's patched versions and break input/display.
- If the game's video uses a codec this minimal build lacks, either extend
  scripts/build-ffmpeg-minimal.sh or drop in the firmware's full FFmpeg 4.x.
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
        "inst": "将本包解压到 roms/ports/（或手动放入端口目录），把自备的 $IPA 放入 $PORT_DIR/ 目录，首次运行会自动提取 game/。包内已自带 Apple-ABI libc++（旧 glibc 构建）与 64 位 libatomic.so.1，libs.aarch64/ 另捆绑无外部依赖的最小 FFmpeg 4.x 与 libstdc++，无需再手动补库；若游戏视频缺解码器，替换为固件自带的 FFmpeg 4.x 或扩展 scripts/build-ffmpeg-minimal.sh。",
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

- \`libs.aarch64/\` bundles a dependency-free minimal FFmpeg 4.4 (same 4.x
  sonames the engine links) plus the gcc 9.4 libstdc++ and a 64-bit
  libatomic.so.1, so the package runs on firmware that ships none of them.
- \`libc++.so.1\` / \`libc++abi.so.1\` sit at the port root because
  \`dylib_map.conf\` maps \`libc++.1\` to \`./libc++.so.1\` and the launcher cds
  there before starting the loader. They are the Apple-ABI build of libc++
  (\`scripts/build-libcxx.sh\` against an old-glibc sysroot, tracked under
  \`prebuilt/aarch64-apple-libcxx/\` so a clone can package without an LLVM
  checkout); a libc++ from a newer toolchain loads on ROCKNIX but fails on glibc
  2.36 firmware.
- Do not add SDL2, glibc, EGL/GLES or driver libs: the firmware's patched
  versions must stay in charge. Anything else missing shows up in on-device
  \`ldd machismo\` / \`ldd libsword3_host.so\` — copy exactly those in.
- If game video needs a codec the minimal build lacks, extend
  \`scripts/build-ffmpeg-minimal.sh\` or drop in the firmware's FFmpeg 4.x.

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
