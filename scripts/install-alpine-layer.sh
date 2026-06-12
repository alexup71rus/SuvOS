#!/usr/bin/env bash
set -euo pipefail

if [ "$#" -ne 4 ]; then
  echo "usage: $0 <layer-name> <rootfs> <packages> <post-install-command>" >&2
  exit 2
fi

LAYER_NAME="$1"
ROOTFS="$2"
PACKAGES="$3"
POST_INSTALL="$4"

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
. "$ROOT_DIR/scripts/suvos-arch.sh"
ARCH="$(suvos_arch)"
DOCKER_PLATFORM="$(suvos_docker_platform "$ARCH")"
ALPINE_VERSION="${SUVOS_ALPINE_VERSION:-3.22}"
IMAGE="${SUVOS_ALPINE_IMAGE:-alpine:$ALPINE_VERSION}"
LAYER_SCHEMA="3"
REFRESH_LAYER_CACHE="${SUVOS_REFRESH_LAYER_CACHE:-0}"
DISABLE_LAYER_CACHE="${SUVOS_DISABLE_LAYER_CACHE:-0}"

[ -d "$ROOTFS" ] || {
  echo "rootfs directory does not exist: $ROOTFS" >&2
  exit 1
}

install_into_root() {
  local target_root="$1"

  if ! command -v docker >/dev/null 2>&1 || ! docker info >/dev/null 2>&1; then
    cat >&2 <<EOF
docker is required to install Alpine packages for layer: $LAYER_NAME.
Start OrbStack/Docker and run make again.
EOF
    exit 1
  fi

  docker run --rm --platform "$DOCKER_PLATFORM" \
    -e LAYER_PACKAGES="$PACKAGES" \
    -e LAYER_POST_INSTALL="$POST_INSTALL" \
    -e SUVOS_HOST_UID="$(id -u)" \
    -e SUVOS_HOST_GID="$(id -g)" \
    -v "$target_root:/suvos-root" \
    -v "$APK_CACHE_DIR:/apk-cache" \
    "$IMAGE" \
    sh -eu -c '
      apk --root /suvos-root \
        --initdb \
        --keys-dir /etc/apk/keys \
        --repositories-file /etc/apk/repositories \
        --cache-dir /apk-cache \
        --update-cache \
        --no-scripts \
        add $LAYER_PACKAGES

      if [ -n "$LAYER_POST_INSTALL" ]; then
        sh -eu -c "$LAYER_POST_INSTALL"
      fi

      rm -rf /suvos-root/var/cache/apk/*
      find /suvos-root/dev -mindepth 1 -maxdepth 1 -exec rm -rf {} + 2>/dev/null || true
      chown -R "$SUVOS_HOST_UID:$SUVOS_HOST_GID" /suvos-root /apk-cache 2>/dev/null || true
    '
}

hash_layer_key() {
  {
    printf 'schema=%s\n' "$LAYER_SCHEMA"
    printf 'layer=%s\n' "$LAYER_NAME"
    printf 'arch=%s\n' "$ARCH"
    printf 'docker_platform=%s\n' "$DOCKER_PLATFORM"
    printf 'image=%s\n' "$IMAGE"
    printf 'alpine_version=%s\n' "$ALPINE_VERSION"
    printf 'packages=%s\n' "$PACKAGES"
    printf 'post_install=%s\n' "$POST_INSTALL"
  } | {
    if command -v shasum >/dev/null 2>&1; then
      shasum -a 256 | awk '{ print $1 }'
    elif command -v sha256sum >/dev/null 2>&1; then
      sha256sum | awk '{ print $1 }'
    else
      python3 -c 'import hashlib, sys; print(hashlib.sha256(sys.stdin.buffer.read()).hexdigest())'
    fi
  }
}

cache_key="$(hash_layer_key)"

LAYER_CACHE_DIR="$ROOT_DIR/build/cache/rootfs-layers"
APK_CACHE_DIR="$ROOT_DIR/build/cache/apk/$ALPINE_VERSION/$ARCH"
LAYER_TAR="$LAYER_CACHE_DIR/$LAYER_NAME-$cache_key.tar"
LAYER_META="$LAYER_TAR.meta"

mkdir -p "$LAYER_CACHE_DIR" "$APK_CACHE_DIR"

if [ "$DISABLE_LAYER_CACHE" = "1" ]; then
  echo "alpine layer cache disabled: $LAYER_NAME"
  install_into_root "$ROOTFS"
  exit 0
fi

if [ "$REFRESH_LAYER_CACHE" != "1" ] && [ -s "$LAYER_TAR" ]; then
  echo "alpine layer cache hit: $LAYER_NAME"
  tar -C "$ROOTFS" -xf "$LAYER_TAR"
  exit 0
fi

echo "alpine layer cache miss: $LAYER_NAME"

TMP_ROOT="$LAYER_CACHE_DIR/.tmp-$LAYER_NAME-$$"
TMP_TAR="$LAYER_TAR.tmp"
cleanup() {
  rm -rf "$TMP_ROOT" "$TMP_TAR"
}
trap cleanup EXIT

rm -rf "$TMP_ROOT" "$TMP_TAR"
mkdir -p "$TMP_ROOT"

install_into_root "$TMP_ROOT"

tar -C "$TMP_ROOT" -cf "$TMP_TAR" .
mv "$TMP_TAR" "$LAYER_TAR"
cat >"$LAYER_META" <<EOF
schema=$LAYER_SCHEMA
layer=$LAYER_NAME
image=$IMAGE
alpine_version=$ALPINE_VERSION
arch=$ARCH
docker_platform=$DOCKER_PLATFORM
packages=$PACKAGES
cache_key=$cache_key
EOF

tar -C "$ROOTFS" -xf "$LAYER_TAR"
