#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT_DIR="$ROOT_DIR/build/cpp"
OUT="$OUT_DIR/cpp-hello"
SRC="$ROOT_DIR/src/cpp/hello.cpp"
IMAGE="${SUVOS_CPP_BUILDER_IMAGE:-alpine:3.22}"

mkdir -p "$OUT_DIR"

if [ -x "$OUT" ] && [ "$OUT" -nt "$SRC" ] && file "$OUT" | grep -q 'ELF 64-bit'; then
  file "$OUT"
  exit 0
fi

fallback() {
  cat >"$OUT" <<'EOF'
#!/bin/sh
echo "C++ demo binary was not built on the host."
echo "Source is available in the project tree at src/cpp/hello.cpp."
EOF
  chmod +x "$OUT"
}

if [ "${SUVOS_SKIP_CPP_BUILD:-0}" = "1" ]; then
  fallback
  exit 0
fi

if ! command -v docker >/dev/null 2>&1 || ! docker info >/dev/null 2>&1; then
  echo "docker is not available; using C++ demo fallback" >&2
  fallback
  exit 0
fi

docker run --rm --platform linux/amd64 \
  -v "$ROOT_DIR:/work" \
  -w /work \
  "$IMAGE" \
  sh -lc 'apk add --no-cache g++ >/dev/null && g++ -static -Os -s -o build/cpp/cpp-hello src/cpp/hello.cpp'

file "$OUT"
