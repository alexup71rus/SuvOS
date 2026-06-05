#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT_DIR="$ROOT_DIR/build/ui"
STAMP="$OUT_DIR/.suvos-ui-bundle.sha256"
MODE="${1:-build}"
REFRESH_UI_BUNDLE="${SUVOS_REFRESH_UI_BUNDLE:-0}"

case "$MODE" in
  build|--check) ;;
  *)
    echo "usage: $0 [--check]" >&2
    exit 2
    ;;
esac

compute_ui_hash() {
  ROOT_DIR="$ROOT_DIR" python3 - <<'PY'
import hashlib
import os
from pathlib import Path

root = Path(os.environ["ROOT_DIR"])
inputs = [
    root / "package.json",
    root / "package-lock.json",
    root / "tsconfig.ui.json",
    root / "tools/build-ui.mjs",
]
inputs.extend(sorted((root / "src/ui/system-settings").rglob("*")))

digest = hashlib.sha256()
for path in inputs:
    if not path.is_file():
        continue
    rel = path.relative_to(root).as_posix()
    digest.update(rel.encode("utf-8"))
    digest.update(b"\0")
    digest.update(path.read_bytes())
    digest.update(b"\0")

print(digest.hexdigest())
PY
}

expected_hash="$(compute_ui_hash)"

bundle_ready() {
  [ -r "$STAMP" ] || return 1
  [ -r "$OUT_DIR/index.html" ] || return 1
  [ -r "$OUT_DIR/styles.css" ] || return 1
  [ -r "$OUT_DIR/app.js" ] || return 1
  [ "$(cat "$STAMP")" = "$expected_hash" ]
}

if [ "$REFRESH_UI_BUNDLE" != "1" ] && bundle_ready; then
  echo "ui bundle cache hit: build/ui"
  exit 0
fi

if [ "$MODE" = "--check" ]; then
  cat >&2 <<'EOF'
ui bundle is missing or stale.
Run: make ui
To force rebuild: SUVOS_REFRESH_UI_BUNDLE=1 make ui
EOF
  exit 1
fi

if [ ! -x "$ROOT_DIR/node_modules/.bin/tsc" ]; then
  npm --prefix "$ROOT_DIR" ci
fi

npm --prefix "$ROOT_DIR" run ui:build
printf '%s\n' "$expected_hash" >"$STAMP"
echo "ui bundle built: build/ui"
