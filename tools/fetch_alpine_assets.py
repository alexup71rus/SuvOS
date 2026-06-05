#!/usr/bin/env python3
import os
import gzip
import stat
import subprocess
import tarfile
import shutil
from pathlib import Path


ARCH = "x86_64"
ALPINE_RELEASE = os.environ.get("SUVOS_ALPINE_RELEASE", "v3.22")
REPO = f"https://dl-cdn.alpinelinux.org/alpine/{ALPINE_RELEASE}/main/{ARCH}"
ROOT = Path(__file__).resolve().parents[1]
CACHE = ROOT / "build" / "cache"
ASSETS = ROOT / "build" / "assets"
KERNEL_OUT = ROOT / "build" / "kernel" / "vmlinuz-x86_64"
GRAPHICS_MODULES_OUT = ROOT / "build" / "kernel" / "graphics-modules"
GRAPHICS_MODULES_ORDER_OUT = ROOT / "build" / "kernel" / "graphics-modules.order"
BUSYBOX_OUT = ASSETS / "busybox-x86_64"
MANIFEST_OUT = ASSETS / "alpine-packages.txt"
REFRESH_ASSETS = os.environ.get("SUVOS_REFRESH_ASSETS", "0") == "1"
GRAPHICS_MODULE_TARGETS = [
  "kernel/drivers/gpu/drm/tiny/bochs.ko.gz",
  "kernel/drivers/gpu/drm/tiny/simpledrm.ko.gz",
  "kernel/drivers/gpu/drm/virtio/virtio-gpu.ko.gz",
]


def download(url: str, path: Path, force: bool = False) -> None:
  path.parent.mkdir(parents=True, exist_ok=True)
  if not force and path.exists() and path.stat().st_size > 0:
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


def read_package_member(package_path: Path, member_name: str) -> bytes:
  with tarfile.open(package_path, "r:gz") as archive:
    stream = archive.extractfile(member_name)
    if stream is None:
      raise RuntimeError(f"{member_name} not found in {package_path.name}")
    return stream.read()


def read_modules_dep(package_path: Path) -> tuple[str, dict[str, list[str]]]:
  modules_dep_name = ""
  with tarfile.open(package_path, "r:gz") as archive:
    for member in archive.getnames():
      if member.endswith("/modules.dep"):
        modules_dep_name = member
        break
    if not modules_dep_name:
      raise RuntimeError("modules.dep not found inside linux-virt package")
    stream = archive.extractfile(modules_dep_name)
    if stream is None:
      raise RuntimeError("modules.dep could not be read")
    text = stream.read().decode("utf-8")

  deps: dict[str, list[str]] = {}
  for line in text.splitlines():
    module, _, requirements = line.partition(":")
    if not module:
      continue
    deps[module] = [item for item in requirements.strip().split() if item]
  modules_root = modules_dep_name.removesuffix("modules.dep")
  return modules_root, deps


def collect_module_order(deps: dict[str, list[str]], targets: list[str]) -> list[str]:
  seen: set[str] = set()
  order: list[str] = []

  def visit(module: str) -> None:
    if module in seen:
      return
    seen.add(module)
    if module not in deps:
      raise RuntimeError(f"kernel module dependency not found: {module}")
    for requirement in deps[module]:
      visit(requirement)
    order.append(module)

  for target in targets:
    visit(target)

  return order


def extract_graphics_modules(package_path: Path) -> None:
  modules_root, deps = read_modules_dep(package_path)
  order = collect_module_order(deps, GRAPHICS_MODULE_TARGETS)

  shutil.rmtree(GRAPHICS_MODULES_OUT, ignore_errors=True)
  GRAPHICS_MODULES_OUT.mkdir(parents=True, exist_ok=True)
  for module in order:
    module_bytes = read_package_member(package_path, f"{modules_root}{module}")
    if module.endswith(".gz"):
      module_bytes = gzip.decompress(module_bytes)
      out_rel = module[:-3]
    else:
      out_rel = module

    out_path = GRAPHICS_MODULES_OUT / out_rel
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_bytes(module_bytes)
    out_path.chmod(0o644)

  GRAPHICS_MODULES_ORDER_OUT.parent.mkdir(parents=True, exist_ok=True)
  GRAPHICS_MODULES_ORDER_OUT.write_text(
    "\n".join(module[:-3] if module.endswith(".gz") else module for module in order) + "\n",
    encoding="utf-8",
  )


def file_ready(path: Path) -> bool:
  return path.exists() and path.stat().st_size > 0


def graphics_modules_ready() -> bool:
  if not file_ready(GRAPHICS_MODULES_ORDER_OUT):
    return False
  for line in GRAPHICS_MODULES_ORDER_OUT.read_text(encoding="utf-8").splitlines():
    if line and not file_ready(GRAPHICS_MODULES_OUT / line):
      return False
  return True


def assets_ready() -> bool:
  if REFRESH_ASSETS:
    return False
  if not file_ready(KERNEL_OUT) or not file_ready(BUSYBOX_OUT) or not file_ready(MANIFEST_OUT):
    return False
  fields = dict(
    line.split("=", 1)
    for line in MANIFEST_OUT.read_text(encoding="utf-8").splitlines()
    if "=" in line
  )
  return fields.get("release") == ALPINE_RELEASE and graphics_modules_ready()


def main() -> None:
  CACHE.mkdir(parents=True, exist_ok=True)
  ASSETS.mkdir(parents=True, exist_ok=True)
  KERNEL_OUT.parent.mkdir(parents=True, exist_ok=True)

  if assets_ready():
    print(f"kernel: {KERNEL_OUT}")
    print(f"busybox: {BUSYBOX_OUT}")
    print(f"graphics-modules: {GRAPHICS_MODULES_OUT}")
    print(f"manifest: {MANIFEST_OUT}")
    return

  release_name = ALPINE_RELEASE.replace("/", "_")
  index_path = CACHE / f"APKINDEX.{release_name}.main.x86_64.tar.gz"
  download(f"{REPO}/APKINDEX.tar.gz", index_path, force=REFRESH_ASSETS)
  packages = read_apkindex(index_path)

  linux_virt = fetch_package(packages, "linux-virt")
  busybox_static = fetch_package(packages, "busybox-static")

  extract_member(linux_virt, "boot/vmlinuz-virt", KERNEL_OUT)
  extract_member(busybox_static, "bin/busybox.static", BUSYBOX_OUT)
  extract_graphics_modules(linux_virt)

  lines = [
    f"repo={REPO}",
    f"release={ALPINE_RELEASE}",
    f"linux-virt={packages['linux-virt']['V']}",
    f"busybox-static={packages['busybox-static']['V']}",
    f"graphics-modules={len(GRAPHICS_MODULES_ORDER_OUT.read_text(encoding='utf-8').splitlines())}",
  ]
  MANIFEST_OUT.write_text("\n".join(lines) + "\n", encoding="utf-8")

  print(f"kernel: {KERNEL_OUT}")
  print(f"busybox: {BUSYBOX_OUT}")
  print(f"graphics-modules: {GRAPHICS_MODULES_OUT}")
  print(f"manifest: {MANIFEST_OUT}")


if __name__ == "__main__":
  main()
