#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
. "$ROOT_DIR/scripts/suvos-arch.sh"

default_arch() {
  case "$(uname -m)" in
    arm64|aarch64) echo "aarch64" ;;
    *) echo "x86_64" ;;
  esac
}

SUVOS_ARCH="${SUVOS_ARCH:-$(default_arch)}"
ARCH="$(suvos_arch)"
DOCKER_PLATFORM="$(suvos_docker_platform "$ARCH")"
PORT="${SUVOS_DEV_GATEWAY_PORT:-8080}"
IMAGE="${SUVOS_DEV_GATEWAY_IMAGE:-alpine:3.22}"
CONTAINER="${SUVOS_DEV_GATEWAY_CONTAINER:-suvos-chromium-dev-gateway}"
WORK_DIR="$ROOT_DIR/build/chromium-dev-gateway"
SYSTEM_STAGE="$WORK_DIR/system/suvos"

case "$PORT" in
  ''|*[!0-9]*)
    echo "SUVOS_DEV_GATEWAY_PORT must be numeric" >&2
    exit 2
    ;;
esac

if ! command -v docker >/dev/null 2>&1 || ! docker info >/dev/null 2>&1; then
  echo "docker is required to run the Chromium development gateway" >&2
  exit 1
fi

if docker ps -a --format '{{.Names}}' | grep -qx "$CONTAINER"; then
  echo "docker container '$CONTAINER' already exists" >&2
  echo "Stop it with: docker rm -f $CONTAINER" >&2
  exit 1
fi

SUVOS_ARCH="$ARCH" "$ROOT_DIR/scripts/build-suvosd.sh"
SUVOS_ARCH="$ARCH" "$ROOT_DIR/scripts/build-suvos-gateway.sh"
"$ROOT_DIR/scripts/build-ui.sh"

rm -rf "$WORK_DIR"
mkdir -p \
  "$SYSTEM_STAGE/bin" \
  "$SYSTEM_STAGE/apps/manifest.d" \
  "$SYSTEM_STAGE/config" \
  "$SYSTEM_STAGE/lib" \
  "$SYSTEM_STAGE/security" \
  "$SYSTEM_STAGE/ui"

cp "$ROOT_DIR/build/suvosd/suvosd-$ARCH" "$SYSTEM_STAGE/bin/suvosd"
cp "$ROOT_DIR/build/suvos-gateway/suvos-gateway-$ARCH" "$SYSTEM_STAGE/bin/suvos-gateway"
cp -R "$ROOT_DIR/build/ui/." "$SYSTEM_STAGE/ui/"
cp "$ROOT_DIR/os/rootfs/system/suvos/config/locale.conf" "$SYSTEM_STAGE/config/locale.conf"
cp "$ROOT_DIR/os/rootfs/system/suvos/lib/i18n.sh" "$SYSTEM_STAGE/lib/i18n.sh"
cp "$ROOT_DIR/os/rootfs/system/suvos/security/roles.conf" "$SYSTEM_STAGE/security/roles.conf"

ROOT_BOOTSTRAP_HASH="$("$ROOT_DIR/scripts/generate-root-secret.sh")"
cp "$ROOT_BOOTSTRAP_HASH" "$SYSTEM_STAGE/security/root-bootstrap.sha256"

chmod +x "$SYSTEM_STAGE/bin/suvosd" "$SYSTEM_STAGE/bin/suvos-gateway"

echo "chromium dev gateway: http://127.0.0.1:$PORT/"
echo "chromium dev gateway: arch=$ARCH platform=$DOCKER_PLATFORM"

exec docker run --rm \
  --platform "$DOCKER_PLATFORM" \
  --name "$CONTAINER" \
  -p "127.0.0.1:$PORT:8080" \
  --mount "type=bind,source=$SYSTEM_STAGE,target=/system/suvos,readonly" \
  --tmpfs /run:rw,exec,nosuid,size=64m \
  --tmpfs /data:rw,exec,nosuid,size=128m \
  "$IMAGE" \
  sh -lc '
    set -eu
    apk add --no-cache socat >/dev/null
    mkdir -p /data/suvos /run/suvosd
    /system/suvos/bin/suvosd &
    suvosd_pid="$!"

    i=0
    while [ "$i" -lt 100 ]; do
      [ -S /run/suvosd/control.sock ] && break
      i=$((i + 1))
      sleep 0.05
    done

    /system/suvos/bin/suvos-gateway &
    gateway_pid="$!"

    cleanup() {
      kill "$gateway_pid" "$suvosd_pid" 2>/dev/null || true
    }
    trap cleanup INT TERM EXIT

    exec socat TCP-LISTEN:8080,bind=0.0.0.0,reuseaddr,fork TCP:127.0.0.1:80
  '
