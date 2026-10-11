#!/usr/bin/env bash
# Build machismo + the sword3 shims as aarch64 ELF inside the local PortMaster
# builder image. The image is an arm64 Ubuntu 20.04 (glibc 2.31) container, so
# its native toolchain emits aarch64 binaries directly — no toolchain file —
# and the resulting glibc symbol floor is far lower than what the ROCKNIX
# cross toolchain (cmake/rocknix-rk3326.cmake) produces, which is the point:
# ROCKNIX-built binaries refuse to load on older handheld firmware.
set -euo pipefail

IMAGE="ghcr.io/monkeyx-net/portmaster-build-templates/portmaster-builder:aarch64-latest"
REPO_DIR="$(cd "$(dirname "$0")/.." && pwd)"

# Docker Desktop wants a Windows-style path in the bind mount; cygpath handles
# Git Bash while the fallback keeps the script usable on plain Linux hosts.
SRC="${REPO_DIR}"
command -v cygpath >/dev/null 2>&1 && SRC="$(cygpath -m "${REPO_DIR}")"

MSYS_NO_PATHCONV=1 docker run --rm --platform linux/arm64 \
    -v "${SRC}:/src" \
    -w /src \
    "${IMAGE}" bash -c '
set -euo pipefail
# FFmpeg dev packages are the only dependency gap in the base image; SDL2,
# GLESv1/v2, EGL and libdrm headers are already installed.
if ! pkg-config --exists libavformat libavcodec libavutil libswscale; then
    apt-get update
    DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
        libavformat-dev libavcodec-dev libavutil-dev libswscale-dev
fi
cmake -S /src -B /src/build-docker -G Ninja \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DBUILD_WRAPGEN=OFF \
    -DBUILD_TESTS=OFF
cmake --build /src/build-docker --parallel "$(nproc)"

echo "== artifacts =="
file /src/build-docker/machismo /src/build-docker/lib*.so
echo "== glibc symbol floor (max GLIBC_ version per binary) =="
for f in /src/build-docker/machismo /src/build-docker/lib*.so; do
    printf "%s: " "$(basename "$f")"
    objdump -T "$f" 2>/dev/null | grep -o "GLIBC_[0-9.]*" | sort -Vu | tail -1
done
echo "== DT_NEEDED =="
for f in /src/build-docker/machismo /src/build-docker/lib*.so; do
    echo "-- $(basename "$f")"
    readelf -d "$f" | grep NEEDED || true
done
'
