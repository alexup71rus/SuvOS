#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ROOTFS="$ROOT_DIR/build/rootfs"
INITRAMFS="$ROOT_DIR/build/initramfs/suvos-initramfs.cpio.gz"
ROOT_BOOTSTRAP_HASH="$("$ROOT_DIR/scripts/generate-root-secret.sh")"
WITH_RUNTIMES="${SUVOS_WITH_RUNTIMES:-1}"

case "$WITH_RUNTIMES" in
  0|1) ;;
  *)
    echo "SUVOS_WITH_RUNTIMES must be 0 or 1" >&2
    exit 2
    ;;
esac

if [ "$WITH_RUNTIMES" = "1" ]; then
  BUILD_PROFILE="${SUVOS_BUILD_PROFILE:-full}"
else
  BUILD_PROFILE="${SUVOS_BUILD_PROFILE:-core}"
fi

python3 "$ROOT_DIR/tools/fetch_alpine_assets.py"

chmod -R u+w "$ROOTFS" 2>/dev/null || true
rm -rf "$ROOTFS"
mkdir -p "$ROOTFS"/{bin,sbin,usr/bin,usr/sbin,dev,proc,sys,tmp,run,root,opt,data,system/suvos/bin,system/suvos/apps,system/suvos/config,system/suvos/lib,system/suvos/security,system/suvos/ui}

tar -C "$ROOT_DIR/os/rootfs" -cf - . | tar -C "$ROOTFS" -xf -
rm -rf "$ROOTFS/opt/suvos"
ln -sfn /system/suvos "$ROOTFS/opt/suvos"

cat >"$ROOTFS/system/suvos/config/build.conf" <<EOF
SUVOS_BUILD_PROFILE=$BUILD_PROFILE
SUVOS_WITH_RUNTIMES=$WITH_RUNTIMES
EOF

if [ "$WITH_RUNTIMES" = "1" ]; then
  "$ROOT_DIR/scripts/install-alpine-runtimes.sh" "$ROOTFS"
else
  rm -f "$ROOTFS/system/suvos/apps/manifest.d/py-hello.app" \
    "$ROOTFS/system/suvos/apps/manifest.d/node-hello.app"
  awk '{ gsub(/,app\.py-hello,app\.node-hello/, ""); print }' \
    "$ROOTFS/system/suvos/security/roles.conf" >"$ROOTFS/system/suvos/security/roles.conf.core"
  mv "$ROOTFS/system/suvos/security/roles.conf.core" "$ROOTFS/system/suvos/security/roles.conf"
fi

cp "$ROOT_DIR/build/assets/busybox-x86_64" "$ROOTFS/bin/busybox"
chmod +x "$ROOTFS/bin/busybox"

for applet in sh ash ls mkdir rmdir pwd cat echo printf touch rm cp mv chmod chown \
  grep sed awk head tail find date clear mount umount dmesg sleep uname reboot \
  poweroff halt id whoami env true false test '[' basename dirname ps kill sync \
  mkfifo cut tail sort ifconfig wget; do
  ln -sf busybox "$ROOTFS/bin/$applet"
done

"$ROOT_DIR/scripts/build-cpp-demo.sh"
"$ROOT_DIR/scripts/build-suvosd.sh"
"$ROOT_DIR/scripts/build-suvosctl.sh"
"$ROOT_DIR/scripts/build-suvos-gateway.sh"
"$ROOT_DIR/scripts/build-suvos-splash.sh"
"$ROOT_DIR/scripts/build-ui.sh"
cp "$ROOT_DIR/build/cpp/cpp-hello" "$ROOTFS/system/suvos/bin/cpp-hello"
cp "$ROOT_DIR/build/suvosd/suvosd" "$ROOTFS/system/suvos/bin/suvosd"
cp "$ROOT_DIR/build/suvosctl/suvosctl" "$ROOTFS/system/suvos/bin/suvosctl"
cp "$ROOT_DIR/build/suvos-gateway/suvos-gateway" "$ROOTFS/system/suvos/bin/suvos-gateway"
cp "$ROOT_DIR/build/suvos-splash/suvos-splash" "$ROOTFS/system/suvos/bin/suvos-splash"
cp -R "$ROOT_DIR/build/ui/." "$ROOTFS/system/suvos/ui/"
chmod +x "$ROOTFS/system/suvos/bin/cpp-hello" \
  "$ROOTFS/system/suvos/bin/suvosd" \
  "$ROOTFS/system/suvos/bin/suvosctl" \
  "$ROOTFS/system/suvos/bin/suvos-gateway" \
  "$ROOTFS/system/suvos/bin/suvos-splash"
cp "$ROOT_BOOTSTRAP_HASH" "$ROOTFS/system/suvos/security/root-bootstrap.sha256"

chmod +x "$ROOTFS/init"
chmod +x "$ROOTFS/system/suvos/bin/"*
chmod +x "$ROOTFS/system/suvos/apps/hello.sh" \
  "$ROOTFS/system/suvos/apps/py-hello.py" \
  "$ROOTFS/system/suvos/apps/node-hello.js"
chmod 0644 "$ROOTFS/system/suvos/config/build.conf" \
  "$ROOTFS/system/suvos/config/locale.conf" \
  "$ROOTFS/system/suvos/lib/i18n.sh" \
  "$ROOTFS/system/suvos/security/roles.conf" \
  "$ROOTFS/system/suvos/security/root-bootstrap.sha256"
find "$ROOTFS/system/suvos/apps/manifest.d" -type f -exec chmod 0644 {} +
find "$ROOTFS/system/suvos/ui" -type f -exec chmod 0644 {} +
find "$ROOTFS/system/suvos" -type d -exec chmod 0555 {} +
find "$ROOTFS/system/suvos" -type f -exec chmod a-w {} +

python3 "$ROOT_DIR/tools/make_initramfs.py" "$ROOTFS" "$INITRAMFS"
