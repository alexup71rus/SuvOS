#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
. "$ROOT_DIR/scripts/suvos-arch.sh"
ARCH="$(suvos_arch)"
DOCKER_PLATFORM="$(suvos_docker_platform "$ARCH")"
ELF_ARCH_PATTERN="$(suvos_elf_arch_pattern "$ARCH")"
OUT_DIR="$ROOT_DIR/build/suvos-gateway"
OUT="$OUT_DIR/suvos-gateway-$ARCH"
LEGACY_OUT="$OUT_DIR/suvos-gateway"
SRC_DIR="$ROOT_DIR/src/suvos-gateway"
IMAGE="${SUVOS_CPP_BUILDER_IMAGE:-alpine:3.22}"

mkdir -p "$OUT_DIR"

newest_source() {
  find "$SRC_DIR" -type f \( -name '*.cpp' -o -name '*.h' \) -print0 |
    xargs -0 stat -f '%m' 2>/dev/null | sort -nr | head -1
}

source_is_newer_than_out() {
  [ -x "$OUT" ] || return 0
  local out_mtime newest
  out_mtime="$(stat -f '%m' "$OUT" 2>/dev/null || echo 0)"
  newest="$(newest_source)"
  [ -z "$newest" ] && return 1
  [ "$newest" -gt "$out_mtime" ]
}

if [ -x "$OUT" ] && ! source_is_newer_than_out && file "$OUT" | grep -Eq "ELF 64-bit.*($ELF_ARCH_PATTERN)"; then
  file "$OUT"
  exit 0
fi

if ! command -v docker >/dev/null 2>&1 || ! docker info >/dev/null 2>&1; then
  echo "docker is required to build the $ARCH C++ suvos-gateway binary" >&2
  exit 1
fi

docker run --rm --platform "$DOCKER_PLATFORM" \
  -e SUVOS_CPP_OUT="build/suvos-gateway/suvos-gateway-$ARCH" \
  -v "$ROOT_DIR:/work" \
  -w /work \
  "$IMAGE" \
  sh -lc 'apk add --no-cache g++ >/dev/null && g++ -std=c++17 -static -Os -s -Wall -Wextra -I src/suvos-gateway -o "$SUVOS_CPP_OUT" src/suvos-gateway/*.cpp'

if [ "$ARCH" = "x86_64" ]; then
  ln -sfn "$(basename "$OUT")" "$LEGACY_OUT"
fi

file "$OUT"
