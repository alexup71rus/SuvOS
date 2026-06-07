#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
. "$ROOT_DIR/scripts/suvos-arch.sh"
ARCH="$(suvos_arch)"
AEC_TARGET_ARCH="$(suvos_aec_target_arch "$ARCH")"
AEC_REPO="${SUVOS_AEC_REPO:-$ROOT_DIR/../admin-explorer-code}"
if [ "$ARCH" = "x86_64" ]; then
  AEC_DIST_DEFAULT="$AEC_REPO/dist/aec-rootfs.tar.gz"
else
  AEC_DIST_DEFAULT="$AEC_REPO/dist/aec-rootfs-$ARCH.tar.gz"
fi
AEC_DIST="${SUVOS_AEC_DIST:-$AEC_DIST_DEFAULT}"

aec_source_is_newer_than_artifact() {
  [ -s "$AEC_DIST" ] || return 0
  [ -d "$AEC_REPO" ] || return 1

  find \
    "$AEC_REPO/src" \
    "$AEC_REPO/scripts" \
    "$AEC_REPO/build" \
    "$AEC_REPO/remote" \
    "$AEC_REPO/package.json" \
    "$AEC_REPO/package-lock.json" \
    "$AEC_REPO/product.json" \
    -path '*/node_modules' -prune -o \
    -path '*/node_modules/*' -prune -o \
    -path '*/dist' -prune -o \
    -path '*/dist/*' -prune -o \
    -path '*/.git' -prune -o \
    -path '*/.git/*' -prune -o \
    -type f -newer "$AEC_DIST" -print -quit 2>/dev/null | grep -q .
}

if [ -s "$AEC_DIST" ] && [ "${SUVOS_REFRESH_AEC:-0}" != "1" ]; then
  if ! aec_source_is_newer_than_artifact; then
    echo "aec artifact ready: $AEC_DIST"
    exit 0
  fi
  echo "aec artifact is stale, rebuilding: $AEC_DIST"
fi

BUILD_SCRIPT="$AEC_REPO/scripts/build-aec-artifact.sh"
if [ ! -x "$BUILD_SCRIPT" ]; then
  echo "AEC artifact is missing and build script is unavailable: $BUILD_SCRIPT" >&2
  echo "Create the sibling admin-explorer-code repo or set SUVOS_AEC_DIST." >&2
  exit 1
fi

AEC_TARGET_ARCH="$AEC_TARGET_ARCH" "$BUILD_SCRIPT"

if [ ! -s "$AEC_DIST" ]; then
  echo "AEC artifact build finished but artifact is missing: $AEC_DIST" >&2
  exit 1
fi

echo "aec artifact ready: $AEC_DIST"
