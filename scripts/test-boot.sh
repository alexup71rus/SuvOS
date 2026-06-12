#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
. "$ROOT_DIR/scripts/suvos-arch.sh"
ARCH="$(suvos_arch)"
DEFAULT_ACCEL="$(suvos_default_accel "$ARCH")"
QEMU_BIN="${QEMU_BIN:-$(suvos_qemu_bin "$ARCH")}"
KERNEL="${SUVOS_KERNEL:-$(suvos_kernel_path "$ROOT_DIR" "$ARCH")}"
INITRD="${SUVOS_INITRD:-$(suvos_initrd_path "$ROOT_DIR" "$ARCH")}"
TEST_PROFILE="${SUVOS_TEST_PROFILE:-boot}"
LOG="${SUVOS_TEST_LOG:-$ROOT_DIR/build/test-boot-$TEST_PROFILE.log}"
TIMEOUT_SECONDS="${SUVOS_TEST_TIMEOUT:-180}"
MEMORY="${SUVOS_TEST_MEMORY:-1024M}"
CPUS="${SUVOS_TEST_CPUS:-1}"
APPEND_EXTRA="${SUVOS_APPEND_EXTRA:-}"
EXTRA_QEMU_ARGS="${SUVOS_TEST_EXTRA_QEMU_ARGS:-}"
ACCEL="${SUVOS_ACCEL:-$DEFAULT_ACCEL}"
CPU_MODEL="${SUVOS_CPU_MODEL:-$(suvos_default_cpu_model "$ARCH" "$ACCEL")}"
MACHINE="${SUVOS_MACHINE:-$(suvos_default_machine "$ARCH")}"
CONSOLE="$(suvos_console "$ARCH")"
ROOT_BOOTSTRAP_SECRET_FILE="${SUVOS_ROOT_BOOTSTRAP_SECRET_FILE:-$ROOT_DIR/build/secrets/root-bootstrap.secret}"
AUTOTEST_ROOT_SECRET=""

if [ -r "$ROOT_BOOTSTRAP_SECRET_FILE" ]; then
  AUTOTEST_ROOT_SECRET="$(tr -d '\r\n' < "$ROOT_BOOTSTRAP_SECRET_FILE")"
fi

if [ ! -f "$INITRD" ] && [ "$ARCH" = "x86_64" ] && [ -f "$ROOT_DIR/build/initramfs/suvos-initramfs.cpio.gz" ]; then
  INITRD="$ROOT_DIR/build/initramfs/suvos-initramfs.cpio.gz"
fi

MACHINE_ARG="$MACHINE"
case ",$MACHINE_ARG," in
  *,accel=*) ;;
  *) MACHINE_ARG="$MACHINE_ARG,accel=$ACCEL" ;;
esac

rm -f "$LOG"

"$QEMU_BIN" \
  -machine "$MACHINE_ARG" \
  -cpu "$CPU_MODEL" \
  -m "$MEMORY" \
  -smp "$CPUS" \
  -kernel "$KERNEL" \
  -initrd "$INITRD" \
  -append "console=$CONSOLE rdinit=/init quiet loglevel=3 panic=-1 suvos.autotest=1 ${AUTOTEST_ROOT_SECRET:+suvos.autotest_root_secret=$AUTOTEST_ROOT_SECRET }$APPEND_EXTRA" \
  -display none \
  -serial "file:$LOG" \
  -monitor none \
  -no-reboot \
  $EXTRA_QEMU_ARGS &

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
