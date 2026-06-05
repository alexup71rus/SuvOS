#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
QEMU_BIN="${QEMU_BIN:-/opt/homebrew/bin/qemu-system-x86_64}"
KERNEL="${SUVOS_KERNEL:-$ROOT_DIR/build/kernel/vmlinuz-x86_64}"
INITRD="${SUVOS_INITRD:-$ROOT_DIR/build/initramfs/suvos-initramfs.cpio.gz}"
MEMORY="${SUVOS_MEMORY:-1024M}"
CPUS="${SUVOS_CPUS:-1}"
CPU_MODEL="${SUVOS_CPU_MODEL:-max}"
APPEND="${SUVOS_APPEND:-console=ttyS0 rdinit=/init quiet loglevel=3 panic=-1}"

fail() {
  printf 'error: %s\n' "$*" >&2
  exit 1
}

[ -x "$QEMU_BIN" ] || fail "QEMU executable not found: $QEMU_BIN"
[ -f "$KERNEL" ] || fail "missing kernel: $KERNEL. Run: make"
[ -f "$INITRD" ] || fail "missing initramfs: $INITRD. Run: make"

exec "$QEMU_BIN" \
  -machine q35,accel=tcg \
  -cpu "$CPU_MODEL" \
  -m "$MEMORY" \
  -smp "$CPUS" \
  -kernel "$KERNEL" \
  -initrd "$INITRD" \
  -append "$APPEND" \
  -display none \
  -serial mon:stdio \
  -no-reboot
