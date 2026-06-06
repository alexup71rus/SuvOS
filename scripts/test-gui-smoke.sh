#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
LOG="$ROOT_DIR/build/test-gui-smoke.log"
MONITOR_SOCK="$ROOT_DIR/build/test-gui-smoke-monitor.sock"
SCREENSHOT="$ROOT_DIR/build/test-gui-smoke.ppm"
SECONDS_TO_RUN="${SUVOS_GUI_SMOKE_SECONDS:-120}"
GUI_WIDTH="${SUVOS_GUI_WIDTH:-1280}"
GUI_HEIGHT="${SUVOS_GUI_HEIGHT:-800}"
GUI_CONNECTOR="${SUVOS_GUI_CONNECTOR:-Virtual-1}"
GUI_KERNEL_VIDEO="${SUVOS_GUI_KERNEL_VIDEO:-video=${GUI_CONNECTOR}:${GUI_WIDTH}x${GUI_HEIGHT}-32}"
GUI_INPUT_DEVICES="${SUVOS_GUI_INPUT_DEVICES:--device qemu-xhci,id=xhci -device usb-kbd,bus=xhci.0 -device usb-tablet,bus=xhci.0 -device usb-mouse,bus=xhci.0}"
GUI_AUDIO_DEVICES="${SUVOS_GUI_AUDIO_DEVICES:--audiodev coreaudio,id=suvos-audio,out.mixing-engine=on -device virtio-sound-pci,audiodev=suvos-audio,streams=1}"
GUI_EXTRA_QEMU_ARGS="${SUVOS_EXTRA_QEMU_ARGS:-$GUI_INPUT_DEVICES $GUI_AUDIO_DEVICES}"

mkdir -p "$ROOT_DIR/build"
rm -f "$LOG" "$MONITOR_SOCK" "$SCREENSHOT"

SUVOS_MEMORY="${SUVOS_MEMORY:-3072M}" \
SUVOS_CPUS="${SUVOS_CPUS:-4}" \
SUVOS_DISPLAY="${SUVOS_DISPLAY:-cocoa}" \
SUVOS_VIDEO_DEVICE="${SUVOS_VIDEO_DEVICE:-virtio-vga,xres=$GUI_WIDTH,yres=$GUI_HEIGHT,edid=on}" \
SUVOS_EXTRA_QEMU_ARGS="$GUI_EXTRA_QEMU_ARGS -monitor unix:$MONITOR_SOCK,server,nowait" \
SUVOS_APPEND="${SUVOS_APPEND:-console=ttyS0 rdinit=/init quiet loglevel=3 panic=-1 $GUI_KERNEL_VIDEO suvos.graphics=1 suvos.gui=1 suvos.render=qemu-tcg}" \
  "$ROOT_DIR/scripts/run-suvos.sh" >"$LOG" 2>&1 &

pid=$!
was_running=0
sleep "$SECONDS_TO_RUN"

if [ -S "$MONITOR_SOCK" ]; then
  python3 - "$MONITOR_SOCK" "$SCREENSHOT" <<'PY'
import socket
import sys
import time

sock_path, screenshot_path = sys.argv[1], sys.argv[2]
with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as sock:
    sock.settimeout(3)
    sock.connect(sock_path)
    sock.sendall(f"screendump {screenshot_path}\n".encode("utf-8"))
    time.sleep(1)
PY
fi

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

if grep -E '\[init\] SuvOS browser shell exited|\[init\] SuvOS browser shell restart limit reached|Aborting now to avoid profile corruption|No usable sandbox|Failed to move to new namespace|Initialization of all EGL display types failed|GLDisplayEGL::Initialize failed|Exiting GPU process due to errors during initialization' "$LOG" >/dev/null; then
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

if [ ! -s "$SCREENSHOT" ]; then
  echo "gui smoke failed: QEMU screendump was not created; log: $LOG" >&2
  sed -n '1,260p' "$LOG" >&2
  exit 1
fi

python3 - "$SCREENSHOT" "$GUI_WIDTH" "$GUI_HEIGHT" <<'PY'
from pathlib import Path
import sys

path = Path(sys.argv[1])
expected_width = int(sys.argv[2])
expected_height = int(sys.argv[3])
data = path.read_bytes()

offset = 0

def next_token() -> bytes:
    global offset
    while offset < len(data) and data[offset] in b" \t\r\n":
        offset += 1
    if offset < len(data) and data[offset] == ord("#"):
        while offset < len(data) and data[offset] not in b"\r\n":
            offset += 1
        return next_token()
    start = offset
    while offset < len(data) and data[offset] not in b" \t\r\n":
        offset += 1
    return data[start:offset]

magic = next_token()
if magic != b"P6":
    raise SystemExit(f"gui smoke failed: unsupported screendump format {magic!r}: {path}")

width = int(next_token())
height = int(next_token())
max_value = int(next_token())
if offset < len(data) and data[offset] in b" \t\r\n":
    offset += 1

if width != expected_width or height != expected_height:
    raise SystemExit(
        f"gui smoke failed: screendump size is {width}x{height}, "
        f"expected {expected_width}x{expected_height}: {path}"
    )

if max_value != 255:
    raise SystemExit(f"gui smoke failed: unsupported screendump max value {max_value}: {path}")

pixels = data[offset:]
pixel_count = min(len(pixels) // 3, width * height)
if pixel_count == 0:
    raise SystemExit(f"gui smoke failed: empty screendump: {path}")

crash_green = bytes([0x11, 0x61, 0x49])
loader_background = bytes([0x0b, 0x11, 0x10])
crash_green_count = 0
loader_background_count = 0
sample = set()
step = max(1, pixel_count // 5000)

for index in range(pixel_count):
    pixel = pixels[index * 3:index * 3 + 3]
    if pixel == crash_green:
        crash_green_count += 1
    if pixel == loader_background:
        loader_background_count += 1
    if index % step == 0:
        sample.add(pixel)

crash_green_ratio = crash_green_count / pixel_count
loader_background_ratio = loader_background_count / pixel_count
if crash_green_ratio > 0.85 or loader_background_ratio > 0.60 or len(sample) < 4:
    raise SystemExit(
        f"gui smoke failed: screendump still looks like boot/fallback screen "
        f"({width}x{height}, crash_green_ratio={crash_green_ratio:.4f}, "
        f"loader_background_ratio={loader_background_ratio:.4f}, sample_colors={len(sample)}): {path}"
    )

print(
    f"gui smoke screendump: {width}x{height}, "
    f"crash_green_ratio={crash_green_ratio:.4f}, "
    f"loader_background_ratio={loader_background_ratio:.4f}, sample_colors={len(sample)}"
)
PY

if [ "$was_running" -ne 1 ]; then
  echo "gui smoke failed: QEMU exited before timeout; log: $LOG" >&2
  sed -n '1,260p' "$LOG" >&2
  exit 1
fi

echo "gui smoke passed; log: $LOG"
