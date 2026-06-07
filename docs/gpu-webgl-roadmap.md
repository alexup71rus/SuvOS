# GPU and WebGL Runtime Notes

## Status

This roadmap is still active.
A future Chromium fork may absorb SuvOS-specific settings and browser chrome, but it
does not solve guest GPU acceleration or WebGL by itself. Keep this track open
until SuvOS has a VM backend with a real accelerated graphics path.

## Current Fast Path

On Apple Silicon, `make run` now defaults to the `aarch64` QEMU-HVF profile.
This avoids x86_64 TCG CPU emulation and uses:

- Alpine `aarch64` kernel/assets/rootfs packages;
- `aarch64` static SuvOS binaries;
- linux-arm64 Admin Explorer Code artifact;
- QEMU `qemu-system-aarch64` with `accel=hvf`;
- `suvos.render=qemu-hvf`.

The `qemu-hvf` render profile is intentionally conservative for Chromium:
Vulkan and VAAPI are disabled, and Mesa llvmpipe is used through ANGLE `gl-egl`.
This keeps Cage/Chromium stable under QEMU-HVF.

## Verified

- `SUVOS_ARCH=aarch64 SUVOS_WITH_RUNTIMES=0 scripts/build-initramfs.sh`
- `SUVOS_ARCH=aarch64 SUVOS_TEST_PROFILE=core scripts/test-boot.sh`
- `AEC_TARGET_ARCH=arm64 ./scripts/build-aec-artifact.sh` in the sibling
  `admin-explorer-code` checkout.
- `make initramfs-aec-arm64`
- `SUVOS_ARCH=aarch64 SUVOS_TEST_PROFILE=aec ... scripts/test-boot.sh`
- `SUVOS_ARCH=aarch64 ... scripts/test-gui-smoke.sh`

## Remaining Gap

This is not true host GPU acceleration yet. Current Homebrew QEMU on this Mac
does not expose `virtio-gpu-gl-pci`, and the boot log still reports:

- `features: -virgl`;
- EGL fallback to `kms_swrast`;
- occasional Chromium `ContextResult::kTransientFailure`.

Trying `virtio-gpu-pci,blob=on,hostmem=512M` fails because this QEMU build needs
rutabaga or udmabuf support for blob resources.

## Next Track for Real GPU/WebGL

Use the new `aarch64` build artifacts to create a bootable arm64 disk/ISO for
Parallels or another VM backend with 3D acceleration. Parallels CLI does not
provide a clean direct `-kernel/-initrd` boot path like QEMU, so the next step is
an actual bootable image rather than another initramfs-only runner.

The AEC terminal stays software-rendered by default in `make run`
(`terminal.integrated.gpuAcceleration=off`) because the current QEMU GPU path is
not a real accelerated WebGL path. It can be overridden for experiments with
`SUVOS_AEC_TERMINAL_GPU=auto make run`.

Acceptance criteria for the real GPU/WebGL track:

- VM log shows a GPU path with host 3D acceleration, not `-virgl`;
- Chromium starts without EGL/Vulkan/VAAPI GPU-process fatal errors;
- a dedicated in-guest WebGL page reports a real WebGL context;
- AEC terminal can run with `terminal.integrated.gpuAcceleration=auto` or `on`
  and load the xterm WebGL renderer without breaking shell startup.
