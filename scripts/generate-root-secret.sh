#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SECRET_DIR="${SUVOS_SECRET_DIR:-$ROOT_DIR/build/secrets}"
SECRET_FILE="${SUVOS_ROOT_BOOTSTRAP_SECRET_FILE:-$SECRET_DIR/root-bootstrap.secret}"
HASH_FILE="${SUVOS_ROOT_BOOTSTRAP_HASH_FILE:-$SECRET_DIR/root-bootstrap.sha256}"

mkdir -p "$SECRET_DIR"

if [ ! -s "$SECRET_FILE" ]; then
  umask 077
  python3 - "$SECRET_FILE" <<'PY'
import pathlib
import secrets
import sys

path = pathlib.Path(sys.argv[1])
path.write_text(secrets.token_urlsafe(32) + "\n")
PY
fi

chmod 0600 "$SECRET_FILE"

python3 - "$SECRET_FILE" "$HASH_FILE" <<'PY'
import hashlib
import pathlib
import sys

secret = pathlib.Path(sys.argv[1]).read_text().strip()
pathlib.Path(sys.argv[2]).write_text(hashlib.sha256(secret.encode("utf-8")).hexdigest() + "\n")
PY

chmod 0600 "$HASH_FILE"
printf '%s\n' "$HASH_FILE"
