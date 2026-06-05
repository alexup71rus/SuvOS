#!/usr/bin/env bash
set -euo pipefail

if [ "$#" -ne 1 ]; then
  echo "usage: $0 <rootfs>" >&2
  exit 2
fi

ROOTFS="$1"
ALPINE_VERSION="${SUVOS_ALPINE_VERSION:-3.22}"
IMAGE="${SUVOS_ALPINE_IMAGE:-alpine:$ALPINE_VERSION}"
CURSOR_THEME_PACKAGE="${SUVOS_CURSOR_THEME_PACKAGE:-adwaita-icon-theme}"
GUI_PACKAGES="${SUVOS_GUI_PACKAGES:-cage chromium xwayland dbus seatd eudev font-dejavu $CURSOR_THEME_PACKAGE gsettings-desktop-schemas xkeyboard-config mesa-dri-gallium mesa-egl mesa-gbm alsa-utils libinput-tools pciutils}"

[ -d "$ROOTFS" ] || {
  echo "rootfs directory does not exist: $ROOTFS" >&2
  exit 1
}

if ! command -v docker >/dev/null 2>&1 || ! docker info >/dev/null 2>&1; then
  cat >&2 <<'EOF'
docker is required to install the x86_64 Wayland/Cage/Chromium GUI runtime.
Start OrbStack/Docker and run make again.
EOF
  exit 1
fi

docker run --rm --platform linux/amd64 \
  -e GUI_PACKAGES="$GUI_PACKAGES" \
  -v "$ROOTFS:/suvos-root" \
  "$IMAGE" \
  sh -eu -c '
    apk --root /suvos-root \
      --initdb \
      --keys-dir /etc/apk/keys \
      --repositories-file /etc/apk/repositories \
      --no-cache \
      --no-scripts \
      add $GUI_PACKAGES

    if [ -x /suvos-root/usr/bin/glib-compile-schemas ] && [ -d /suvos-root/usr/share/glib-2.0/schemas ]; then
      chroot /suvos-root /usr/bin/glib-compile-schemas /usr/share/glib-2.0/schemas
    fi

    rm -rf /suvos-root/var/cache/apk/*
  '
