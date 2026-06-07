#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
. "$ROOT_DIR/scripts/suvos-arch.sh"
ARCH="$(suvos_arch)"
DEFAULT_ACCEL="$(suvos_default_accel "$ARCH")"
QEMU_BIN="${QEMU_BIN:-$(suvos_qemu_bin "$ARCH")}"
KERNEL="${SUVOS_KERNEL:-$(suvos_kernel_path "$ROOT_DIR" "$ARCH")}"
INITRD="${SUVOS_INITRD:-$(suvos_initrd_path "$ROOT_DIR" "$ARCH")}"
MEMORY="${SUVOS_MEMORY:-1024M}"
CPUS="${SUVOS_CPUS:-1}"
ACCEL="${SUVOS_ACCEL:-$DEFAULT_ACCEL}"
CPU_MODEL="${SUVOS_CPU_MODEL:-$(suvos_default_cpu_model "$ARCH" "$ACCEL")}"
MACHINE="${SUVOS_MACHINE:-$(suvos_default_machine "$ARCH")}"
DISPLAY_BACKEND="${SUVOS_DISPLAY:-none}"
VGA="${SUVOS_VGA:-}"
VIDEO_DEVICE="${SUVOS_VIDEO_DEVICE:-}"
SERIAL="${SUVOS_SERIAL:-mon:stdio}"
CONSOLE="$(suvos_console "$ARCH")"
APPEND="${SUVOS_APPEND:-console=$CONSOLE rdinit=/init quiet loglevel=3 panic=-1}"
EXTRA_QEMU_ARGS="${SUVOS_EXTRA_QEMU_ARGS:-}"

fail() {
  printf 'error: %s\n' "$*" >&2
  exit 1
}

[ -x "$QEMU_BIN" ] || fail "QEMU executable not found: $QEMU_BIN"
[ -f "$KERNEL" ] || fail "missing kernel: $KERNEL. Run: make"
if [ ! -f "$INITRD" ] && [ "$ARCH" = "x86_64" ] && [ -f "$ROOT_DIR/build/initramfs/suvos-initramfs.cpio.gz" ]; then
  INITRD="$ROOT_DIR/build/initramfs/suvos-initramfs.cpio.gz"
fi
[ -f "$INITRD" ] || fail "missing initramfs: $INITRD. Run: make"

MACHINE_ARG="$MACHINE"
case ",$MACHINE_ARG," in
  *,accel=*) ;;
  *) MACHINE_ARG="$MACHINE_ARG,accel=$ACCEL" ;;
esac

qemu_args=(
  -machine "$MACHINE_ARG"
  -cpu "$CPU_MODEL"
  -m "$MEMORY"
  -smp "$CPUS"
  -kernel "$KERNEL"
  -initrd "$INITRD"
  -append "$APPEND"
  -display "$DISPLAY_BACKEND"
  -serial "$SERIAL"
  -no-reboot
)

if [ -n "$VIDEO_DEVICE" ]; then
  qemu_args+=(-device "$VIDEO_DEVICE")
elif [ -n "$VGA" ]; then
  qemu_args+=(-vga "$VGA")
fi

if [ -n "$EXTRA_QEMU_ARGS" ]; then
  # Intentional shell splitting: this is a developer-only QEMU override.
  qemu_args+=($EXTRA_QEMU_ARGS)
fi

exec "$QEMU_BIN" "${qemu_args[@]}"
