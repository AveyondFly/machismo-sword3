#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
TOOLCHAIN_ROOT=${ROCKNIX_TOOLCHAIN_ROOT:-/home/ubuntu/distribution/build.ROCKNIX-RK3326.aarch64/toolchain}
SYSROOT=${ROCKNIX_SYSROOT:-"$TOOLCHAIN_ROOT/aarch64-rocknix-linux-gnu/sysroot"}
CC=${ROCKNIX_CC:-"$TOOLCHAIN_ROOT/bin/aarch64-rocknix-linux-gnu-gcc"}
PKG_CONFIG=${PKG_CONFIG:-pkg-config}
SOURCE=${1:-${SWORD3_IPA:-}}
BUILD_DIR=$(mktemp -d "${TMPDIR:-/tmp}/sword3-video-test-XXXXXX")
trap 'rm -rf "$BUILD_DIR"' EXIT HUP INT TERM

if [ -z "$SOURCE" ]; then
	echo "usage: $0 /path/to/Sword3.ipa|/path/to/short.mp4" >&2
	exit 2
fi
if [ ! -f "$SOURCE" ]; then
	echo "input does not exist: $SOURCE" >&2
	exit 2
fi
if [ ! -x "$CC" ]; then
	echo "ROCKNIX compiler does not exist: $CC" >&2
	exit 2
fi

case "$SOURCE" in
	*.ipa|*.IPA|*.zip|*.ZIP)
		unzip -Z1 "$SOURCE" >"$BUILD_DIR/archive.list"
		ENTRY=$(awk '/^Payload\/[^/]+\.app\/Video\/ch\.mp4$/ { print; exit }' \
			"$BUILD_DIR/archive.list")
		if [ -z "$ENTRY" ]; then
			ENTRY=$(awk '/^Payload\/[^/]+\.app\/.*\.[Mm][Pp]4$/ { print; exit }' \
				"$BUILD_DIR/archive.list")
		fi
		if [ -z "$ENTRY" ]; then
			echo "no MP4 found below Payload/*.app in $SOURCE" >&2
			exit 1
		fi
		unzip -p "$SOURCE" "$ENTRY" >"$BUILD_DIR/sample.mp4"
		;;
	*)
		cp "$SOURCE" "$BUILD_DIR/sample.mp4"
		;;
esac

if ! command -v ffprobe >/dev/null 2>&1; then
	echo "ffprobe is required for the host-side media probe" >&2
	exit 2
fi
ffprobe -v error -select_streams v:0 \
	-show_entries stream=codec_name,width,height,duration \
	-of default=noprint_wrappers=1 "$BUILD_DIR/sample.mp4"
EXPECTED_FRAMES=$(ffprobe -v error -select_streams v:0 -count_frames \
	-show_entries stream=nb_read_frames -of default=noprint_wrappers=1:nokey=1 \
	"$BUILD_DIR/sample.mp4")
case "$EXPECTED_FRAMES" in
	''|*[!0-9]*)
		echo "ffprobe did not return a video frame count" >&2
		exit 1
		;;
esac

export PKG_CONFIG_SYSROOT_DIR="$SYSROOT"
export PKG_CONFIG_LIBDIR="$SYSROOT/usr/lib/pkgconfig:$SYSROOT/usr/share/pkgconfig"
FFMPEG_VERSION=$("$PKG_CONFIG" --modversion libavformat)
case "$FFMPEG_VERSION" in
	60.*) ;;
	*)
		echo "expected ROCKNIX FFmpeg 6 (libavformat 60), found $FFMPEG_VERSION" >&2
		exit 1
		;;
esac

SDL_CFLAGS=$("$PKG_CONFIG" --cflags sdl2 SDL2_mixer)
SDL_LIBS=$("$PKG_CONFIG" --libs sdl2 SDL2_mixer)
FFMPEG_CFLAGS=$("$PKG_CONFIG" --cflags libavformat libavcodec libavutil libswscale)
FFMPEG_LIBS=$("$PKG_CONFIG" --libs libavformat libavcodec libavutil libswscale)

# Compile every host-service source with the exact ROCKNIX sysroot.
for SOURCE_FILE in paths input audio_bridge video_bridge; do
	case "$SOURCE_FILE" in
		input|audio_bridge) EXTRA_CFLAGS=$SDL_CFLAGS ;;
		video_bridge) EXTRA_CFLAGS=$FFMPEG_CFLAGS ;;
		*) EXTRA_CFLAGS= ;;
	esac
	# pkg-config output is intentionally word-split into compiler arguments.
	# shellcheck disable=SC2086
	"$CC" --sysroot="$SYSROOT" -std=c11 -Wall -Wextra -Werror -O2 \
		-I"$ROOT/src" $EXTRA_CFLAGS \
		-c "$ROOT/src/ports/sword3/$SOURCE_FILE.c" \
		-o "$BUILD_DIR/$SOURCE_FILE.o"
done

# shellcheck disable=SC2086
"$CC" --sysroot="$SYSROOT" -std=c11 -Wall -Wextra -Werror -O2 \
	-I"$ROOT/src" $FFMPEG_CFLAGS \
	"$ROOT/tests/test_sword3_ffmpeg.c" \
	"$BUILD_DIR/input.o" "$BUILD_DIR/audio_bridge.o" \
	"$BUILD_DIR/video_bridge.o" \
	$SDL_LIBS $FFMPEG_LIBS -o "$BUILD_DIR/test_sword3_ffmpeg"

file "$BUILD_DIR/test_sword3_ffmpeg"
echo "ROCKNIX host services compile against FFmpeg 6 (libavformat $FFMPEG_VERSION)"

# Execute the target decoder under an explicit runner, or qemu when available.
RUNNER=${ROCKNIX_RUNNER:-}
if [ -z "$RUNNER" ] && command -v qemu-aarch64 >/dev/null 2>&1; then
	# This SDK installs the target loader below /usr/lib instead of /lib,
	# so invoke it explicitly rather than relying on qemu's -L lookup.
	RUNNER="qemu-aarch64 $SYSROOT/usr/lib/ld-linux-aarch64.so.1 --library-path $SYSROOT/usr/lib"
fi
if [ -n "$RUNNER" ]; then
	# ROCKNIX_RUNNER is intentionally word-split (for example, qemu-aarch64 -L ...).
	# shellcheck disable=SC2086
	SDL_AUDIODRIVER=dummy SDL_VIDEODRIVER=dummy \
		$RUNNER "$BUILD_DIR/test_sword3_ffmpeg" \
		"$BUILD_DIR/sample.mp4" "$EXPECTED_FRAMES"
else
	echo "qemu-aarch64 unavailable; target decoder execution skipped" >&2
fi
