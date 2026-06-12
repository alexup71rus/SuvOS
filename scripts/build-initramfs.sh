#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
. "$ROOT_DIR/scripts/suvos-arch.sh"
ARCH="$(suvos_arch)"
ROOTFS="$ROOT_DIR/build/rootfs"
INITRAMFS="${SUVOS_INITRD:-$(suvos_initrd_path "$ROOT_DIR" "$ARCH")}"
ROOT_BOOTSTRAP_HASH="$("$ROOT_DIR/scripts/generate-root-secret.sh")"
WITH_RUNTIMES="${SUVOS_WITH_RUNTIMES:-1}"
WITH_GUI="${SUVOS_WITH_GUI:-0}"
WITH_AEC="${SUVOS_WITH_AEC:-0}"
WITH_DEVTOOLS="${SUVOS_WITH_DEVTOOLS:-0}"
LOCKFILE="${SUVOS_VENDORS_LOCKFILE:-$ROOT_DIR/third_party/vendors.lock.json}"

lock_value() {
  python3 "$ROOT_DIR/scripts/vendor-lock.py" --lockfile "$LOCKFILE" get aec "$1" 2>/dev/null || true
}

LOCKED_AEC_PATH="$(lock_value path)"
LOCKED_AEC_REPO=""
if [ -n "$LOCKED_AEC_PATH" ]; then
  LOCKED_AEC_REPO="$ROOT_DIR/$LOCKED_AEC_PATH"
fi

resolve_aec_repo() {
  if [ -n "${SUVOS_AEC_REPO:-}" ]; then
    printf '%s\n' "$SUVOS_AEC_REPO"
    return 0
  fi

  if [ -n "$LOCKED_AEC_REPO" ] && [ -d "$LOCKED_AEC_REPO" ]; then
    printf '%s\n' "$LOCKED_AEC_REPO"
    return 0
  fi

  return 1
}

AEC_REPO="$(resolve_aec_repo || true)"
if [ -n "$AEC_REPO" ]; then
  if [ "$ARCH" = "x86_64" ]; then
    AEC_DIST_DEFAULT="$AEC_REPO/dist/aec-rootfs.tar.gz"
  else
    AEC_DIST_DEFAULT="$AEC_REPO/dist/aec-rootfs-$ARCH.tar.gz"
  fi
else
  AEC_DIST_DEFAULT=""
fi
AEC_DIST="${SUVOS_AEC_DIST:-$AEC_DIST_DEFAULT}"

case "$WITH_RUNTIMES" in
  0|1) ;;
  *)
    echo "SUVOS_WITH_RUNTIMES must be 0 or 1" >&2
    exit 2
    ;;
esac

case "$WITH_GUI" in
  0|1) ;;
  *)
    echo "SUVOS_WITH_GUI must be 0 or 1" >&2
    exit 2
    ;;
esac

case "$WITH_AEC" in
  0|1) ;;
  *)
    echo "SUVOS_WITH_AEC must be 0 or 1" >&2
    exit 2
    ;;
esac

case "$WITH_DEVTOOLS" in
  0|1) ;;
  *)
    echo "SUVOS_WITH_DEVTOOLS must be 0 or 1" >&2
    exit 2
    ;;
esac

if [ "$WITH_AEC" = "1" ] && [ "$WITH_GUI" != "1" ]; then
  echo "SUVOS_WITH_AEC requires SUVOS_WITH_GUI=1" >&2
  exit 2
fi

if [ "$WITH_DEVTOOLS" = "1" ]; then
  BUILD_PROFILE="${SUVOS_BUILD_PROFILE:-dev}"
elif [ "$WITH_AEC" = "1" ]; then
  BUILD_PROFILE="${SUVOS_BUILD_PROFILE:-aec}"
elif [ "$WITH_GUI" = "1" ]; then
  BUILD_PROFILE="${SUVOS_BUILD_PROFILE:-gui}"
elif [ "$WITH_RUNTIMES" = "1" ]; then
  BUILD_PROFILE="${SUVOS_BUILD_PROFILE:-full}"
