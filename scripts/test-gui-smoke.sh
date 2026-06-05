#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
LOG="$ROOT_DIR/build/test-gui-smoke.log"
SECONDS_TO_RUN="${SUVOS_GUI_SMOKE_SECONDS:-120}"
GUI_WIDTH="${SUVOS_GUI_WIDTH:-1280}"
GUI_HEIGHT="${SUVOS_GUI_HEIGHT:-800}"
GUI_INPUT_DEVICES="${SUVOS_GUI_INPUT_DEVICES:--device qemu-xhci,id=xhci -device usb-kbd,bus=xhci.0 -device usb-tablet,bus=xhci.0 -device usb-mouse,bus=xhci.0}"
GUI_AUDIO_DEVICES="${SUVOS_GUI_AUDIO_DEVICES:--audiodev coreaudio,id=suvos-audio,out.mixing-engine=on -device virtio-sound-pci,audiodev=suvos-audio,streams=1}"

mkdir -p "$ROOT_DIR/build"
rm -f "$LOG"

SUVOS_MEMORY="${SUVOS_MEMORY:-3072M}" \
SUVOS_CPUS="${SUVOS_CPUS:-4}" \
SUVOS_DISPLAY="${SUVOS_DISPLAY:-cocoa}" \
SUVOS_VIDEO_DEVICE="${SUVOS_VIDEO_DEVICE:-virtio-vga,xres=$GUI_WIDTH,yres=$GUI_HEIGHT,edid=on}" \
SUVOS_EXTRA_QEMU_ARGS="${SUVOS_EXTRA_QEMU_ARGS:-$GUI_INPUT_DEVICES $GUI_AUDIO_DEVICES}" \
SUVOS_APPEND="${SUVOS_APPEND:-console=ttyS0 rdinit=/init quiet loglevel=3 panic=-1 suvos.graphics=1 suvos.gui=1 suvos.render=qemu-tcg}" \
  "$ROOT_DIR/scripts/run-suvos.sh" >"$LOG" 2>&1 &

pid=$!
was_running=0
sleep "$SECONDS_TO_RUN"

if kill -0 "$pid" 2>/dev/null; then
  was_running=1
  kill -TERM "$pid" 2>/dev/null || true
  sleep 3
fi

if kill -0 "$pid" 2>/dev/null; then
  kill -KILL "$pid" 2>/dev/null || true
fi

wait "$pid" 2>/dev/null || true

if ! grep -q 'suvos-gui: starting Cage with Chromium' "$LOG"; then
  echo "gui smoke failed: Cage/Chromium launch marker not found; log: $LOG" >&2
  sed -n '1,220p' "$LOG" >&2
  exit 1
fi

if grep -E 'libinput initialization failed|Unable to start the wlroots backend' "$LOG" >/dev/null; then
  echo "gui smoke failed: known Cage/wlroots startup error found; log: $LOG" >&2
  sed -n '1,260p' "$LOG" >&2
  exit 1
fi

if grep -E '\[init\] SuvOS browser shell exited|Aborting now to avoid profile corruption|No usable sandbox|Failed to move to new namespace' "$LOG" >/dev/null; then
  echo "gui smoke failed: Chromium/Cage exited before smoke timeout; log: $LOG" >&2
  sed -n '1,360p' "$LOG" >&2
  exit 1
fi

if ! grep -q 'suvos-gui: browser user: suvos-browser' "$LOG"; then
  echo "gui smoke failed: browser is not configured for suvos-browser; log: $LOG" >&2
  sed -n '1,260p' "$LOG" >&2
  exit 1
fi

if ! grep -q 'suvos-gui: render profile: qemu-tcg' "$LOG"; then
  echo "gui smoke failed: qemu-tcg render profile not found; log: $LOG" >&2
  sed -n '1,260p' "$LOG" >&2
  exit 1
fi

if grep -q -- '--no-sandbox' "$LOG" || grep -q 'unsupported command-line flag.*no-sandbox' "$LOG"; then
  echo "gui smoke failed: Chromium sandbox disable flag leaked into default GUI boot; log: $LOG" >&2
  sed -n '1,360p' "$LOG" >&2
  exit 1
fi

if ! grep -q 'suvos-gui: udev ready' "$LOG"; then
  echo "gui smoke failed: udev did not start; log: $LOG" >&2
  sed -n '1,260p' "$LOG" >&2
  exit 1
fi

if ! grep -q 'Device:.*QEMU QEMU USB Mouse' "$LOG" && ! grep -q 'Device:.*QEMU QEMU USB Tablet' "$LOG"; then
  echo "gui smoke failed: libinput did not report a QEMU pointer device; log: $LOG" >&2
  sed -n '1,320p' "$LOG" >&2
  exit 1
fi

if ! grep -q "${GUI_WIDTH}x${GUI_HEIGHT}" "$LOG"; then
  echo "gui smoke failed: requested DRM mode ${GUI_WIDTH}x${GUI_HEIGHT} not found; log: $LOG" >&2
  sed -n '1,260p' "$LOG" >&2
  exit 1
fi

if ! grep -q 'VirtIO SoundCard' "$LOG" || grep -q -- '--- no soundcards ---' "$LOG"; then
  echo "gui smoke failed: VirtIO ALSA playback card not found; log: $LOG" >&2
  sed -n '1,340p' "$LOG" >&2
  exit 1
fi

if grep -q "Can not open.*virtio-sound\\.in" "$LOG"; then
  echo "gui smoke failed: virtio-sound tried to open a host input stream; log: $LOG" >&2
  sed -n '1,120p' "$LOG" >&2
  exit 1
fi

if [ "$was_running" -ne 1 ]; then
  echo "gui smoke failed: QEMU exited before timeout; log: $LOG" >&2
  sed -n '1,260p' "$LOG" >&2
  exit 1
fi

echo "gui smoke passed; log: $LOG"
