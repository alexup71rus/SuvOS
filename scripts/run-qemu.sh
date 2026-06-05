#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

QEMU_BIN="${QEMU_BIN:-/opt/homebrew/bin/qemu-system-x86_64}"
QEMU_IMG="${QEMU_IMG:-/opt/homebrew/bin/qemu-img}"

IMAGE="${1:-${SUVOS_IMAGE:-$ROOT_DIR/build/suvos-x86_64.qcow2}}"
IMAGE_FORMAT="${SUVOS_IMAGE_FORMAT:-qcow2}"
MEMORY="${SUVOS_MEMORY:-1024M}"
CPUS="${SUVOS_CPUS:-2}"
ACCEL="${SUVOS_ACCEL:-tcg}"
DISPLAY_BACKEND="${SUVOS_DISPLAY:-cocoa}"
VGA="${SUVOS_VGA:-std}"
SSH_HOSTFWD="${SUVOS_SSH_HOSTFWD:-tcp::2222-:22}"

fail() {
  printf 'error: %s\n' "$*" >&2
  exit 1
}

[ -x "$QEMU_BIN" ] || fail "QEMU executable not found: $QEMU_BIN"

if [ ! -f "$IMAGE" ]; then
  cat >&2 <<EOF
Missing image: $IMAGE

To create an empty qcow2 disk for later OS installation/build output:
  mkdir -p "$ROOT_DIR/build"
  "$QEMU_IMG" create -f qcow2 "$ROOT_DIR/build/suvos-x86_64.qcow2" 2G

An empty disk will not boot until we install or build SuvOS into it.
You can also pass an ISO path:
  $0 /path/to/installer.iso
EOF
  exit 1
fi

boot_args=()
case "$IMAGE" in
  *.iso|*.ISO)
    boot_args=(-cdrom "$IMAGE" -boot d)
    ;;
  *)
    boot_args=(-drive "file=$IMAGE,format=$IMAGE_FORMAT,if=virtio" -boot order=c)
    ;;
esac

exec "$QEMU_BIN" \
  -machine "q35,accel=$ACCEL" \
  -cpu qemu64 \
  -m "$MEMORY" \
  -smp "$CPUS" \
  "${boot_args[@]}" \
  -display "$DISPLAY_BACKEND" \
  -vga "$VGA" \
  -netdev "user,id=net0,hostfwd=$SSH_HOSTFWD" \
  -device "e1000,netdev=net0" \
  -serial mon:stdio
