#!/usr/bin/env bash
set -euo pipefail

SCALE_PERCENT="${SUVOS_GUI_SCALE_PERCENT:-90}"
MAX_WIDTH="${SUVOS_GUI_MAX_WIDTH:-1880}"
MAX_HEIGHT="${SUVOS_GUI_MAX_HEIGHT:-1120}"
MIN_WIDTH="${SUVOS_GUI_MIN_WIDTH:-1024}"
MIN_HEIGHT="${SUVOS_GUI_MIN_HEIGHT:-720}"
FALLBACK_WIDTH="${SUVOS_GUI_FALLBACK_WIDTH:-1760}"
FALLBACK_HEIGHT="${SUVOS_GUI_FALLBACK_HEIGHT:-990}"

detect_macos_size() {
  if command -v system_profiler >/dev/null 2>&1 && command -v python3 >/dev/null 2>&1; then
    display_json="$(system_profiler SPDisplaysDataType -json 2>/dev/null)" || display_json=""
    if [ -n "$display_json" ]; then
      detected_resolution="$(python3 -c '
import json
import re
import sys

try:
    data = json.load(sys.stdin)
except Exception:
    raise SystemExit(1)

displays = []
for gpu in data.get("SPDisplaysDataType", []):
    displays.extend(gpu.get("spdisplays_ndrvs") or [])

def parse_resolution(display):
    for key in ("_spdisplays_resolution", "_spdisplays_pixels"):
        value = str(display.get(key, ""))
        match = re.search(r"(\d+)\s*x\s*(\d+)", value)
        if match:
            return int(match.group(1)), int(match.group(2))
    return None

main_display = None
for display in displays:
    if display.get("spdisplays_main") == "spdisplays_yes":
        main_display = display
        break

for display in [main_display, *displays]:
    if not display:
        continue
    resolution = parse_resolution(display)
    if resolution:
        print(*resolution)
        raise SystemExit(0)

raise SystemExit(1)
' <<<"$display_json" || true)"
      if [ -n "$detected_resolution" ]; then
        printf '%s\n' "$detected_resolution"
        return 0
      fi
    fi
  fi

  if command -v osascript >/dev/null 2>&1; then
    osascript -e 'tell application "Finder" to get bounds of window of desktop' 2>/dev/null |
      awk -F ', *' 'NF >= 4 { print ($3 - $1), ($4 - $2); exit }'
    return 0
  fi

  return 1
}

if [ "$(uname -s 2>/dev/null || true)" = "Darwin" ]; then
  detected="$(detect_macos_size || true)"
else
  detected=""
fi

if [ -n "$detected" ]; then
  set -- $detected
  screen_width="$1"
  screen_height="$2"
else
  screen_width="$FALLBACK_WIDTH"
  screen_height="$FALLBACK_HEIGHT"
fi

python3 - "$screen_width" "$screen_height" "$SCALE_PERCENT" "$MAX_WIDTH" "$MAX_HEIGHT" "$MIN_WIDTH" "$MIN_HEIGHT" "$FALLBACK_WIDTH" "$FALLBACK_HEIGHT" <<'PY'
import sys

screen_width, screen_height, scale, max_width, max_height, min_width, min_height, fallback_width, fallback_height = map(int, sys.argv[1:])

if screen_width <= 0 or screen_height <= 0:
    screen_width, screen_height = fallback_width, fallback_height

width = screen_width * scale // 100
height = screen_height * scale // 100

if max_width > 0:
    width = min(width, max_width)
if max_height > 0:
    height = min(height, max_height)

width = max(min_width, width)
height = max(min_height, height)

def round_down(value: int, step: int = 8) -> int:
    return max(step, value - (value % step))

print(round_down(width), round_down(height))
PY
