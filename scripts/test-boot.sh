#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
QEMU_BIN="${QEMU_BIN:-/opt/homebrew/bin/qemu-system-x86_64}"
KERNEL="${SUVOS_KERNEL:-$ROOT_DIR/build/kernel/vmlinuz-x86_64}"
INITRD="${SUVOS_INITRD:-$ROOT_DIR/build/initramfs/suvos-initramfs.cpio.gz}"
TEST_PROFILE="${SUVOS_TEST_PROFILE:-boot}"
LOG="${SUVOS_TEST_LOG:-$ROOT_DIR/build/test-boot-$TEST_PROFILE.log}"
TIMEOUT_SECONDS="${SUVOS_TEST_TIMEOUT:-180}"

rm -f "$LOG"

"$QEMU_BIN" \
  -machine q35,accel=tcg \
  -cpu max \
  -m 1024M \
  -smp 1 \
  -kernel "$KERNEL" \
  -initrd "$INITRD" \
  -append "console=ttyS0 rdinit=/init quiet loglevel=3 panic=-1 suvos.autotest=1" \
  -display none \
  -serial "file:$LOG" \
  -monitor none \
  -no-reboot &

pid="$!"
deadline=$((SECONDS + TIMEOUT_SECONDS))

while kill -0 "$pid" 2>/dev/null; do
  if [ "$SECONDS" -ge "$deadline" ]; then
    kill "$pid" 2>/dev/null || true
    sleep 1
    kill -9 "$pid" 2>/dev/null || true
    echo "boot test timed out; log: $LOG" >&2
    tail -80 "$LOG" >&2 || true
    exit 1
  fi
  sleep 1
done

wait "$pid" || true

if ! grep -q "SUVOS_AUTOTEST_PASS" "$LOG"; then
  echo "boot test failed; log: $LOG" >&2
  tail -120 "$LOG" >&2 || true
  exit 1
fi

echo "boot test passed; log: $LOG"
