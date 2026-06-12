#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
. "$ROOT_DIR/scripts/suvos-arch.sh"

LOCKFILE="${SUVOS_VENDORS_LOCKFILE:-$ROOT_DIR/third_party/vendors.lock.json}"
ARCH="$(suvos_arch)"
HOST_OS="$(uname -s)"

lock_value() {
  python3 "$ROOT_DIR/scripts/vendor-lock.py" --lockfile "$LOCKFILE" get chromium "$1" 2>/dev/null || true
}

resolve_chromium_source_dir() {
  if [ -n "${SUVOS_CHROMIUM_SOURCE_DIR:-}" ]; then
    printf '%s\n' "$SUVOS_CHROMIUM_SOURCE_DIR"
    return 0
  fi
  if [ -n "${SUVOS_CHROMIUM_REPO:-}" ]; then
    printf '%s\n' "$SUVOS_CHROMIUM_REPO"
    return 0
  fi

  locked_path="$(lock_value path)"
  if [ -n "$locked_path" ] && [ -d "$ROOT_DIR/$locked_path" ]; then
    printf '%s\n' "$ROOT_DIR/$locked_path"
    return 0
  fi

  if [ -d "$ROOT_DIR/third_party/SuvOS_Chromium" ]; then
    printf '%s\n' "$ROOT_DIR/third_party/SuvOS_Chromium"
    return 0
  fi

  echo "Chromium source checkout not found." >&2
  echo "Run bootstrap for the vendor checkout or set SUVOS_CHROMIUM_SOURCE_DIR." >&2
  exit 2
}

default_out_dir() {
  case "$HOST_OS:$ARCH" in
    Darwin:*)
      printf '%s\n' "out/Release"
      ;;
    Linux:x86_64)
      printf '%s\n' "out/Linux_x64"
      ;;
    Linux:aarch64)
      printf '%s\n' "out/Linux_arm64"
      ;;
    *)
      printf '%s\n' "out/Release"
      ;;
  esac
}

run_build_tool() {
  if [ -x "$CHROMIUM_DIR/third_party/siso/cipd/siso" ]; then
    siso_args=()
    if [ "${SUVOS_CHROMIUM_SISO_FAST_LOCAL:-1}" = "1" ]; then
      siso_args+=("-fast_local")
    fi
    "$CHROMIUM_DIR/third_party/siso/cipd/siso" ninja \
      -C "$OUT_DIR" \
      -local_jobs "$JOBS" \
      "${siso_args[@]}" \
      "$TARGET"
    return
  fi

  if command -v autoninja >/dev/null 2>&1; then
    autoninja -C "$OUT_DIR" "$TARGET" -j "$JOBS"
    return
  fi

  ninja -C "$OUT_DIR" -j "$JOBS" "$TARGET"
}

restore_node_modules() {
  if [ "$MOVED_NODE_MODULES" -eq 1 ] && [ -d "$HIDDEN_NODE_MODULES_DIR" ]; then
    mv "$HIDDEN_NODE_MODULES_DIR" "$NODE_MODULES_DIR"
  fi
}

CHROMIUM_DIR="$(resolve_chromium_source_dir)"
OUT_DIR="${SUVOS_CHROMIUM_OUT_DIR:-$(default_out_dir)}"
TARGET="${SUVOS_CHROMIUM_TARGET:-chrome}"
JOBS="${SUVOS_CHROMIUM_JOBS:-10}"
LOG_FILE="${SUVOS_CHROMIUM_BUILD_LOG-$ROOT_DIR/build/ninja-chrome.log}"
NODE_MODULES_DIR="$ROOT_DIR/node_modules"
HIDDEN_NODE_MODULES_DIR="$ROOT_DIR/node_modules.hidden-during-chromium-build"
MOVED_NODE_MODULES=0

if [ "${OUT_DIR#/}" = "$OUT_DIR" ]; then
  OUT_DIR="$CHROMIUM_DIR/$OUT_DIR"
fi

if ! [[ "$JOBS" =~ ^[0-9]+$ ]] || [ "$JOBS" -lt 1 ]; then
  echo "SUVOS_CHROMIUM_JOBS must be a positive integer, got: $JOBS" >&2
  exit 2
fi

[ -d "$CHROMIUM_DIR" ] || {
  echo "Chromium source checkout not found: $CHROMIUM_DIR" >&2
  exit 2
}
[ -d "$OUT_DIR" ] || {
  echo "Chromium output directory not found: $OUT_DIR" >&2
  exit 2
}

trap restore_node_modules EXIT

if [ ! -e "$NODE_MODULES_DIR" ] && [ -d "$HIDDEN_NODE_MODULES_DIR" ]; then
  echo "Restoring previously hidden node_modules: $HIDDEN_NODE_MODULES_DIR"
  mv "$HIDDEN_NODE_MODULES_DIR" "$NODE_MODULES_DIR"
fi

if [ -d "$NODE_MODULES_DIR" ]; then
  if [ -e "$HIDDEN_NODE_MODULES_DIR" ]; then
    echo "Refusing to move node_modules: already exists: $HIDDEN_NODE_MODULES_DIR" >&2
    exit 2
  fi
  mv "$NODE_MODULES_DIR" "$HIDDEN_NODE_MODULES_DIR"
  MOVED_NODE_MODULES=1
fi

echo "Chromium source: $CHROMIUM_DIR"
echo "Output dir:      $OUT_DIR"
echo "Target:          $TARGET"
echo "Jobs:            $JOBS"
if [ -n "$LOG_FILE" ]; then
  echo "Log:             $LOG_FILE"
  mkdir -p "$(dirname "$LOG_FILE")"
  cd "$CHROMIUM_DIR"
  run_build_tool 2>&1 | tee "$LOG_FILE"
else
  echo "Log:             stdout"
  cd "$CHROMIUM_DIR"
  run_build_tool
fi
