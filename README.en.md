# SuvOS

SuvOS is a Linux-based OS prototype with a browser-centered user interface.
The user shell is built around ordinary Chromium, while system actions go
through a small controlled SuvOS layer: `/init`, `/system/suvos`, `/data/suvos`,
`suvosd`, `suvos-gateway`, the web UI, app manifests, localization, and the
QEMU run/test flow.

Alpine `v3.22` is used as the source for the kernel, BusyBox, musl, and system
packages. It is not the product identity of SuvOS.

The normal manual run currently boots the GUI profile: Wayland/Cage, Chromium,
the local gateway at `http://suv.os/`, and Admin Explorer Code at
`http://suv.os/aec/`. On Apple Silicon, `make run` defaults to the fast
`aarch64` QEMU-HVF path; the x86_64 QEMU/TCG path remains available as an
explicit compatibility path.

## Quick Start

```sh
make bootstrap-vendors
make run
```

`make bootstrap-vendors` syncs the pinned vendor checkouts from
`third_party/vendors.lock.json`:

- `SuvOS_AEC` -> `third_party/aec`;
- `SuvOS_Chromium` -> `third_party/chromium`.

`make run` builds the GUI+AEC initramfs and runs SuvOS in QEMU. Chromium opens
two tabs: `http://suv.os/` for the current system web UI and
`http://suv.os/aec/` for AEC.

## Requirements

Build and run expect:

- a Docker-compatible runtime such as Docker Desktop or OrbStack;
- QEMU; on macOS this is usually Homebrew QEMU;
- Node.js/npm for UI checks and UI builds;
- network access for the first Alpine asset and vendor checkout fetch.

Later builds use local caches until asset versions, package lists, or vendor
refs change.

## Current State

- initramfs-only boot with a SuvOS-owned `/init`;
- read-only `/system/suvos` after boot and writable `/data/suvos`;
- `suvosd` as the privileged C++ control plane;
- `suvos` CLI and diagnostic `suvosctl`;
- Unix socket API at `/run/suvosd/control.sock`;
- `suvos-gateway`, listening only on `127.0.0.1`;
- JSON endpoints: `/health`, `/api/status`, `/api/roles`, `/api/apps`,
  `/api/aec/status`;
- TypeScript UI from `src/ui`, built and installed into `/system/suvos/ui`;
- Wayland/Cage/Chromium browser shell without GNOME/KDE/session manager;
- Chromium profile under `/data/suvos/chromium`;
- AEC as a root-capable admin/debug explorer inside the guest VM;
- manifest registry at `/system/suvos/apps/manifest.d/*.app`;
- `setup` and `root` runtime roles;
- bootstrap secret outside the image at `build/secrets/root-bootstrap.secret`;
- `ru` and `en` localization through `SUVOS_LANG`;
- core, full runtime, dev/root, AEC, and GUI smoke tests.

## Build

```sh
make
```

`make` builds the full initramfs profile. The build fetches Alpine `v3.22`
assets, builds `suvosd`, `suvosctl`, `suvos-gateway`, `suvos-splash`, and
copies the ready UI bundle.

Main outputs:

```text
build/kernel/vmlinuz-<arch>
build/initramfs/suvos-initramfs.cpio.gz
build/initramfs/suvos-initramfs-<arch>.cpio.gz
```

Caches and generated files:

```text
build/cache
build/kernel
build/assets
build/cache/rootfs-layers
build/cache/apk
```

Useful commands:

```sh
SUVOS_REFRESH_ASSETS=1 make assets
SUVOS_REFRESH_LAYER_CACHE=1 make run
SUVOS_DISABLE_LAYER_CACHE=1 make run
make clean-layer-cache
make clean
make distclean
```

`make clean` keeps secrets and the package/rootfs layer cache. `make distclean`
removes the whole `build/` directory, including the generated bootstrap secret.

## UI

UI source lives in `src/ui`. The initramfs receives only the built `build/ui`,
which is then installed into `/system/suvos/ui`.

