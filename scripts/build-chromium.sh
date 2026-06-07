#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
. "$ROOT_DIR/scripts/suvos-arch.sh"
ARCH="$(suvos_arch)"
LOCKFILE="${SUVOS_VENDORS_LOCKFILE:-$ROOT_DIR/third_party/vendors.lock.json}"
LEGACY_CHROMIUM_REPO="$ROOT_DIR/../SuvOS_Chromium"

lock_value() {
  python3 "$ROOT_DIR/scripts/vendor-lock.py" --lockfile "$LOCKFILE" get chromium "$1" 2>/dev/null || true
}

LOCKED_CHROMIUM_PATH="$(lock_value path)"
LOCKED_CHROMIUM_REF="$(lock_value ref)"
LOCKED_CHROMIUM_REPO=""
if [ -n "$LOCKED_CHROMIUM_PATH" ]; then
  LOCKED_CHROMIUM_REPO="$ROOT_DIR/$LOCKED_CHROMIUM_PATH"
fi

if [ -n "${SUVOS_CHROMIUM_DIST:-}" ]; then
  CHROMIUM_DIST="$SUVOS_CHROMIUM_DIST"
  if [ -s "$CHROMIUM_DIST" ] && [ "${SUVOS_REFRESH_CHROMIUM:-0}" != "1" ]; then
    echo "chromium artifact ready: $CHROMIUM_DIST"
    exit 0
  fi
else
  CHROMIUM_DIST=""
fi

resolve_chromium_repo() {
  if [ -n "${SUVOS_CHROMIUM_REPO:-}" ]; then
    printf '%s\n' "$SUVOS_CHROMIUM_REPO"
    return 0
  fi

  if [ -n "$LOCKED_CHROMIUM_REPO" ] && [ -d "$LOCKED_CHROMIUM_REPO" ]; then
    printf '%s\n' "$LOCKED_CHROMIUM_REPO"
    return 0
  fi

  if [ -d "$LEGACY_CHROMIUM_REPO" ]; then
    printf '%s\n' "$LEGACY_CHROMIUM_REPO"
    return 0
  fi

  if [ -n "$LOCKED_CHROMIUM_REPO" ]; then
    echo "Chromium checkout not found: $LOCKED_CHROMIUM_REPO" >&2
    echo "Run: $ROOT_DIR/scripts/bootstrap-vendors.sh chromium" >&2
  else
    echo "Chromium checkout is not configured in $LOCKFILE" >&2
  fi
  echo "Or set SUVOS_CHROMIUM_REPO / SUVOS_CHROMIUM_DIST." >&2
  exit 1
}

CHROMIUM_REPO="$(resolve_chromium_repo)"
if [ -z "$CHROMIUM_DIST" ]; then
  if [ "$ARCH" = "x86_64" ]; then
    CHROMIUM_DIST="$CHROMIUM_REPO/dist/chromium-rootfs.tar.gz"
  else
    CHROMIUM_DIST="$CHROMIUM_REPO/dist/chromium-rootfs-$ARCH.tar.gz"
  fi
fi

if [ -z "${SUVOS_CHROMIUM_REPO:-}" ] && [ "$CHROMIUM_REPO" = "$LOCKED_CHROMIUM_REPO" ] && [ -n "$LOCKED_CHROMIUM_REF" ]; then
  current_ref="$(git -C "$CHROMIUM_REPO" rev-parse HEAD 2>/dev/null || true)"
  if [ -n "$current_ref" ] && [ "$current_ref" != "$LOCKED_CHROMIUM_REF" ]; then
    echo "warning: Chromium checkout is at $current_ref, lockfile pins $LOCKED_CHROMIUM_REF" >&2
    echo "Run: $ROOT_DIR/scripts/bootstrap-vendors.sh chromium" >&2
  fi
fi

chromium_source_is_newer_than_artifact() {
  [ -s "$CHROMIUM_DIST" ] || return 0
  [ -d "$CHROMIUM_REPO" ] || return 1

  find \
    "$CHROMIUM_REPO/scripts" \
    "$CHROMIUM_REPO/Makefile" \
    "$CHROMIUM_REPO/README.md" \
    -path '*/dist' -prune -o \
    -path '*/dist/*' -prune -o \
    -path '*/build' -prune -o \
    -path '*/build/*' -prune -o \
    -path '*/.git' -prune -o \
    -path '*/.git/*' -prune -o \
    -type f -newer "$CHROMIUM_DIST" -print -quit 2>/dev/null | grep -q .
}

if [ -s "$CHROMIUM_DIST" ] && [ "${SUVOS_REFRESH_CHROMIUM:-0}" != "1" ]; then
  if ! chromium_source_is_newer_than_artifact; then
    echo "chromium artifact ready: $CHROMIUM_DIST"
    exit 0
  fi
  echo "chromium artifact is stale, rebuilding: $CHROMIUM_DIST"
fi

BUILD_SCRIPT="$CHROMIUM_REPO/scripts/build-chromium-artifact.sh"
if [ ! -x "$BUILD_SCRIPT" ]; then
  echo "Chromium artifact is missing and build script is unavailable: $BUILD_SCRIPT" >&2
  echo "Create the sibling SuvOS_Chromium repo or set SUVOS_CHROMIUM_DIST." >&2
  exit 1
fi

CHROMIUM_TARGET_ARCH="$ARCH" "$BUILD_SCRIPT"

if [ ! -s "$CHROMIUM_DIST" ]; then
  echo "Chromium artifact build finished but artifact is missing: $CHROMIUM_DIST" >&2
  exit 1
fi

echo "chromium artifact ready: $CHROMIUM_DIST"
