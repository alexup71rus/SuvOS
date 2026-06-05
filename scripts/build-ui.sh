#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

if [ ! -x "$ROOT_DIR/node_modules/.bin/tsc" ]; then
  npm --prefix "$ROOT_DIR" ci
fi

npm --prefix "$ROOT_DIR" run ui:build
