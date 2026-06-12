#!/usr/bin/env bash
set -euo pipefail

if [ "$#" -ne 1 ]; then
  echo "usage: $0 <rootfs>" >&2
  exit 2
fi

ROOTFS="$1"
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
. "$ROOT_DIR/scripts/suvos-arch.sh"
ARCH="$(suvos_arch)"

[ -d "$ROOTFS" ] || {
  echo "rootfs directory does not exist: $ROOTFS" >&2
  exit 1
}

if [ -n "${SUVOS_CHROMIUM_DIST:-}" ]; then
  CHROMIUM_DIST="$SUVOS_CHROMIUM_DIST"
else
  if [ -n "${SUVOS_CHROMIUM_REPO:-}" ]; then
    CHROMIUM_REPO="$SUVOS_CHROMIUM_REPO"
  else
    LOCKED_CHROMIUM_PATH="$(python3 "$ROOT_DIR/scripts/vendor-lock.py" --lockfile "${SUVOS_VENDORS_LOCKFILE:-$ROOT_DIR/third_party/vendors.lock.json}" get chromium path 2>/dev/null || true)"
    if [ -n "$LOCKED_CHROMIUM_PATH" ] && [ -d "$ROOT_DIR/$LOCKED_CHROMIUM_PATH" ]; then
      CHROMIUM_REPO="$ROOT_DIR/$LOCKED_CHROMIUM_PATH"
    else
      echo "Chromium checkout not found. Run: $ROOT_DIR/scripts/bootstrap-vendors.sh chromium" >&2
      exit 1
    fi
  fi

  if [ "$ARCH" = "x86_64" ]; then
    CHROMIUM_DIST="$CHROMIUM_REPO/dist/chromium-rootfs.tar.gz"
  else
    CHROMIUM_DIST="$CHROMIUM_REPO/dist/chromium-rootfs-$ARCH.tar.gz"
  fi
fi

[ -s "$CHROMIUM_DIST" ] || {
  echo "Chromium artifact is missing: $CHROMIUM_DIST" >&2
  exit 1
}

tar_args=(-xzf "$CHROMIUM_DIST")
if tar --version 2>/dev/null | grep -q 'GNU tar'; then
  tar_args=(--warning=no-timestamp "${tar_args[@]}")
fi

tar -C "$ROOTFS" "${tar_args[@]}"