```sh
npm run ui:check
npm run ui:fix
make ui
SUVOS_REFRESH_UI_BUNDLE=1 make ui
```

The initramfs build does not rebuild frontend source directly: it only checks
and copies `build/ui`.

## Vendor Forks

Large upstream projects are not cloned into the main SuvOS repo and their source
trees are not committed here. They live as separate checkouts pinned by SHA in
`third_party/vendors.lock.json` and provide ready artifacts to SuvOS.

`SuvOS_AEC` is the Code - OSS based fork for Admin Explorer Code:

```sh
make aec
SUVOS_AEC_REPO=/path/to/SuvOS_AEC make aec
SUVOS_AEC_DIST=/path/to/aec-rootfs.tar.gz make run
SUVOS_REFRESH_AEC=1 make aec
```

`SuvOS_Chromium` is the vendor repo for the Chromium artifact:

```sh
make chromium
SUVOS_CHROMIUM_REPO=/path/to/SuvOS_Chromium make chromium
SUVOS_CHROMIUM_DIST=/path/to/chromium-rootfs.tar.gz make run
SUVOS_REFRESH_CHROMIUM=1 make chromium
```

The current `SuvOS_Chromium` provides a browser rootfs overlay. Deeper Chromium
patches belong in that vendor repo, not in the main SuvOS repo.

## Tests

```sh
make test
```

The fast core test runs without Python/Node runtime packages. It boots SuvOS in
QEMU and verifies `suvosd`, `suvosctl`, the HTTP gateway/UI, JSON endpoints,
read-only `/system/suvos`, writable `/data/suvos`, the negative auth path, and
the empty app registry.

```sh
make test-full
```

The full runtime test adds Python/Node runtime packages and verifies that they
are available in the guest VM.

```sh
make test-dev
```

The developer/root profile builds the GUI+AEC image with `apk-tools`, checks a
successful `suvos auth root <bootstrap-secret>`, and confirms that Alpine
package-manager files exist in the guest VM.

```sh
make test-aec-smoke
make test-gui-smoke
make test-gui-resolutions
```

AEC smoke verifies the artifact, the gateway route `/aec/`, product metadata
without marketplace/cloud/chat defaults, required syntax/icon extensions, and
`node-pty`. GUI smoke verifies Cage/Chromium startup, the AEC tab, render
profile, the `suvos-browser` user, absence of default `--no-sandbox`, health
checks, and a screendump. The resolution test runs startup sizes 1024x768 and
1440x900.

## Run

```sh
make run
```

The normal manual run builds the GUI+AEC profile and opens Chromium tabs for
`http://suv.os/` and `http://suv.os/aec/`.

On Apple Silicon, `make run` selects `run-arm64`: `aarch64` kernel/assets,
`aarch64` rootfs packages, an arm64 AEC artifact, and QEMU-HVF. On other hosts,
the default backend is x86_64 QEMU/TCG.

Explicit runners:

```sh
make run-arm64
make run-qemu-x86
make run-dev
make run-core
make run-console
make run-graphics
make run-core-graphics
make run-gui
make run-gui-aec
make run-parallels
```

`make run-gui` and `make run-gui-aec` are compatibility aliases for older manual
commands; the main GUI path is now `make run`.

`make run-dev` adds `apk-tools`, `/etc/apk/repositories`, and `/etc/apk/keys`
to the guest. This profile does not install packages by itself; the network is
used only if the user manually runs `apk`.

`make run-parallels` is still a guarded target. The current initramfs-only
prototype does not have a useful direct `-kernel/-initrd` path through the
Parallels CLI; the real Parallels/GPU track needs a bootable arm64 disk/ISO
image.

## GUI Parameters

On macOS, startup resolution is selected automatically at roughly 90% of the
main display, with an upper clamp near 2K. Overrides:

