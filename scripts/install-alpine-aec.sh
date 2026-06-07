#!/usr/bin/env bash
set -euo pipefail

if [ "$#" -ne 1 ]; then
  echo "usage: $0 <rootfs>" >&2
  exit 2
fi

ROOTFS="$1"
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
AEC_PACKAGES="${SUVOS_AEC_PACKAGES:-bash git openssh-client libstdc++ libgcc file less ripgrep procps}"

[ -d "$ROOTFS" ] || {
  echo "rootfs directory does not exist: $ROOTFS" >&2
  exit 1
}

"$ROOT_DIR/scripts/install-alpine-layer.sh" \
  aec \
  "$ROOTFS" \
  "$AEC_PACKAGES" \
  ""
