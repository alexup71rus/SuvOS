#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
. "$ROOT_DIR/scripts/suvos-arch.sh"
ARCH="$(suvos_arch)"
DOCKER_PLATFORM="$(suvos_docker_platform "$ARCH")"
ELF_ARCH_PATTERN="$(suvos_elf_arch_pattern "$ARCH")"
OUT_DIR="$ROOT_DIR/build/suvosctl"
OUT="$OUT_DIR/suvosctl-$ARCH"
LEGACY_OUT="$OUT_DIR/suvosctl"
SRC="$ROOT_DIR/src/suvosctl/suvosctl.cpp"
IMAGE="${SUVOS_CPP_BUILDER_IMAGE:-alpine:3.22}"

mkdir -p "$OUT_DIR"

if [ -x "$OUT" ] && [ "$OUT" -nt "$SRC" ] && file "$OUT" | grep -Eq "ELF 64-bit.*($ELF_ARCH_PATTERN)"; then
  file "$OUT"
  exit 0
fi

if ! command -v docker >/dev/null 2>&1 || ! docker info >/dev/null 2>&1; then
  echo "docker is required to build the $ARCH C++ suvosctl binary" >&2
  exit 1
fi

docker run --rm --platform "$DOCKER_PLATFORM" \
  -e SUVOS_CPP_OUT="build/suvosctl/suvosctl-$ARCH" \
  -v "$ROOT_DIR:/work" \
  -w /work \
  "$IMAGE" \
  sh -lc 'apk add --no-cache g++ >/dev/null && g++ -std=c++17 -static -Os -s -Wall -Wextra -o "$SUVOS_CPP_OUT" src/suvosctl/suvosctl.cpp'

if [ "$ARCH" = "x86_64" ]; then
  ln -sfn "$(basename "$OUT")" "$LEGACY_OUT"
fi

file "$OUT"