```sh
SUVOS_GUI_SCALE_PERCENT=95 make run
make run SUVOS_GUI_WIDTH=1440 SUVOS_GUI_HEIGHT=900
SUVOS_GUI_MAX_WIDTH=0 SUVOS_GUI_MAX_HEIGHT=0 make run
```

QEMU input/audio devices also remain build/run variables:

```sh
make run SUVOS_GUI_INPUT_DEVICES="-device virtio-keyboard-pci -device virtio-tablet-pci"
make run SUVOS_GUI_AUDIO_DEVICES="-audiodev coreaudio,id=suvos-audio,out.mixing-engine=on -device virtio-sound-pci,audiodev=suvos-audio,streams=1"
```

The render profile is selected with the kernel parameter
`suvos.render=<profile>`. Current dev profiles use `qemu-tcg` or `qemu-hvf`;
the implicit default remains `hardware` so the real device path does not become
locked to software-only rendering.

## Runtime Shape

```text
Linux kernel
  -> /init
      -> mount /system/suvos read-only
      -> prepare /data/suvos
      -> suvosd
          -> FIFO CLI compatibility path
          -> /run/suvosd/control.sock
      -> suvos-gateway
          -> 127.0.0.1:80
          -> http://suv.os/
          -> http://suv.os/api/*
      -> suvos-start-aec      # GUI+AEC profile
          -> 127.0.0.1:3030/aec
      -> suvos-start-gui      # GUI profile
          -> Cage
              -> Chromium
```

`suvosd` remains the privileged control plane. CLI, UI, and gateway code must
not launch arbitrary system paths directly.

`suvos-gateway` listens only on `127.0.0.1`. Guest `/etc/hosts` maps `suv.os`
to loopback. Browser-facing endpoints return structured JSON so the UI does not
parse localized CLI text.

## File Model

```text
/system/suvos/
  bin/
  apps/
    manifest.d/
  ui/
  config/
  lib/
  security/
  aec/              # AEC profile only

/data/suvos/
  aec/
  apps/
  chromium/
  extensions/
  logs/
  state/
  tmp/
```

`/system/suvos` is bind-mounted read-only during boot. Changing this policy
should be an explicit architecture decision, not a side effect of AEC, UI, or
package tooling work.

`/data/suvos` is writable, but files and extensions from this area are not
trusted automatically. Manifest validation, roles, and capabilities remain an
explicit boundary.

`/opt/suvos` remains a compatibility symlink to `/system/suvos`.

## Control Plane and API

Current important endpoints:

```text
http://suv.os/
http://suv.os/health
http://suv.os/api/status
http://suv.os/api/roles
http://suv.os/api/apps
http://suv.os/api/aec/status
http://suv.os/aec/
```

`/health` means baseline control-plane readiness: `suvosd`, the socket API, the
read-only system root, and the UI bundle. It is not just "the gateway process
started".

State-changing browser actions should only be added after a session token /
capability layer exists. Until then, the UI stays a diagnostic dashboard and
must not grow into a root panel.

## App Manifests

System applications are described with files:

```text
/system/suvos/apps/manifest.d/<name>.app
```

The format is currently simple `key=value`:

```text
name=example
version=0.1.0
runtime=shell
path=/system/suvos/apps/example.sh
capability=app.example
ui_entry=
description.en=allowlisted system app
description.ru=разрешенное системное приложение
```

`suvosd` only runs applications from the manifest registry, checks capability,
and canonicalizes the executable path. Names containing `/` or `..` are
blocked. The TSV registry remains only as a legacy fallback.

The current system image keeps the registry empty: temporary demo applications
were removed.

## Roles and Bootstrap Secret

The boot session starts in the `setup` runtime role. This role can read status,
roles, the app list, and attempt `auth root`.

The `root` runtime role is unlocked for the current boot session with:

```sh
suvos auth root <bootstrap-secret>
```

The secret exists only outside the image:

```text
build/secrets/root-bootstrap.secret
```

The image receives only verification material:

