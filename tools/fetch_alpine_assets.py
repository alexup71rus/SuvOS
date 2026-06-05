#!/usr/bin/env python3
import os
import stat
import subprocess
import tarfile
from pathlib import Path


ARCH = "x86_64"
ALPINE_RELEASE = os.environ.get("SUVOS_ALPINE_RELEASE", "v3.22")
REPO = f"https://dl-cdn.alpinelinux.org/alpine/{ALPINE_RELEASE}/main/{ARCH}"
ROOT = Path(__file__).resolve().parents[1]
CACHE = ROOT / "build" / "cache"
ASSETS = ROOT / "build" / "assets"
KERNEL_OUT = ROOT / "build" / "kernel" / "vmlinuz-x86_64"
BUSYBOX_OUT = ASSETS / "busybox-x86_64"
MANIFEST_OUT = ASSETS / "alpine-packages.txt"


def download(url: str, path: Path) -> None:
  path.parent.mkdir(parents=True, exist_ok=True)
  if path.exists() and path.stat().st_size > 0:
    return
  print(f"download {url}")
  subprocess.run(["curl", "-fsSL", url, "-o", str(path)], check=True)


def read_apkindex(index_path: Path) -> dict[str, dict[str, str]]:
  with tarfile.open(index_path, "r:gz") as archive:
    data = archive.extractfile("APKINDEX")
    if data is None:
      raise RuntimeError("APKINDEX not found inside APKINDEX.tar.gz")
    text = data.read().decode("utf-8")

  packages: dict[str, dict[str, str]] = {}
  for paragraph in text.strip().split("\n\n"):
    fields: dict[str, str] = {}
    for line in paragraph.splitlines():
      if ":" not in line:
        continue
      key, value = line.split(":", 1)
      fields[key] = value
    name = fields.get("P")
    if name:
      packages[name] = fields
  return packages


def fetch_package(packages: dict[str, dict[str, str]], name: str) -> Path:
  fields = packages.get(name)
  if not fields:
    raise RuntimeError(f"package not found in Alpine APKINDEX: {name}")

  version = fields["V"]
  package_file = f"{name}-{version}.apk"
  package_path = CACHE / package_file
  download(f"{REPO}/{package_file}", package_path)
  return package_path


def extract_member(package_path: Path, member_name: str, out_path: Path) -> None:
  out_path.parent.mkdir(parents=True, exist_ok=True)
  with tarfile.open(package_path, "r:gz") as archive:
    member = archive.getmember(member_name)
    stream = archive.extractfile(member)
    if stream is None:
      raise RuntimeError(f"{member_name} not found in {package_path.name}")
    out_path.write_bytes(stream.read())
  out_path.chmod(out_path.stat().st_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)


def main() -> None:
  CACHE.mkdir(parents=True, exist_ok=True)
  ASSETS.mkdir(parents=True, exist_ok=True)
  KERNEL_OUT.parent.mkdir(parents=True, exist_ok=True)

  release_name = ALPINE_RELEASE.replace("/", "_")
  index_path = CACHE / f"APKINDEX.{release_name}.main.x86_64.tar.gz"
  download(f"{REPO}/APKINDEX.tar.gz", index_path)
  packages = read_apkindex(index_path)

  linux_virt = fetch_package(packages, "linux-virt")
  busybox_static = fetch_package(packages, "busybox-static")

  extract_member(linux_virt, "boot/vmlinuz-virt", KERNEL_OUT)
  extract_member(busybox_static, "bin/busybox.static", BUSYBOX_OUT)

  lines = [
    f"repo={REPO}",
    f"release={ALPINE_RELEASE}",
    f"linux-virt={packages['linux-virt']['V']}",
    f"busybox-static={packages['busybox-static']['V']}",
  ]
  MANIFEST_OUT.write_text("\n".join(lines) + "\n", encoding="utf-8")

  print(f"kernel: {KERNEL_OUT}")
  print(f"busybox: {BUSYBOX_OUT}")
  print(f"manifest: {MANIFEST_OUT}")


if __name__ == "__main__":
  main()
