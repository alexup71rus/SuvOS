#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT_DIR="$ROOT_DIR/build/suvos-gateway"
OUT="$OUT_DIR/suvos-gateway"
SRC="$ROOT_DIR/src/suvos-gateway/suvos-gateway.cpp"
IMAGE="${SUVOS_CPP_BUILDER_IMAGE:-alpine:3.22}"

mkdir -p "$OUT_DIR"

if [ -x "$OUT" ] && [ "$OUT" -nt "$SRC" ] && file "$OUT" | grep -q 'ELF 64-bit'; then
  file "$OUT"
  exit 0
fi

if ! command -v docker >/dev/null 2>&1 || ! docker info >/dev/null 2>&1; then
  echo "docker is required to build the x86_64 C++ suvos-gateway binary" >&2
  exit 1
fi

docker run --rm --platform linux/amd64 \
  -v "$ROOT_DIR:/work" \
  -w /work \
  "$IMAGE" \
  sh -lc 'apk add --no-cache g++ >/dev/null && g++ -std=c++17 -static -Os -s -Wall -Wextra -o build/suvos-gateway/suvos-gateway src/suvos-gateway/suvos-gateway.cpp'

file "$OUT"
