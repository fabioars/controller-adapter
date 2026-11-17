#!/usr/bin/env bash
# Fetches third-party code at pinned commits into third_party/:
#   - Bluepad32 (+ BTstack submodule): Bluetooth controllers
#   - libxsm3 (LGPL-2.1): Xbox 360 console controller authentication
set -euo pipefail

BLUEPAD32_COMMIT="80d6314"
LIBXSM3_COMMIT="7f1b78c"  # newer commits have a broken SHA-1 rewrite
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DEST="$ROOT/third_party/bluepad32"

if [ ! -d "$DEST/.git" ]; then
    git clone https://github.com/ricardoquesada/bluepad32.git "$DEST"
fi
git -C "$DEST" fetch --quiet origin
git -C "$DEST" checkout --quiet "$BLUEPAD32_COMMIT"
git -C "$DEST" submodule update --init --recursive --quiet
echo "Bluepad32 ready at $DEST ($(git -C "$DEST" rev-parse --short HEAD))"

XSM3_DEST="$ROOT/third_party/libxsm3"
if [ ! -d "$XSM3_DEST/.git" ]; then
    git clone https://github.com/InvoxiPlayGames/libxsm3.git "$XSM3_DEST"
fi
git -C "$XSM3_DEST" fetch --quiet origin
git -C "$XSM3_DEST" checkout --quiet "$LIBXSM3_COMMIT"
echo "libxsm3 ready at $XSM3_DEST ($(git -C "$XSM3_DEST" rev-parse --short HEAD))"
