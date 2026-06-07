#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
. "$ROOT_DIR/scripts/suvos-arch.sh"
ARCH="$(suvos_arch)"
AEC_TARGET_ARCH="$(suvos_aec_target_arch "$ARCH")"
LOCKFILE="${SUVOS_VENDORS_LOCKFILE:-$ROOT_DIR/third_party/vendors.lock.json}"
LEGACY_AEC_REPO="$ROOT_DIR/../admin-explorer-code"

lock_value() {
  python3 "$ROOT_DIR/scripts/vendor-lock.py" --lockfile "$LOCKFILE" get aec "$1" 2>/dev/null || true
}

LOCKED_AEC_PATH="$(lock_value path)"
LOCKED_AEC_REF="$(lock_value ref)"
LOCKED_AEC_REPO=""
if [ -n "$LOCKED_AEC_PATH" ]; then
  LOCKED_AEC_REPO="$ROOT_DIR/$LOCKED_AEC_PATH"
fi

if [ -n "${SUVOS_AEC_DIST:-}" ]; then
  AEC_DIST="$SUVOS_AEC_DIST"
  if [ -s "$AEC_DIST" ] && [ "${SUVOS_REFRESH_AEC:-0}" != "1" ]; then
    echo "aec artifact ready: $AEC_DIST"
    exit 0
  fi
else
  AEC_DIST=""
fi

resolve_aec_repo() {
  if [ -n "${SUVOS_AEC_REPO:-}" ]; then
    printf '%s\n' "$SUVOS_AEC_REPO"
    return 0
  fi

  if [ -n "$LOCKED_AEC_REPO" ] && [ -d "$LOCKED_AEC_REPO" ]; then
    printf '%s\n' "$LOCKED_AEC_REPO"
    return 0
  fi

  if [ -d "$LEGACY_AEC_REPO" ]; then
    printf '%s\n' "$LEGACY_AEC_REPO"
    return 0
  fi

  if [ -n "$LOCKED_AEC_REPO" ]; then
    echo "AEC checkout not found: $LOCKED_AEC_REPO" >&2
    echo "Run: $ROOT_DIR/scripts/bootstrap-vendors.sh aec" >&2
  else
    echo "AEC checkout is not configured in $LOCKFILE" >&2
  fi
  echo "Or set SUVOS_AEC_REPO / SUVOS_AEC_DIST." >&2
  exit 1
}

AEC_REPO="$(resolve_aec_repo)"
if [ -z "$AEC_DIST" ]; then
  if [ "$ARCH" = "x86_64" ]; then
    AEC_DIST="$AEC_REPO/dist/aec-rootfs.tar.gz"
  else
    AEC_DIST="$AEC_REPO/dist/aec-rootfs-$ARCH.tar.gz"
  fi
fi

if [ -z "${SUVOS_AEC_REPO:-}" ] && [ "$AEC_REPO" = "$LOCKED_AEC_REPO" ] && [ -n "$LOCKED_AEC_REF" ]; then
  current_ref="$(git -C "$AEC_REPO" rev-parse HEAD 2>/dev/null || true)"
  if [ -n "$current_ref" ] && [ "$current_ref" != "$LOCKED_AEC_REF" ]; then
    echo "warning: AEC checkout is at $current_ref, lockfile pins $LOCKED_AEC_REF" >&2
    echo "Run: $ROOT_DIR/scripts/bootstrap-vendors.sh aec" >&2
  fi
fi

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
