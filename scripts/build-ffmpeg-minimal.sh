#!/bin/sh
# Build a minimal shared FFmpeg for bundling into the port's libs.aarch64/.
#
# The engine links FFmpeg 4.x sonames (libavformat.so.58, libavcodec.so.58,
# libavutil.so.56, libswscale.so.5, libswresample.so.3). Older handheld
# firmware does not ship these, and Debian's ffmpeg cannot be bundled instead:
# its dependency closure (X11, OpenCL, gnutls, libssh, ...) does not exist on
# handheld firmware. So build FFmpeg 4.4 with --disable-everything plus the
# codec set iOS-era game videos actually use — the result depends only on
# libc/libm and keeps the same sonames, so it also satisfies firmware builds
# that have no FFmpeg at all.
#
# Runs INSIDE the builder container (cwd /src). Skipped when already built.
set -eu

FFMPEG_VERSION=4.4.5
OUT_DIR=/src/build-docker/bundled-libs

if [ -f "$OUT_DIR/libavformat.so.58" ]; then
    echo "ffmpeg-minimal: $OUT_DIR already populated, skipping"
    exit 0
fi

WORK=/tmp/ffmpeg-minimal
OUT_TARBALL=/src/build-docker/ffmpeg-n${FFMPEG_VERSION}.tar.gz
URL="https://github.com/FFmpeg/FFmpeg/archive/refs/tags/n${FFMPEG_VERSION}.tar.gz"

if [ ! -f "$OUT_TARBALL" ]; then
    if command -v curl >/dev/null 2>&1; then
        curl -fsSL -o "$OUT_TARBALL" "$URL"
    elif command -v wget >/dev/null 2>&1; then
        wget -qO "$OUT_TARBALL" "$URL"
    else
        apt-get update
        DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends curl
        curl -fsSL -o "$OUT_TARBALL" "$URL"
    fi
fi

rm -rf "$WORK"
mkdir -p "$WORK" "$OUT_DIR"
cd "$WORK"
tar xzf "$OUT_TARBALL"
cd "FFmpeg-n${FFMPEG_VERSION}"

# configure only takes singular --enable-<kind>=NAME flags; build the list.
set --
for name in h264 hevc mpeg4 mpeg1video mpeg2video mjpeg theora vorbis opus \
            flac ac3 eac3 mp3 mp2 pcm_s16le pcm_s16be pcm_u8 \
            adpcm_ima_qt adpcm_ms; do
    set -- "$@" --enable-decoder="$name"
done
for name in mov matroska avi ogg mp3 wav flac asf aac h264 hevc m4v swf flv; do
    set -- "$@" --enable-demuxer="$name"
done
for name in aac h264 hevc mpeg4video mpegaudio vorbis opus ac3 flac mjpeg; do
    set -- "$@" --enable-parser="$name"
done

./configure \
    --enable-shared --disable-static \
    --disable-autodetect \
    --disable-programs --disable-doc --disable-debug \
    --disable-network --disable-iconv --disable-bzlib --disable-zlib \
    --disable-sdl2 \
    --disable-everything \
    --enable-swscale \
    --enable-protocol=file \
    "$@"
make -j"$(nproc)"

for lib in \
    libavformat/libavformat.so.58 \
    libavcodec/libavcodec.so.58 \
    libavutil/libavutil.so.56 \
    libswscale/libswscale.so.5 \
    libswresample/libswresample.so.3; do
    cp -L "$lib" "$OUT_DIR/"
done

echo "ffmpeg-minimal: bundled libs and their NEEDED closure:"
for f in "$OUT_DIR"/libav*.so.* "$OUT_DIR"/libsw*.so.*; do
    echo "-- $(basename "$f")"
    readelf -d "$f" | grep NEEDED || true
done
