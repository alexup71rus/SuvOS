#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CHROMIUM_DIR="${SUVOS_CHROMIUM_SOURCE_DIR:-$ROOT_DIR/third_party/SuvOS_Chromium}"
OUT_DIR="${SUVOS_CHROMIUM_OUT_DIR:-out/Release}"
TARGET="${SUVOS_CHROMIUM_TARGET:-chrome}"
JOBS="${SUVOS_CHROMIUM_JOBS:-10}"
LOG_FILE="${SUVOS_CHROMIUM_BUILD_LOG:-$ROOT_DIR/build/ninja-chrome.log}"
NODE_MODULES_DIR="$ROOT_DIR/node_modules"
HIDDEN_NODE_MODULES_DIR="$ROOT_DIR/node_modules.hidden-during-chromium-build"
MOVED_NODE_MODULES=0

if [[ ! "$JOBS" =~ ^[0-9]+$ ]] || [[ "$JOBS" -lt 1 ]]; then
  echo "SUVOS_CHROMIUM_JOBS must be a positive integer, got: $JOBS" >&2
  exit 2
fi

if [[ ! -d "$CHROMIUM_DIR" ]]; then
  echo "Chromium source checkout not found: $CHROMIUM_DIR" >&2
  exit 2
fi

SISO="$CHROMIUM_DIR/third_party/siso/cipd/siso"
if [[ ! -x "$SISO" ]]; then
  echo "siso not found or not executable: $SISO" >&2
  echo "Run gclient sync for the Chromium checkout first." >&2
  exit 2
fi

restore_node_modules() {
  if [[ "$MOVED_NODE_MODULES" -eq 1 && -d "$HIDDEN_NODE_MODULES_DIR" ]]; then
    mv "$HIDDEN_NODE_MODULES_DIR" "$NODE_MODULES_DIR"
  fi
}
trap restore_node_modules EXIT

mkdir -p "$(dirname "$LOG_FILE")"

if [[ ! -e "$NODE_MODULES_DIR" && -d "$HIDDEN_NODE_MODULES_DIR" ]]; then
  echo "Restoring previously hidden node_modules: $HIDDEN_NODE_MODULES_DIR"
  mv "$HIDDEN_NODE_MODULES_DIR" "$NODE_MODULES_DIR"
fi

if [[ -d "$NODE_MODULES_DIR" ]]; then
  if [[ -e "$HIDDEN_NODE_MODULES_DIR" ]]; then
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
echo "Log:             $LOG_FILE"

cd "$CHROMIUM_DIR"
"$SISO" ninja -C "$OUT_DIR" -local_jobs "$JOBS" "$TARGET" 2>&1 | tee "$LOG_FILE"
