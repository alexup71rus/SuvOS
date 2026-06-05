#!/usr/bin/env bash
set -euo pipefail

QEMU_BIN="${QEMU_BIN:-/opt/homebrew/bin/qemu-system-x86_64}"
QEMU_IMG="${QEMU_IMG:-/opt/homebrew/bin/qemu-img}"

fail() {
  printf 'error: %s\n' "$*" >&2
  exit 1
}

[ -x "$QEMU_BIN" ] || fail "QEMU executable not found: $QEMU_BIN"
[ -x "$QEMU_IMG" ] || fail "qemu-img executable not found: $QEMU_IMG"

printf 'QEMU binary: %s\n' "$QEMU_BIN"
"$QEMU_BIN" --version | head -n 1
file "$QEMU_BIN"

if ! file "$QEMU_BIN" | grep -q 'arm64'; then
  printf 'warning: QEMU is not arm64-native. On Apple Silicon, prefer /opt/homebrew/bin/qemu-system-x86_64.\n' >&2
fi

tmpdir="$(mktemp -d "${TMPDIR:-/tmp}/suvos-qemu-smoke.XXXXXX")"
pidfile="$tmpdir/qemu.pid"

cleanup() {
  if [ -f "$pidfile" ]; then
    pid="$(cat "$pidfile" 2>/dev/null || true)"
    if [ -n "${pid:-}" ] && kill -0 "$pid" 2>/dev/null; then
      kill "$pid" 2>/dev/null || true
      for _ in 1 2 3 4 5; do
        kill -0 "$pid" 2>/dev/null || break
        sleep 0.1
      done
      kill -9 "$pid" 2>/dev/null || true
    fi
  fi
  rm -rf "$tmpdir"
}

trap cleanup EXIT

"$QEMU_BIN" \
  -machine q35,accel=tcg \
  -cpu qemu64 \
  -m 128M \
  -display none \
  -serial none \
  -parallel none \
  -monitor none \
  -no-reboot \
  -S \
  -daemonize \
  -pidfile "$pidfile"

sleep 0.5
[ -f "$pidfile" ] || fail "QEMU did not write a pidfile"

pid="$(cat "$pidfile")"
kill -0 "$pid" 2>/dev/null || fail "QEMU process is not running after launch"

printf 'QEMU smoke test passed. x86_64 machine can be created with TCG emulation.\n'
