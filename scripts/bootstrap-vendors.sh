#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
LOCKFILE="${SUVOS_VENDORS_LOCKFILE:-$ROOT_DIR/third_party/vendors.lock.json}"
INCLUDE_OPTIONAL=0
REQUESTED_VENDORS=()

usage() {
  cat <<'EOF'
usage: scripts/bootstrap-vendors.sh [--include-optional] [vendor...]

Examples:
  scripts/bootstrap-vendors.sh
  scripts/bootstrap-vendors.sh aec
  scripts/bootstrap-vendors.sh --include-optional
EOF
}

while [ "$#" -gt 0 ]; do
  case "$1" in
    --include-optional|--all)
      INCLUDE_OPTIONAL=1
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      REQUESTED_VENDORS+=("$1")
      ;;
  esac
  shift
done

vendor_lines=()
if [ "${#REQUESTED_VENDORS[@]}" -gt 0 ]; then
  while IFS= read -r line; do
    vendor_lines+=("$line")
  done < <(python3 "$ROOT_DIR/scripts/vendor-lock.py" --lockfile "$LOCKFILE" list "${REQUESTED_VENDORS[@]}")
else
  if [ "$INCLUDE_OPTIONAL" = "1" ]; then
    while IFS= read -r line; do
      vendor_lines+=("$line")
    done < <(python3 "$ROOT_DIR/scripts/vendor-lock.py" --lockfile "$LOCKFILE" list --include-optional)
  else
    while IFS= read -r line; do
      vendor_lines+=("$line")
    done < <(python3 "$ROOT_DIR/scripts/vendor-lock.py" --lockfile "$LOCKFILE" list)
  fi
fi

if [ "${#vendor_lines[@]}" -eq 0 ]; then
  echo "no vendors selected from lockfile: $LOCKFILE"
  exit 0
fi

sync_vendor() {
  local name="$1"
  local rel_path="$2"
  local repo="$3"
  local ref="$4"
  local optional="$5"
  local bootstrap="$6"
  local checkout="$ROOT_DIR/$rel_path"

  if [ "$bootstrap" != "true" ]; then
    echo "skip $name: bootstrap is disabled in $LOCKFILE"
    return 0
  fi

  if [ -z "$ref" ]; then
    echo "vendor $name has no pinned ref in $LOCKFILE" >&2
    return 1
  fi

  mkdir -p "$(dirname "$checkout")"

  if [ -d "$checkout/.git" ]; then
    if [ -n "$(git -C "$checkout" status --short)" ]; then
      echo "vendor checkout is dirty, refusing to reset: $checkout" >&2
      return 1
    fi

    current_origin="$(git -C "$checkout" remote get-url origin 2>/dev/null || true)"
    if [ -n "$current_origin" ] && [ "$current_origin" != "$repo" ]; then
      echo "vendor checkout has unexpected origin: $checkout" >&2
      echo "expected: $repo" >&2
      echo "actual:   $current_origin" >&2
      return 1
    fi

    if [ -z "$current_origin" ]; then
      git -C "$checkout" remote add origin "$repo"
    fi

    git -C "$checkout" fetch --tags origin
  else
    if [ -e "$checkout" ]; then
      echo "vendor path exists but is not a git checkout: $checkout" >&2
      return 1
    fi
    git clone "$repo" "$checkout"
    git -C "$checkout" fetch --tags origin
  fi

  git -C "$checkout" checkout --detach "$ref"

  if [ -f "$checkout/.gitmodules" ]; then
    git -C "$checkout" submodule update --init --recursive
  fi

  echo "vendor ready: $name -> $rel_path @ $(git -C "$checkout" rev-parse HEAD)"
}

for line in "${vendor_lines[@]}"; do
  IFS=$'\t' read -r name rel_path repo ref optional bootstrap <<<"$line"
  sync_vendor "$name" "$rel_path" "$repo" "$ref" "$optional" "$bootstrap"
done
