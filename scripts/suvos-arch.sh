#!/usr/bin/env bash

suvos_arch() {
  local arch="${SUVOS_ARCH:-x86_64}"
  case "$arch" in
    x86_64|amd64|x64) echo "x86_64" ;;
    aarch64|arm64) echo "aarch64" ;;
    *)
      echo "unsupported SUVOS_ARCH: $arch" >&2
      return 2
      ;;
  esac
}

suvos_docker_platform() {
  case "$1" in
    x86_64) echo "linux/amd64" ;;
    aarch64) echo "linux/arm64" ;;
    *)
      echo "unsupported Docker platform arch: $1" >&2
      return 2
      ;;
  esac
}

suvos_elf_arch_pattern() {
  case "$1" in
    x86_64) echo "x86-64" ;;
    aarch64) echo "ARM aarch64|ARM64|aarch64" ;;
    *)
      echo "unsupported ELF arch: $1" >&2
      return 2
      ;;
  esac
}

suvos_aec_target_arch() {
  case "$1" in
    x86_64) echo "x64" ;;
    aarch64) echo "arm64" ;;
    *)
      echo "unsupported AEC arch: $1" >&2
      return 2
      ;;
  esac
}

suvos_qemu_bin() {
  local bin
  case "$1" in
    x86_64) bin="qemu-system-x86_64" ;;
    aarch64) bin="qemu-system-aarch64" ;;
    *)
      echo "unsupported QEMU arch: $1" >&2
      return 2
      ;;
  esac

  if command -v "$bin" >/dev/null 2>&1; then
    command -v "$bin"
    return 0
  fi

  if [ "$(uname -s 2>/dev/null || true)" = "Darwin" ] && [ -x "/opt/homebrew/bin/$bin" ]; then
    echo "/opt/homebrew/bin/$bin"
    return 0
  fi

  echo "$bin"
}

suvos_kernel_path() {
  local root="$1"
  local arch="$2"
  echo "$root/build/kernel/vmlinuz-$arch"
}

suvos_initrd_path() {
  local root="$1"
  local arch="$2"
  echo "$root/build/initramfs/suvos-initramfs-$arch.cpio.gz"
}

suvos_console() {
  case "$1" in
    x86_64) echo "ttyS0" ;;
    aarch64) echo "ttyAMA0" ;;
    *)
      echo "unsupported console arch: $1" >&2
      return 2
      ;;
  esac
}

suvos_default_machine() {
  case "$1" in
    x86_64) echo "q35" ;;
    aarch64) echo "virt" ;;
    *)
      echo "unsupported machine arch: $1" >&2
      return 2
      ;;
  esac
}

suvos_default_accel() {
  local arch="$1"
  local host_arch
  host_arch="$(uname -m)"
  case "$arch:$host_arch" in
    x86_64:x86_64)
      if [ -r /dev/kvm ] && [ -w /dev/kvm ]; then
        echo "kvm"
      else
        echo "tcg"
      fi
      ;;
    aarch64:arm64) echo "hvf" ;;
    *) echo "tcg" ;;
  esac
}

suvos_default_cpu_model() {
  local arch="$1"
  local accel="${2:-}"
  case "$arch:$accel" in
    x86_64:kvm) echo "host" ;;
    aarch64:hvf) echo "host" ;;
    *) echo "max" ;;
  esac
}
