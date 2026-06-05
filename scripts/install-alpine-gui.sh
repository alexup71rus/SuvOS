#!/usr/bin/env bash
set -euo pipefail

if [ "$#" -ne 1 ]; then
  echo "usage: $0 <rootfs>" >&2
  exit 2
fi

ROOTFS="$1"
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CURSOR_THEME_PACKAGE="${SUVOS_CURSOR_THEME_PACKAGE:-adwaita-icon-theme}"
GUI_PACKAGES="${SUVOS_GUI_PACKAGES:-cage chromium xwayland dbus seatd eudev font-dejavu $CURSOR_THEME_PACKAGE gsettings-desktop-schemas xkeyboard-config mesa-dri-gallium mesa-egl mesa-gbm alsa-utils libinput-tools pciutils}"
GUI_POST_INSTALL='
if [ -x /suvos-root/usr/bin/glib-compile-schemas ] && [ -d /suvos-root/usr/share/glib-2.0/schemas ]; then
  chroot /suvos-root /usr/bin/glib-compile-schemas /usr/share/glib-2.0/schemas
fi
'

[ -d "$ROOTFS" ] || {
  echo "rootfs directory does not exist: $ROOTFS" >&2
  exit 1
}

"$ROOT_DIR/scripts/install-alpine-layer.sh" \
  gui \
  "$ROOTFS" \
  "$GUI_PACKAGES" \
  "$GUI_POST_INSTALL"