else
  BUILD_PROFILE="${SUVOS_BUILD_PROFILE:-core}"
fi

SUVOS_ARCH="$ARCH" python3 "$ROOT_DIR/tools/fetch_alpine_assets.py"

chmod -R u+w "$ROOTFS" 2>/dev/null || true
rm -rf "$ROOTFS"
mkdir -p "$ROOTFS"/{bin,sbin,usr/bin,usr/sbin,dev,proc,sys,tmp,run,root,opt,data,system/suvos/bin,system/suvos/apps/manifest.d,system/suvos/config,system/suvos/lib,system/suvos/modules,system/suvos/security,system/suvos/ui}

tar -C "$ROOT_DIR/os/rootfs" -cf - . | tar -C "$ROOTFS" -xf -
rm -rf "$ROOTFS/opt/suvos"
ln -sfn /system/suvos "$ROOTFS/opt/suvos"

cat >"$ROOTFS/system/suvos/config/build.conf" <<EOF
SUVOS_BUILD_PROFILE=$BUILD_PROFILE
SUVOS_ARCH=$ARCH
SUVOS_WITH_RUNTIMES=$WITH_RUNTIMES
SUVOS_WITH_GUI=$WITH_GUI
SUVOS_WITH_AEC=$WITH_AEC
SUVOS_WITH_DEVTOOLS=$WITH_DEVTOOLS
EOF

if [ "$WITH_RUNTIMES" = "1" ]; then
  SUVOS_ARCH="$ARCH" "$ROOT_DIR/scripts/install-alpine-runtimes.sh" "$ROOTFS"
fi

if [ "$WITH_DEVTOOLS" = "1" ]; then
  SUVOS_ARCH="$ARCH" "$ROOT_DIR/scripts/install-alpine-devtools.sh" "$ROOTFS"
fi

if [ "$WITH_GUI" = "1" ]; then
  SUVOS_ARCH="$ARCH" "$ROOT_DIR/scripts/install-alpine-gui.sh" "$ROOTFS"
  SUVOS_ARCH="$ARCH" "$ROOT_DIR/scripts/build-chromium.sh"
  SUVOS_ARCH="$ARCH" "$ROOT_DIR/scripts/install-chromium-artifact.sh" "$ROOTFS"
fi

if [ "$WITH_AEC" = "1" ]; then
  if [ -z "$AEC_REPO" ] && [ -z "${SUVOS_AEC_DIST:-}" ]; then
    if [ -n "$LOCKED_AEC_REPO" ]; then
      echo "AEC checkout not found: $LOCKED_AEC_REPO" >&2
      echo "Run: $ROOT_DIR/scripts/bootstrap-vendors.sh aec" >&2
    else
      echo "AEC checkout is not configured in $LOCKFILE" >&2
    fi
    echo "Or set SUVOS_AEC_REPO / SUVOS_AEC_DIST." >&2
    exit 1
  fi

  SUVOS_ARCH="$ARCH" "$ROOT_DIR/scripts/build-aec.sh"
  SUVOS_ARCH="$ARCH" "$ROOT_DIR/scripts/install-debian-glibc.sh" "$ROOTFS"
  SUVOS_ARCH="$ARCH" "$ROOT_DIR/scripts/install-alpine-aec.sh" "$ROOTFS"
fi

cp "$ROOT_DIR/build/assets/busybox-$ARCH" "$ROOTFS/bin/busybox"
chmod +x "$ROOTFS/bin/busybox"

for applet in sh ash ls mkdir rmdir pwd cat echo printf touch rm cp mv chmod chown \
  grep sed awk head tail find date clear mount umount dmesg sleep uname reboot \
  poweroff halt id whoami env true false test '[' basename dirname ps kill sync \
  mkfifo cut tail sort tee ifconfig route udhcpc ip killall pidof wget insmod lsmod \
  rmmod readlink; do
  ln -sf busybox "$ROOTFS/bin/$applet"
done
ln -sf /bin/env "$ROOTFS/usr/bin/env"

