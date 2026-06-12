#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
. "$ROOT_DIR/scripts/suvos-arch.sh"

ARCH="$(suvos_arch)"
if [ "$ARCH" != "x86_64" ]; then
  echo "Windows WHPX launcher currently supports x86_64 only" >&2
  exit 2
fi

WINDOWS_PROJECT_DIR="${SUVOS_WINDOWS_PROJECT_DIR:-C:\\Projects\\SuvOS}"
WINDOWS_RUN_DIR="$WINDOWS_PROJECT_DIR\\run"
WINDOWS_BUILD_DIR="$WINDOWS_PROJECT_DIR\\build"
WINDOWS_ARTIFACTS_DIR="$WINDOWS_PROJECT_DIR\\artifacts"
QEMU_EXE="${SUVOS_WINDOWS_QEMU_EXE:-C:\\Program Files\\qemu\\qemu-system-x86_64.exe}"
WINDOWS_QEMU_TASK="${SUVOS_WINDOWS_QEMU_TASK:-SuvOSWindowsQemuRun}"

KERNEL="$(suvos_kernel_path "$ROOT_DIR" "$ARCH")"
INITRD="$(suvos_initrd_path "$ROOT_DIR" "$ARCH")"
[ -f "$KERNEL" ] || { echo "missing kernel: $KERNEL" >&2; exit 1; }
[ -f "$INITRD" ] || { echo "missing initramfs: $INITRD" >&2; exit 1; }

WINDOWS_PROJECT_WSL="$(wslpath -u "$WINDOWS_PROJECT_DIR")"
WINDOWS_RUN_WSL="$(wslpath -u "$WINDOWS_RUN_DIR")"
WINDOWS_ARTIFACTS_WSL="$(wslpath -u "$WINDOWS_ARTIFACTS_DIR")"
mkdir -p "$WINDOWS_RUN_WSL" "$WINDOWS_ARTIFACTS_WSL"
cp "$KERNEL" "$WINDOWS_RUN_WSL/vmlinuz-x86_64"
cp "$INITRD" "$WINDOWS_RUN_WSL/suvos-initramfs-x86_64.cpio.gz"

GUI_WIDTH="${SUVOS_GUI_WIDTH:-1584}"
GUI_HEIGHT="${SUVOS_GUI_HEIGHT:-888}"
MEMORY="${SUVOS_MEMORY:-4096M}"
CPUS="${SUVOS_CPUS:-4}"
CPU_MODEL="${SUVOS_WINDOWS_QEMU_CPU:-qemu64}"
DISPLAY_BACKEND="${SUVOS_WINDOWS_QEMU_DISPLAY:-sdl,gl=on}"
VIDEO_DEVICE="${SUVOS_WINDOWS_QEMU_VIDEO_DEVICE:-virtio-vga-gl,xres=$GUI_WIDTH,yres=$GUI_HEIGHT,edid=on}"
AUDIO_ARGS="${SUVOS_WINDOWS_QEMU_AUDIO_ARGS:--audiodev sdl,id=suvos-audio,out.mixing-engine=on -device ich9-intel-hda -device hda-output,audiodev=suvos-audio}"
INPUT_ARGS="${SUVOS_WINDOWS_QEMU_INPUT_ARGS:--device qemu-xhci,id=xhci -device usb-kbd,bus=xhci.0 -device usb-tablet,bus=xhci.0 -device usb-mouse,bus=xhci.0}"
KERNEL_APPEND="${SUVOS_APPEND:-console=ttyS0 rdinit=/init quiet loglevel=3 panic=-1 video=Virtual-1:${GUI_WIDTH}x${GUI_HEIGHT}-32 suvos.graphics=1 suvos.gui=1 suvos.aec=1 suvos.render=qemu-whpx}"

POWERSHELL_SCRIPT="$WINDOWS_ARTIFACTS_WSL/run-windows-qemu-suvos.ps1"
cat >"$POWERSHELL_SCRIPT" <<EOF_PS1
\$ErrorActionPreference = 'Stop'
\$Qemu = '$QEMU_EXE'
\$ProjectDir = '$WINDOWS_PROJECT_DIR'
\$RunDir = '$WINDOWS_RUN_DIR'
\$BuildDir = '$WINDOWS_BUILD_DIR'
\$Log = Join-Path \$BuildDir 'windows-qemu-serial.log'
\$Err = Join-Path \$BuildDir 'windows-qemu-stderr.log'
New-Item -ItemType Directory -Force -Path \$RunDir, \$BuildDir | Out-Null
Remove-Item \$Log, \$Err -ErrorAction SilentlyContinue

