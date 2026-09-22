#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
TARGET=${TARGET:-root@192.168.31.110}
REMOTE_DIR=${REMOTE_DIR:-/roms/ports/paladin2-ios}
PACKAGE=${OUTPUT:-"$ROOT/dist/paladin2-ios"}
LAUNCHER=${LAUNCHER:-"$(dirname "$PACKAGE")/paladin2.sh"}
REMOTE_LAUNCHER=${REMOTE_LAUNCHER:-"$(dirname "$REMOTE_DIR")/paladin2.sh"}

"$ROOT/scripts/package-rocknix.sh"

ssh "$TARGET" "mkdir -p '$REMOTE_DIR'"
tar -C "$PACKAGE" -cf - . |
    ssh "$TARGET" "tar -C '$REMOTE_DIR' -xf -"
scp "$LAUNCHER" "$TARGET:$REMOTE_LAUNCHER.new"
ssh "$TARGET" "chmod 755 '$REMOTE_LAUNCHER.new' && mv -f '$REMOTE_LAUNCHER.new' '$REMOTE_LAUNCHER' && rm -f '$REMOTE_DIR/run-rocknix.sh' '$REMOTE_DIR/paladin2-ios.sh'"

echo "Deployed runtime to $TARGET:$REMOTE_DIR"
echo "Deployed launcher to $TARGET:$REMOTE_LAUNCHER"
echo "Copy the owned IPA separately, then run:"
echo "  ssh $TARGET '$REMOTE_LAUNCHER'"
