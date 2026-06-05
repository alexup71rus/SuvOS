#!/usr/bin/env python3
import argparse
import gzip
import os
import stat
import time
from pathlib import Path


TRAILER = "TRAILER!!!"


SPECIAL_DEVICES = {
  "dev/console": (stat.S_IFCHR | 0o600, 5, 1),
  "dev/tty": (stat.S_IFCHR | 0o666, 5, 0),
  "dev/null": (stat.S_IFCHR | 0o666, 1, 3),
  "dev/zero": (stat.S_IFCHR | 0o666, 1, 5),
  "dev/random": (stat.S_IFCHR | 0o666, 1, 8),
  "dev/urandom": (stat.S_IFCHR | 0o666, 1, 9),
}


class CpioWriter:
  def __init__(self) -> None:
    self.data = bytearray()
    self.ino = 1

  def _pad(self) -> None:
    while len(self.data) % 4:
      self.data.append(0)

  def add_entry(
    self,
    name: str,
    mode: int,
    payload: bytes = b"",
    uid: int = 0,
    gid: int = 0,
    mtime: int | None = None,
    rdevmajor: int = 0,
    rdevminor: int = 0,
    nlink: int = 1,
  ) -> None:
    if name.startswith("/"):
      name = name[1:]
    name_bytes = name.encode("utf-8") + b"\0"
    mtime = int(time.time()) if mtime is None else mtime

    fields = [
      "070701",
      f"{self.ino:08x}",
      f"{mode:08x}",
      f"{uid:08x}",
      f"{gid:08x}",
      f"{nlink:08x}",
      f"{mtime:08x}",
      f"{len(payload):08x}",
      f"{0:08x}",
      f"{0:08x}",
      f"{rdevmajor:08x}",
      f"{rdevminor:08x}",
      f"{len(name_bytes):08x}",
      f"{0:08x}",
    ]

    self.ino += 1
    self.data.extend("".join(fields).encode("ascii"))
    self.data.extend(name_bytes)
    self._pad()
    self.data.extend(payload)
    self._pad()

  def add_trailer(self) -> None:
    self.add_entry(TRAILER, 0)

  def bytes(self) -> bytes:
    return bytes(self.data)


def iter_paths(root: Path) -> list[Path]:
  paths: list[Path] = []
  for current, dirnames, filenames in os.walk(root):
    dirnames.sort()
    filenames.sort()
    current_path = Path(current)
    if current_path != root:
      paths.append(current_path)
    for filename in filenames:
      paths.append(current_path / filename)
  return paths


def add_tree(writer: CpioWriter, root: Path) -> set[str]:
  added: set[str] = set()

  for path in iter_paths(root):
    rel = path.relative_to(root).as_posix()
    st = path.lstat()
    mode = stat.S_IMODE(st.st_mode)
    mtime = int(st.st_mtime)

    if path.is_symlink():
      target = os.readlink(path).encode("utf-8")
      writer.add_entry(rel, stat.S_IFLNK | mode, target, mtime=mtime)
    elif path.is_dir():
      writer.add_entry(rel, stat.S_IFDIR | mode, mtime=mtime, nlink=2)
    elif path.is_file():
      writer.add_entry(rel, stat.S_IFREG | mode, path.read_bytes(), mtime=mtime)
    else:
      continue

    added.add(rel)

  return added


def main() -> None:
  parser = argparse.ArgumentParser()
  parser.add_argument("rootfs", type=Path)
  parser.add_argument("output", type=Path)
  args = parser.parse_args()

  rootfs = args.rootfs.resolve()
  output = args.output.resolve()
  writer = CpioWriter()

  added = add_tree(writer, rootfs)

  if "dev" not in added:
    writer.add_entry("dev", stat.S_IFDIR | 0o755, nlink=2)

  for name, (mode, major, minor) in SPECIAL_DEVICES.items():
    if name not in added:
      writer.add_entry(name, mode, rdevmajor=major, rdevminor=minor)

  writer.add_trailer()

  output.parent.mkdir(parents=True, exist_ok=True)
  if output.suffix == ".gz":
    with gzip.GzipFile(filename="", mode="wb", fileobj=output.open("wb"), mtime=0) as gz:
      gz.write(writer.bytes())
  else:
    output.write_bytes(writer.bytes())

  print(f"initramfs: {output}")


if __name__ == "__main__":
  main()
