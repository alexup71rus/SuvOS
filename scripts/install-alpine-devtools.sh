#!/usr/bin/env bash
set -euo pipefail

if [ "$#" -ne 1 ]; then
  echo "usage: $0 <rootfs>" >&2
  exit 2
fi

ROOTFS="$1"
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DEV_PACKAGES="${SUVOS_DEV_PACKAGES:-apk-tools ca-certificates}"
DEV_POST_INSTALL='
mkdir -p /suvos-root/etc/apk/keys /suvos-root/var/cache/apk
cp -a /etc/apk/keys/. /suvos-root/etc/apk/keys/
cp /etc/apk/repositories /suvos-root/etc/apk/repositories
'

[ -d "$ROOTFS" ] || {
  echo "rootfs directory does not exist: $ROOTFS" >&2
  exit 1
}

"$ROOT_DIR/scripts/install-alpine-layer.sh" \
  devtools \
  "$ROOTFS" \
  "$DEV_PACKAGES" \
  "$DEV_POST_INSTALL"
