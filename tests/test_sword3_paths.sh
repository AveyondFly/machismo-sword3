#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
BUILD_DIR=$(mktemp -d "${TMPDIR:-/tmp}/sword3-paths-build-XXXXXX")
trap 'rm -rf "$BUILD_DIR"' EXIT HUP INT TERM

"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -O2 \
	-I"$ROOT/src" \
	"$ROOT/tests/test_sword3_paths.c" \
	"$ROOT/src/ports/sword3/paths.c" \
	-o "$BUILD_DIR/test_sword3_paths"

"$BUILD_DIR/test_sword3_paths"
