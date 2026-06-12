#!/usr/bin/env python3
import argparse
from contextlib import contextmanager
import gzip
import os
import shutil
import stat
import subprocess
import sys
import time
from pathlib import Path
from typing import BinaryIO, Iterator


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
  def __init__(self, output: BinaryIO, *, progress: bool = True) -> None:
    self.output = output
    self.ino = 1
    self.entries = 0
    self.payload_bytes = 0
    self.archive_bytes = 0
    self.progress = progress
    self.next_progress_payload = 128 * 1024 * 1024
    self.last_progress_time = time.monotonic()

  def _write(self, data: bytes) -> None:
    self.output.write(data)
    self.archive_bytes += len(data)

  def _pad(self) -> None:
    padding = (-self.archive_bytes) % 4
    if padding:
      self._write(b"\0" * padding)

  def _copy_payload(self, source: Path) -> None:
    with source.open("rb") as file:
      while True:
        chunk = file.read(1024 * 1024)
        if not chunk:
          break
        self._write(chunk)

  def _maybe_report_progress(self, *, force: bool = False) -> None:
    if not self.progress:
      return

    now = time.monotonic()
    if (
      not force
      and self.payload_bytes < self.next_progress_payload
      and now - self.last_progress_time < 10
    ):
      return

    print(
      "initramfs: progress "
      f"entries={self.entries} "
      f"payload={self.payload_bytes // (1024 * 1024)}MiB "
      f"archive={self.archive_bytes // (1024 * 1024)}MiB",
      file=sys.stderr,
      flush=True,
    )
    while self.payload_bytes >= self.next_progress_payload:
      self.next_progress_payload += 128 * 1024 * 1024
    self.last_progress_time = now

  def add_entry(
    self,
    name: str,
    mode: int,
    payload: bytes = b"",
    source: Path | None = None,
    payload_size: int | None = None,
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
    if payload_size is None:
      payload_size = len(payload)

    fields = [
      "070701",
      f"{self.ino:08x}",
      f"{mode:08x}",
      f"{uid:08x}",
      f"{gid:08x}",
      f"{nlink:08x}",
      f"{mtime:08x}",
      f"{payload_size:08x}",
      f"{0:08x}",
      f"{0:08x}",
      f"{rdevmajor:08x}",
      f"{rdevminor:08x}",
      f"{len(name_bytes):08x}",
      f"{0:08x}",
    ]

    self.ino += 1
    self.entries += 1
    self._write("".join(fields).encode("ascii"))
    self._write(name_bytes)
    self._pad()
    if source is not None:
      self._copy_payload(source)
    elif payload:
      self._write(payload)
    self.payload_bytes += payload_size
    self._pad()
    self._maybe_report_progress()

  def add_trailer(self) -> None:
    self.add_entry(TRAILER, 0)

  def finish(self) -> None:
    self._maybe_report_progress(force=True)


def iter_paths(root: Path) -> Iterator[Path]:
  for current, dirnames, filenames in os.walk(root):
    dirnames.sort()
    filenames.sort()
    current_path = Path(current)
    if current_path != root:
      yield current_path
    for filename in filenames:
      yield current_path / filename


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
      writer.add_entry(
        rel,
        stat.S_IFREG | mode,
        source=path,
        payload_size=st.st_size,
        mtime=mtime,
      )
    else:
      continue

    added.add(rel)

  return added


@contextmanager
def open_output(output: Path, compress_level: int) -> Iterator[tuple[BinaryIO, str]]:
  output.parent.mkdir(parents=True, exist_ok=True)
  tmp_output = output.with_name(f"{output.name}.tmp")
  tmp_output.unlink(missing_ok=True)
  compressor = "none"

  try:
    if output.suffix == ".gz":
      pigz = shutil.which("pigz")
      if pigz:
        compressor = f"pigz -{compress_level}"
        with tmp_output.open("wb") as file:
          proc = subprocess.Popen(
            [pigz, "-n", f"-{compress_level}", "-c"],
            stdin=subprocess.PIPE,
            stdout=file,
          )
          assert proc.stdin is not None
          try:
            yield proc.stdin, compressor
          finally:
            proc.stdin.close()
            rc = proc.wait()
            if rc != 0:
              raise subprocess.CalledProcessError(rc, proc.args)
      else:
        compressor = f"gzip -{compress_level}"
        with tmp_output.open("wb") as file:
          with gzip.GzipFile(
            filename="",
            mode="wb",
            fileobj=file,
            mtime=0,
            compresslevel=compress_level,
          ) as gz:
            yield gz, compressor
    else:
      with tmp_output.open("wb") as file:
        yield file, compressor

    os.replace(tmp_output, output)
  except Exception:
    tmp_output.unlink(missing_ok=True)
    raise


def main() -> None:
  parser = argparse.ArgumentParser()
  parser.add_argument("rootfs", type=Path)
  parser.add_argument("output", type=Path)
  parser.add_argument(
    "--compress-level",
    type=int,
    default=int(os.environ.get("SUVOS_INITRAMFS_COMPRESS_LEVEL", "1")),
  )
  parser.add_argument(
    "--quiet",
    action="store_true",
    help="suppress progress output",
  )
  args = parser.parse_args()

  if args.compress_level < 1 or args.compress_level > 9:
    raise SystemExit("compress level must be between 1 and 9")

  rootfs = args.rootfs.resolve()
  output = args.output.resolve()

  started = time.monotonic()
  with open_output(output, args.compress_level) as (stream, compressor):
    writer = CpioWriter(stream, progress=not args.quiet)

    added = add_tree(writer, rootfs)

    if "dev" not in added:
      writer.add_entry("dev", stat.S_IFDIR | 0o755, nlink=2)

    for name, (mode, major, minor) in SPECIAL_DEVICES.items():
      if name not in added:
        writer.add_entry(name, mode, rdevmajor=major, rdevminor=minor)

    writer.add_trailer()
    writer.finish()

  elapsed = time.monotonic() - started
  size = output.stat().st_size if output.exists() else 0
  print(
    "initramfs: packed "
    f"entries={writer.entries} "
    f"payload={writer.payload_bytes // (1024 * 1024)}MiB "
    f"output={size // (1024 * 1024)}MiB "
    f"compressor={compressor} "
    f"elapsed={elapsed:.1f}s",
    file=sys.stderr,
    flush=True,
  )

  print(f"initramfs: {output}")


if __name__ == "__main__":
  main()