```text
/system/suvos/security/root-bootstrap.sha256
```

For a real device this should become a per-device provisioning / claim flow,
not a shared secret for a mass image.

## Admin Explorer Code (AEC)

Admin Explorer Code, or AEC, is a separate admin/debug web environment inside
the guest SuvOS VM. It is based on the Code - OSS fork `SuvOS_AEC`, is supplied
to SuvOS as a ready vendor artifact, and is installed into `/system/suvos/aec`
in the GUI+AEC profile.

AEC provides a root-capable explorer and terminal for the guest filesystem:

- guest VM file tree;
- editor/viewer tabs;
- text, log, and simple media asset viewing;
- integrated terminal through `node-pty`;
- workspace, user data, and extensions under `/data/suvos/aec` and
  `/data/suvos/extensions/aec`.

In the normal `make run` flow, AEC is available in Chromium at
`http://suv.os/aec/`. Inside the guest, the AEC server listens on
`127.0.0.1:3030/aec`, and `suvos-gateway` proxies `/aec/` and serves
`/api/aec/status`.

AEC is a deliberate exception for guest VM development and administration. It
can have root-level access to the guest filesystem, including runtime state
viewing and editing. It is not the user-facing file picker and not the trust
model for future ordinary applications.

Constraints:

- AEC runs inside the guest VM and does not expose the host filesystem;
- the AEC artifact lives in the separate `SuvOS_AEC` repo, not in the main
  SuvOS repo;
- AEC must not weaken the read-only boot policy for `/system/suvos`;
- writing to `/system/suvos` from AEC requires a separate explicit boot policy
  change;
- the policy-aware user file API belongs on the separate
  `suvos-gateway -> suvosd` track, not inside AEC.

By default, the AEC terminal stays in software rendering mode:

```sh
SUVOS_AEC_TERMINAL_GPU=off make run
```

Experiments with the WebGL terminal renderer belong to the separate GPU/WebGL
track:

```sh
SUVOS_AEC_TERMINAL_GPU=auto make run
```

## Browser Shell

The GUI profile starts ordinary Chromium as a Wayland client inside Cage. SuvOS
does not use GNOME, KDE, or a full session manager for the default UI path.

Important rules:

- Chromium runs as `suvos-browser`, not as root;
- browser state is stored under `/data/suvos/chromium`;
- `--no-sandbox` is forbidden in the default GUI boot;
- `--kiosk` and `--app` are not used for the main browser shell;
- `xwayland` may exist only as a Cage package runtime dependency;
- browser-shell exit is treated as a recoverable failure: `/init` restarts
  Cage/Chromium up to the limit, then shows the crash/fallback screen and
  returns to the serial console.

The system panel, power UX, browser-chrome controls, privileged settings, and
deep file picker should be handled through the `SuvOS_Chromium` patchset /
privileged internal pages, not by growing the ordinary `http://suv.os/` web
page.

## Localization

Default language is configured in:

```text
/system/suvos/config/locale.conf
```

Supported values:

```text
SUVOS_LANG=ru
SUVOS_LANG=en
```

The current boot image defaults to Russian. System messages and runtime apps
should read `SUVOS_LANG`.

## GPU/WebGL

The current Apple Silicon fast path uses `aarch64` QEMU-HVF and the `qemu-hvf`
render profile. This is a fast and stable VM dev path, but not true host
GPU/WebGL yet: Chromium uses Mesa software fallback. Details and acceptance
criteria for the accelerated path are tracked in
[docs/gpu-webgl-roadmap.md](docs/gpu-webgl-roadmap.md).

## License

SuvOS is released under the MIT License. See [LICENSE](LICENSE).

## Safety

- Do not install SuvOS onto the host machine.
- Test through QEMU.
- Do not commit `build/` or package/rootfs caches.
- Do not reveal the contents of `build/secrets/root-bootstrap.secret`.
- Do not weaken the read-only boot policy for `/system/suvos` without an
  explicit decision.
