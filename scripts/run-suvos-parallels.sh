#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
. "$ROOT_DIR/scripts/suvos-arch.sh"
PRLCTL="${PRLCTL:-/Applications/Parallels Desktop.app/Contents/MacOS/prlctl}"
HOST_ARCH="$(uname -m)"
SUVOS_ARCH="$(suvos_arch)"
KERNEL="${SUVOS_KERNEL:-$(suvos_kernel_path "$ROOT_DIR" "$SUVOS_ARCH")}"
INITRD="${SUVOS_INITRD:-$(suvos_initrd_path "$ROOT_DIR" "$SUVOS_ARCH")}"

fail() {
  printf 'error: %s\n' "$*" >&2
  exit 1
}

note() {
  printf '%s\n' "$*" >&2
}

[ -f "$KERNEL" ] || fail "missing kernel: $KERNEL. Run: make initramfs-aec"
[ -f "$INITRD" ] || fail "missing initramfs: $INITRD. Run: make initramfs-aec"

if [ "$HOST_ARCH" = "arm64" ] && [ "$SUVOS_ARCH" = "x86_64" ]; then
  cat >&2 <<'EOF'
error: Parallels is not a good runtime for the current SuvOS build on this Mac.

Current state:
  host:  arm64 Apple Silicon
  SuvOS: x86_64 kernel/initramfs

Parallels can only run this through its x86 emulator on Apple Silicon, not as a
hardware-accelerated VM. That mode is limited and expected to be slow, so it is
not a useful replacement for QEMU TCG for the AEC GUI profile.

The fast path is to add an aarch64 SuvOS build:
  - Alpine aarch64 kernel/assets
  - aarch64 initramfs/rootfs packages
  - aarch64 AEC artifact
  - an arm64 VM runner for Parallels/Virtualization.framework/QEMU-HVF

Until then, keep QEMU for this x86_64 prototype:
  make run-qemu
EOF
  exit 1
fi

if [ ! -x "$PRLCTL" ]; then
  alt_prlctl="$(command -v prlctl || true)"
  if [ -n "$alt_prlctl" ]; then
    PRLCTL="$alt_prlctl"
  fi
fi

if [ ! -x "$PRLCTL" ]; then
  fail "Parallels CLI prlctl was not found. Install Parallels Desktop only after the arm64 runner path is ready."
fi

note "Parallels CLI found: $PRLCTL"
if [ "$SUVOS_ARCH" = "aarch64" ]; then
  cat >&2 <<'EOF'
No Parallels VM is created yet.

The aarch64 kernel/initramfs/AEC artifacts now exist, but Parallels does not
offer the direct QEMU-style -kernel/-initrd boot path this prototype currently
uses. The next Parallels step is a bootable arm64 disk or ISO image with the
SuvOS kernel/initramfs wired through an EFI/Linux bootloader, then enabling and
testing Parallels 3D acceleration/WebGL inside that VM.

Use the working fast runner for now:
  make run

Old x86_64 QEMU/TCG runner remains available as:
  make run-qemu-x86
EOF
else
  note "No Parallels VM is created yet. The next supported step is an aarch64 SuvOS boot artifact."
fi
exit 1
