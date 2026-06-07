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
DOCKER_PLATFORM="$(suvos_docker_platform "$ARCH")"
IMAGE="${SUVOS_GLIBC_IMAGE:-debian:bookworm-slim}"
CACHE_VERSION="v3"
CACHE_DIR="$ROOT_DIR/build/cache/debian-glibc"
CACHE_KEY="$(printf '%s-%s-%s' "$IMAGE" "$ARCH" "$CACHE_VERSION" | tr '/:@' '___')"
PAYLOAD="$CACHE_DIR/$CACHE_KEY.tar.gz"

case "$ARCH" in
  x86_64)
    DEBIAN_MULTIARCH="x86_64-linux-gnu"
    GLIBC_LOADER="ld-linux-x86-64.so.2"
    GLIBC_LOADER_LINK="lib64/ld-linux-x86-64.so.2"
    ;;
  aarch64)
    DEBIAN_MULTIARCH="aarch64-linux-gnu"
    GLIBC_LOADER="ld-linux-aarch64.so.1"
    GLIBC_LOADER_LINK="lib/ld-linux-aarch64.so.1"
    ;;
  *)
    echo "unsupported SUVOS_ARCH for glibc payload: $ARCH" >&2
    exit 2
    ;;
esac

[ -d "$ROOTFS" ] || {
  echo "rootfs directory does not exist: $ROOTFS" >&2
  exit 1
}

mkdir -p "$CACHE_DIR"

if [ ! -s "$PAYLOAD" ] || [ "${SUVOS_REFRESH_GLIBC_CACHE:-0}" = "1" ]; then
  if ! command -v docker >/dev/null 2>&1 || ! docker info >/dev/null 2>&1; then
    echo "docker is required to prepare the AEC glibc payload" >&2
    exit 1
  fi

  rm -f "$PAYLOAD"
  docker run --rm --platform "$DOCKER_PLATFORM" \
    -e DEBIAN_MULTIARCH="$DEBIAN_MULTIARCH" \
    -e GLIBC_LOADER="$GLIBC_LOADER" \
    -e GLIBC_LOADER_LINK="$GLIBC_LOADER_LINK" \
    -v "$CACHE_DIR:/out" \
    "$IMAGE" \
    sh -lc '
      set -eu
      stage=/tmp/suvos-glibc
      rm -rf "$stage"
      mkdir -p "$stage/lib/$DEBIAN_MULTIARCH" "$stage/$(dirname "$GLIBC_LOADER_LINK")" "$stage/usr/share/doc/suvos-aec-glibc"

      for name in \
        "$GLIBC_LOADER" \
        libc.so.6 \
        libdl.so.2 \
        libgcc_s.so.1 \
        libm.so.6 \
        libnss_dns.so.2 \
        libnss_files.so.2 \
        libpthread.so.0 \
        libresolv.so.2 \
        librt.so.1 \
        libstdc++.so.6 \
        libutil.so.1; do
        for path in /lib/$DEBIAN_MULTIARCH/$name /usr/lib/$DEBIAN_MULTIARCH/$name; do
          [ -e "$path" ] || continue
          cp -a "$path" "$stage/lib/$DEBIAN_MULTIARCH/"
          resolved="$(readlink -f "$path" 2>/dev/null || true)"
          if [ -n "$resolved" ] && [ "$resolved" != "$path" ] && [ -e "$resolved" ]; then
            cp -a "$resolved" "$stage/lib/$DEBIAN_MULTIARCH/"
          fi
        done
      done

      ln -sfn "/lib/$DEBIAN_MULTIARCH/$GLIBC_LOADER" "$stage/$GLIBC_LOADER_LINK"

      for doc in \
        /usr/share/doc/libc6/copyright \
        /usr/share/doc/libstdc++6/copyright \
        /usr/share/doc/libgcc-s1/copyright; do
        [ -r "$doc" ] || continue
        cp "$doc" "$stage/usr/share/doc/suvos-aec-glibc/$(basename "$(dirname "$doc")").copyright"
      done

      tar -C "$stage" -czf /out/payload.tar.gz .
    '
  mv "$CACHE_DIR/payload.tar.gz" "$PAYLOAD"
fi

tar -C "$ROOTFS" -xzf "$PAYLOAD"
echo "installed AEC glibc payload from $IMAGE for $ARCH"