\$Args = @(
  '-machine', 'q35,accel=whpx',
  '-cpu', '$CPU_MODEL',
  '-m', '$MEMORY',
  '-smp', '$CPUS',
  '-kernel', (Join-Path \$RunDir 'vmlinuz-x86_64'),
  '-initrd', (Join-Path \$RunDir 'suvos-initramfs-x86_64.cpio.gz'),
  '-append', '$KERNEL_APPEND',
  '-display', '$DISPLAY_BACKEND',
  '-serial', ('file:' + \$Log),
  '-no-reboot',
  '-device', '$VIDEO_DEVICE'
)

\$Args += '$INPUT_ARGS'.Split(' ', [System.StringSplitOptions]::RemoveEmptyEntries)
\$Args += '$AUDIO_ARGS'.Split(' ', [System.StringSplitOptions]::RemoveEmptyEntries)

function Quote-Arg([string]\$Arg) {
  if (\$Arg -notmatch '[\s"]') {
    return \$Arg
  }
  return '"' + (\$Arg -replace '"', '\"') + '"'
}

\$ArgumentLine = (\$Args | ForEach-Object { Quote-Arg \$_ }) -join ' '
'qemu: ' + \$Qemu | Out-File -Encoding UTF8 \$Err
'args: ' + \$ArgumentLine | Out-File -Encoding UTF8 -Append \$Err
\$Process = Start-Process -FilePath \$Qemu -ArgumentList \$ArgumentLine -RedirectStandardError \$Err -WorkingDirectory \$ProjectDir -PassThru
Start-Sleep -Seconds 3
if (\$Process.HasExited) {
  throw "QEMU exited early with code \$(\$Process.ExitCode). See \$Err"
}
EOF_PS1

run_with_wsl_interop() {
  command -v powershell.exe >/dev/null 2>&1 || return 1
  powershell.exe -NoProfile -Command "exit 0" >/dev/null 2>&1 || return 1
  powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$(wslpath -w "$POWERSHELL_SCRIPT")"
}

windows_ssh_target() {
  if [ -n "${SUVOS_WINDOWS_SSH_TARGET:-}" ]; then
    printf '%s\n' "$SUVOS_WINDOWS_SSH_TARGET"
    return 0
  fi

  gateway="$(
    awk '/^nameserver[[:space:]]+/ { print $2; exit }' /etc/resolv.conf 2>/dev/null || true
  )"
  [ -n "$gateway" ] || return 1
  printf '%s@%s\n' "${SUVOS_WINDOWS_SSH_USER:-$USER}" "$gateway"
}

run_with_windows_ssh() {
  command -v ssh >/dev/null 2>&1 || return 1
  target="$(windows_ssh_target)" || return 1
  powershell_path="$(wslpath -w "$POWERSHELL_SCRIPT")"
  remote_cmd='schtasks /Create /TN "'$WINDOWS_QEMU_TASK'" /TR "powershell.exe -NoProfile -ExecutionPolicy Bypass -File \"'$powershell_path'\"" /SC ONCE /ST 23:59 /F /IT >NUL && schtasks /Run /TN "'$WINDOWS_QEMU_TASK'"'
  ssh \
    -o BatchMode=yes \
    -o ConnectTimeout="${SUVOS_WINDOWS_SSH_CONNECT_TIMEOUT:-5}" \
    -o StrictHostKeyChecking=accept-new \
    "$target" \
    "$remote_cmd"
}

if ! run_with_wsl_interop; then
  echo "powershell.exe interop is unavailable; falling back to Windows SSH scheduled task" >&2
  run_with_windows_ssh || {
    echo "failed to start Windows QEMU" >&2
    echo "PowerShell launcher: $(wslpath -w "$POWERSHELL_SCRIPT")" >&2
    echo "Set SUVOS_WINDOWS_SSH_TARGET=user@host if the default WSL gateway target is not reachable." >&2
    exit 1
  }
fi
echo "windows qemu serial log: $WINDOWS_BUILD_DIR\\windows-qemu-serial.log"
echo "windows qemu stderr log: $WINDOWS_BUILD_DIR\\windows-qemu-stderr.log"