SUVOS_ARCH="$ARCH" "$ROOT_DIR/scripts/build-suvosd.sh"
SUVOS_ARCH="$ARCH" "$ROOT_DIR/scripts/build-suvosctl.sh"
SUVOS_ARCH="$ARCH" "$ROOT_DIR/scripts/build-suvos-gateway.sh"
SUVOS_ARCH="$ARCH" "$ROOT_DIR/scripts/build-suvos-splash.sh"
"$ROOT_DIR/scripts/build-ui.sh" --check
if [ "$WITH_AEC" = "1" ]; then
  mkdir -p "$ROOTFS/system/suvos/aec"
  tar -C "$ROOTFS/system/suvos/aec" -xzf "$AEC_DIST"
fi
cp "$ROOT_DIR/build/suvosd/suvosd-$ARCH" "$ROOTFS/system/suvos/bin/suvosd"
cp "$ROOT_DIR/build/suvosctl/suvosctl-$ARCH" "$ROOTFS/system/suvos/bin/suvosctl"
cp "$ROOT_DIR/build/suvos-gateway/suvos-gateway-$ARCH" "$ROOTFS/system/suvos/bin/suvos-gateway"
cp "$ROOT_DIR/build/suvos-splash/suvos-splash-$ARCH" "$ROOTFS/system/suvos/bin/suvos-splash"
cp -R "$ROOT_DIR/build/ui/." "$ROOTFS/system/suvos/ui/"
rm -f "$ROOTFS/system/suvos/ui/.suvos-ui-bundle.sha256"
cp -R "$ROOT_DIR/build/kernel/graphics-modules-$ARCH" "$ROOTFS/system/suvos/modules/graphics"
cp "$ROOT_DIR/build/kernel/graphics-modules-$ARCH.order" "$ROOTFS/system/suvos/modules/graphics.order"
cp -R "$ROOT_DIR/build/kernel/network-modules-$ARCH" "$ROOTFS/system/suvos/modules/network"
cp "$ROOT_DIR/build/kernel/network-modules-$ARCH.order" "$ROOTFS/system/suvos/modules/network.order"
chmod +x "$ROOTFS/system/suvos/bin/suvosd" \
  "$ROOTFS/system/suvos/bin/suvosctl" \
  "$ROOTFS/system/suvos/bin/suvos-gateway" \
  "$ROOTFS/system/suvos/bin/suvos-splash"
cp "$ROOT_BOOTSTRAP_HASH" "$ROOTFS/system/suvos/security/root-bootstrap.sha256"

chmod +x "$ROOTFS/init"
chmod +x "$ROOTFS/system/suvos/bin/"*
find "$ROOTFS/system/suvos/apps" -maxdepth 1 -type f -exec chmod 0755 {} +
chmod 0644 "$ROOTFS/system/suvos/config/build.conf" \
  "$ROOTFS/system/suvos/config/locale.conf" \
  "$ROOTFS/system/suvos/lib/i18n.sh" \
  "$ROOTFS/system/suvos/security/roles.conf" \
  "$ROOTFS/system/suvos/security/root-bootstrap.sha256"
find "$ROOTFS/system/suvos/apps/manifest.d" -type f -exec chmod 0644 {} +
find "$ROOTFS/system/suvos/modules" -type f -exec chmod 0644 {} +
find "$ROOTFS/system/suvos/ui" -type f -exec chmod 0644 {} +
if [ "$WITH_AEC" = "1" ]; then
  find "$ROOTFS/system/suvos/aec" -type f -name '*.sh' -exec chmod 0755 {} +
  find "$ROOTFS/system/suvos/aec/bin" -type f -exec chmod 0755 {} + 2>/dev/null || true
fi
find "$ROOTFS/system/suvos" -type d -exec chmod 0555 {} +
find "$ROOTFS/system/suvos" -type f -exec chmod a-w {} +

python3 "$ROOT_DIR/tools/make_initramfs.py" "$ROOTFS" "$INITRAMFS"
if [ "$ARCH" = "x86_64" ] && [ -z "${SUVOS_INITRD:-}" ]; then
  cp "$INITRAMFS" "$ROOT_DIR/build/initramfs/suvos-initramfs.cpio.gz"
fi
