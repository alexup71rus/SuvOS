#!/usr/bin/env bash
set -euo pipefail

if [ "$#" -ne 1 ]; then
  echo "usage: $0 <rootfs>" >&2
  exit 2
fi

ROOTFS="$1"
ALPINE_VERSION="${SUVOS_ALPINE_VERSION:-3.22}"
IMAGE="${SUVOS_ALPINE_IMAGE:-alpine:$ALPINE_VERSION}"

[ -d "$ROOTFS" ] || {
  echo "rootfs directory does not exist: $ROOTFS" >&2
  exit 1
}

if ! command -v docker >/dev/null 2>&1 || ! docker info >/dev/null 2>&1; then
  cat >&2 <<'EOF'
docker is required to install Python and Node.js into the x86_64 Alpine rootfs.
Start OrbStack/Docker and run make again.
EOF
  exit 1
fi

docker run --rm --platform linux/amd64 \
  -v "$ROOTFS:/suvos-root" \
  "$IMAGE" \
  sh -eu -c '
    apk --root /suvos-root \
      --initdb \
      --keys-dir /etc/apk/keys \
      --repositories-file /etc/apk/repositories \
      --no-cache \
      --no-scripts \
      add python3 nodejs ca-certificates

    rm -rf /suvos-root/var/cache/apk/*
  '
