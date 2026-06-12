#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

command -v wslpath >/dev/null 2>&1 || {
  echo "This helper must be run from WSL." >&2
  exit 2
}
command -v powershell.exe >/dev/null 2>&1 || {
  echo "powershell.exe interop is required to create the Windows scheduled task." >&2
  exit 2
}

WINDOWS_PROJECT_DIR="${SUVOS_WINDOWS_PROJECT_DIR:-C:\\Projects\\SuvOS}"
WINDOWS_PROJECT_WSL="$(wslpath -u "$WINDOWS_PROJECT_DIR")"
WINDOWS_BUILD_WSL="$WINDOWS_PROJECT_WSL/build"
WINDOWS_ARTIFACTS_WSL="$WINDOWS_PROJECT_WSL/artifacts"
WINDOWS_CHROMIUM_TASK="${SUVOS_WINDOWS_CHROMIUM_TASK:-SuvOS Chromium Build}"
WSL_DISTRO="${SUVOS_WINDOWS_WSL_DISTRO:-Ubuntu}"
WSL_USER="${SUVOS_WINDOWS_WSL_USER:-$USER}"

CHROMIUM_SOURCE_DIR="${SUVOS_CHROMIUM_SOURCE_DIR:-$ROOT_DIR/third_party/chromium}"
CHROMIUM_OUT_DIR="${SUVOS_CHROMIUM_OUT_DIR:-out/Linux_x64}"
CHROMIUM_TARGET="${SUVOS_CHROMIUM_TARGET:-chrome}"
CHROMIUM_JOBS="${SUVOS_CHROMIUM_JOBS:-20}"

DRIVER_WSL="$WINDOWS_ARTIFACTS_WSL/run-wsl-chromium-build.sh"
RUNNER_PS1_WSL="$WINDOWS_ARTIFACTS_WSL/run-wsl-chromium-build.ps1"
SCHEDULER_PS1_WSL="$WINDOWS_ARTIFACTS_WSL/schedule-wsl-chromium-build.ps1"
LOG_WSL="$WINDOWS_BUILD_WSL/ninja-chrome-shell-controls.log"

mkdir -p "$WINDOWS_BUILD_WSL" "$WINDOWS_ARTIFACTS_WSL"

cat >"$DRIVER_WSL" <<EOF_DRIVER
#!/usr/bin/env bash
set -euo pipefail

ROOT='$ROOT_DIR'
CHROMIUM_DIR='$CHROMIUM_SOURCE_DIR'
LINUX_LOG="\$ROOT/build/ninja-chrome-shell-controls.log"
WINDOWS_LOG='$LOG_WSL'
PID_FILE="\$ROOT/build/ninja-chrome-shell-controls.pid"
EXIT_FILE="\$ROOT/build/ninja-chrome-shell-controls.exit"
START_FILE="\$ROOT/build/ninja-chrome-shell-controls.start"

mkdir -p "\$ROOT/build" '$WINDOWS_BUILD_WSL'
rm -f "\$EXIT_FILE"
printf '%s\\n' "started \$(date -Is)" > "\$START_FILE"
printf '%s\\n' "\$\$" > "\$PID_FILE"
: > "\$LINUX_LOG"
: > "\$WINDOWS_LOG"

set +e
(
  set -euo pipefail
  echo "build_pid=\$\$"
  echo "start_time=\$(date -Is)"
  echo "root=\$ROOT"
  echo "chromium_dir=\$CHROMIUM_DIR"
  echo "host=\$(uname -m) jobs='$CHROMIUM_JOBS'"
  cd "\$ROOT"
  export SUVOS_ARCH=x86_64
  export SUVOS_CHROMIUM_SOURCE_DIR="\$CHROMIUM_DIR"
  export SUVOS_CHROMIUM_OUT_DIR='$CHROMIUM_OUT_DIR'
  export SUVOS_CHROMIUM_TARGET='$CHROMIUM_TARGET'
  export SUVOS_CHROMIUM_JOBS='$CHROMIUM_JOBS'
  export SUVOS_CHROMIUM_BUILD_LOG=
  scripts/build-chromium-source.sh
  scripts/package-chromium-source-artifact.sh
  ls -lh "\$CHROMIUM_DIR/dist"/chromium-rootfs*.tar.gz
) 2>&1 | tee "\$LINUX_LOG" "\$WINDOWS_LOG"
status=\${PIPESTATUS[0]}
set -e
printf '%s\\n' "\$status" > "\$EXIT_FILE"
printf 'build_exit=%s\\n' "\$status" | tee -a "\$LINUX_LOG" "\$WINDOWS_LOG"
exit "\$status"
EOF_DRIVER
chmod +x "$DRIVER_WSL"

DRIVER_WIN="$(wslpath -w "$DRIVER_WSL")"
RUNNER_PS1_WIN="$(wslpath -w "$RUNNER_PS1_WSL")"
SCHEDULER_PS1_WIN="$(wslpath -w "$SCHEDULER_PS1_WSL")"

cat >"$RUNNER_PS1_WSL" <<EOF_RUNNER
\$ErrorActionPreference = 'Stop'
& wsl.exe -d '$WSL_DISTRO' -u '$WSL_USER' -- bash '$DRIVER_WIN'
exit \$LASTEXITCODE
EOF_RUNNER

cat >"$SCHEDULER_PS1_WSL" <<EOF_SCHEDULER
\$ErrorActionPreference = 'Stop'
\$TaskName = '$WINDOWS_CHROMIUM_TASK'
\$Runner = '$RUNNER_PS1_WIN'
\$Action = 'powershell.exe -NoProfile -ExecutionPolicy Bypass -File "' + \$Runner + '"'
schtasks /Create /TN \$TaskName /TR \$Action /SC ONCE /ST 23:59 /F /IT
schtasks /Run /TN \$TaskName
EOF_SCHEDULER

powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$SCHEDULER_PS1_WIN"
echo "chromium build log: $WINDOWS_PROJECT_DIR\\build\\ninja-chrome-shell-controls.log"
