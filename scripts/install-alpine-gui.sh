#!/usr/bin/env bash
set -euo pipefail

if [ "$#" -ne 1 ]; then
  echo "usage: $0 <rootfs>" >&2
  exit 2
fi

ROOTFS="$1"
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CURSOR_THEME_PACKAGE="${SUVOS_CURSOR_THEME_PACKAGE:-adwaita-icon-theme}"
GUI_PACKAGES="${SUVOS_GUI_PACKAGES:-cage xwayland dbus seatd eudev su-exec font-dejavu $CURSOR_THEME_PACKAGE gsettings-desktop-schemas xkeyboard-config mesa-dri-gallium mesa-egl mesa-gbm alsa-utils libinput-tools pciutils iw wpa_supplicant util-linux-misc bluez}"
GUI_POST_INSTALL='
if [ -x /suvos-root/usr/bin/glib-compile-schemas ] && [ -d /suvos-root/usr/share/glib-2.0/schemas ]; then
  glib_log=/tmp/suvos-glib-compile-schemas.log
  if ! chroot /suvos-root /usr/bin/glib-compile-schemas /usr/share/glib-2.0/schemas 2>"$glib_log"; then
    cat "$glib_log" >&2
    exit 1
  fi
  if [ -s "$glib_log" ]; then
    grep -v "Paths starting with .* are deprecated" "$glib_log" >&2 || true
  fi
fi

for sandbox in \
  /suvos-root/usr/lib/chromium/chrome-sandbox \
  /suvos-root/usr/lib/chromium/chrome_sandbox; do
  if [ -f "$sandbox" ]; then
    chown 0:0 "$sandbox"
    chmod 4755 "$sandbox"
    break
  fi
done
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
