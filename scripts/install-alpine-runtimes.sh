#!/usr/bin/env bash
set -euo pipefail

if [ "$#" -ne 1 ]; then
  echo "usage: $0 <rootfs>" >&2
  exit 2
fi

ROOTFS="$1"
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
RUNTIME_PACKAGES="${SUVOS_RUNTIME_PACKAGES:-python3 nodejs ca-certificates}"

[ -d "$ROOTFS" ] || {
  echo "rootfs directory does not exist: $ROOTFS" >&2
  exit 1
}

"$ROOT_DIR/scripts/install-alpine-layer.sh" \
  runtimes \
  "$ROOTFS" \
  "$RUNTIME_PACKAGES" \
  ""
