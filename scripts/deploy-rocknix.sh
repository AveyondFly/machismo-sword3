#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
TARGET=${TARGET:-root@192.168.31.110}
REMOTE_DIR=${REMOTE_DIR:-/roms/ports/sword3-ios}
PACKAGE=${OUTPUT:-"$ROOT/dist/sword3-ios"}

"$ROOT/scripts/package-rocknix.sh"

ssh "$TARGET" "mkdir -p '$REMOTE_DIR'"
tar -C "$PACKAGE" -cf - . |
    ssh "$TARGET" "tar -C '$REMOTE_DIR' -xf -"

echo "Deployed runtime to $TARGET:$REMOTE_DIR"
echo "Copy the owned IPA separately, then run:"
echo "  ssh $TARGET '. /storage/env.txt && $REMOTE_DIR/run-rocknix.sh'"
