#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path


ROOT_DIR = Path(__file__).resolve().parent.parent
DEFAULT_LOCKFILE = ROOT_DIR / "third_party" / "vendors.lock.json"


def load_lockfile(path: Path) -> dict:
    try:
        with path.open("r", encoding="utf-8") as handle:
            data = json.load(handle)
    except FileNotFoundError as exc:
        raise SystemExit(f"vendor lockfile not found: {path}") from exc

    vendors = data.get("vendors")
    if not isinstance(vendors, dict):
        raise SystemExit(f"invalid vendor lockfile format: {path}")
    return vendors


def stringify(value: object) -> str:
    if value is None:
        return ""
    if isinstance(value, bool):
        return "true" if value else "false"
    return str(value)


def vendor_record(vendor: dict) -> list[str]:
    return [
        stringify(vendor.get("path")),
        stringify(vendor.get("repo")),
        stringify(vendor.get("ref")),
        stringify(vendor.get("optional", False)),
        stringify(vendor.get("bootstrap", True)),
    ]


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--lockfile", default=str(DEFAULT_LOCKFILE))

    subparsers = parser.add_subparsers(dest="command", required=True)

    get_parser = subparsers.add_parser("get")
    get_parser.add_argument("vendor")
    get_parser.add_argument("field")

    list_parser = subparsers.add_parser("list")
    list_parser.add_argument("--include-optional", action="store_true")
    list_parser.add_argument("vendors", nargs="*")

    args = parser.parse_args()
    lockfile = Path(args.lockfile)
    vendors = load_lockfile(lockfile)

    if args.command == "get":
        vendor = vendors.get(args.vendor)
        if not isinstance(vendor, dict):
            raise SystemExit(f"vendor not found in lockfile: {args.vendor}")
        print(stringify(vendor.get(args.field)))
        return 0

    if args.command == "list":
        requested = args.vendors or list(vendors.keys())
        missing = [name for name in requested if name not in vendors]
        if missing:
            raise SystemExit(f"vendors not found in lockfile: {', '.join(missing)}")

        for name in requested:
            vendor = vendors[name]
            optional = bool(vendor.get("optional", False))
            if optional and not args.include_optional and not args.vendors:
                continue
            print("\t".join([name, *vendor_record(vendor)]))
        return 0

    raise SystemExit("unsupported command")


if __name__ == "__main__":
    sys.exit(main())
