#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ROOTFS="$ROOT_DIR/build/rootfs"
INITRAMFS="$ROOT_DIR/build/initramfs/suvos-initramfs.cpio.gz"

python3 "$ROOT_DIR/tools/fetch_alpine_assets.py"

rm -rf "$ROOTFS"
mkdir -p "$ROOTFS"/{bin,sbin,usr/bin,usr/sbin,dev,proc,sys,tmp,run,root,opt,system/suvos/bin,system/suvos/apps,system/suvos/config,system/suvos/lib,system/suvos/security,system/suvos/src/cpp}

tar -C "$ROOT_DIR/os/rootfs" -cf - . | tar -C "$ROOTFS" -xf -
rm -rf "$ROOTFS/opt/suvos"
ln -sfn /system/suvos "$ROOTFS/opt/suvos"

"$ROOT_DIR/scripts/install-alpine-runtimes.sh" "$ROOTFS"

cp "$ROOT_DIR/build/assets/busybox-x86_64" "$ROOTFS/bin/busybox"
chmod +x "$ROOTFS/bin/busybox"

for applet in sh ash ls mkdir rmdir pwd cat echo printf touch rm cp mv chmod chown \
  grep sed awk head tail find date clear mount umount dmesg sleep uname reboot \
  poweroff halt id whoami env true false test '[' basename dirname ps kill sync \
  mkfifo cut tail sort; do
  ln -sf busybox "$ROOTFS/bin/$applet"
done

"$ROOT_DIR/scripts/build-cpp-demo.sh"
"$ROOT_DIR/scripts/build-suvosd.sh"
cp "$ROOT_DIR/build/cpp/cpp-hello" "$ROOTFS/system/suvos/bin/cpp-hello"
cp "$ROOT_DIR/build/suvosd/suvosd" "$ROOTFS/system/suvos/bin/suvosd"
chmod +x "$ROOTFS/system/suvos/bin/cpp-hello" "$ROOTFS/system/suvos/bin/suvosd"
cp "$ROOT_DIR/src/cpp/hello.cpp" "$ROOTFS/system/suvos/src/cpp/hello.cpp"

chmod +x "$ROOTFS/init"
chmod +x "$ROOTFS/system/suvos/bin/"*
chmod +x "$ROOTFS/system/suvos/apps/hello.sh" \
  "$ROOTFS/system/suvos/apps/py-hello.py" \
  "$ROOTFS/system/suvos/apps/node-hello.js"
chmod 0644 "$ROOTFS/system/suvos/apps/registry.tsv" \
  "$ROOTFS/system/suvos/config/locale.conf" \
  "$ROOTFS/system/suvos/lib/i18n.sh" \
  "$ROOTFS/system/suvos/security/roles.conf" \
  "$ROOTFS/system/suvos/src/cpp/hello.cpp"
find "$ROOTFS/system/suvos" -type d -exec chmod 0555 {} +
find "$ROOTFS/system/suvos" -type f -exec chmod a-w {} +

python3 "$ROOT_DIR/tools/make_initramfs.py" "$ROOTFS" "$INITRAMFS"
