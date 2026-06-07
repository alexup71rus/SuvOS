# SuvOS

SuvOS is currently a minimal x86_64 Linux-based OS prototype:

- prebuilt Alpine `v3.22` `linux-virt` kernel;
- custom initramfs;
- SuvOS `/init`;
- SuvOS console shell;
- BusyBox for basic Unix-compatible commands;
- basic `setup`/`root` role system;
- root bootstrap secret generated outside the image, with only its hash embedded;
- statically linked C++ `suvosd` daemon for privileged commands;
- `suvos` CLI client over FIFO IPC;
- internal Unix socket API at `/run/suvosd/control.sock` for the future HTTP gateway;
- diagnostic socket client `suvosctl`;
- framebuffer splash utility `suvos-splash` for the first graphics smoke layer;
- optional GUI profile with a Wayland/Cage/Chromium browser shell;
- localhost-only HTTP gateway `suvos-gateway` on `http://suv.os/`;
- structured JSON endpoints for status, roles, and the app registry;
- first web UI page served through `suvos-gateway`;
- empty app registry at `/system/suvos/apps/manifest.d` for future system apps;
- read-only `/system/suvos` system area at boot;
- writable `/data/suvos` area for future data and extensions;
- shell-level localization for `ru` and `en`.

## Build

```sh
make
```

The build obtains the kernel, minimal graphics modules, and static BusyBox from Alpine `v3.22`, and builds `suvosd`, `suvosctl`, `suvos-splash`, and `suvos-gateway` through Docker/OrbStack. Python/Node runtime packages are installed only in the explicit full profile.

Alpine assets are cached in `build/cache`, `build/kernel`, and `build/assets`. If the outputs already exist, the build does not hit the network. To force-refresh upstream assets:

```sh
SUVOS_REFRESH_ASSETS=1 make assets
```

Alpine runtime/GUI/AEC rootfs layers are cached too. The first `make test-full` or GUI/AEC run creates a tar layer under `build/cache/rootfs-layers` and an APK download cache under `build/cache/apk`; later builds with the same Alpine image and package list just extract the cached layer without the long `Installing ...` package list.

Layer cache controls:

```sh
SUVOS_REFRESH_LAYER_CACHE=1 make run
SUVOS_DISABLE_LAYER_CACHE=1 make run
make clean-layer-cache
```

`make clean` keeps the layer cache. `make clean-layer-cache` removes only the Alpine package/rootfs layer cache. `make distclean` removes the whole `build/` directory.

The UI bundle is cached in `build/ui` by hashing `src/ui/system-settings`, `tsconfig.ui.json`, `package.json`, `package-lock.json`, and `tools/build-ui.mjs`. The initramfs build only checks the ready bundle and copies it into the image; the bundle itself is created separately through `make ui`.

```sh
make ui
SUVOS_REFRESH_UI_BUNDLE=1 make ui
```

Outputs:

```text
build/kernel/vmlinuz-x86_64
build/initramfs/suvos-initramfs.cpio.gz
```

## Test

```sh
make test
```

By default this is the fast core test. It builds the initramfs without Python/Node runtime packages, boots SuvOS in QEMU with `suvos.autotest=1`, runs basic commands, checks roles, verifies the empty app registry, verifies `/system/suvos` is read-only, and powers off.

Full test:

```sh
make test-full
```

`make test-full` installs Python/Node runtime dependencies into the initramfs and verifies those runtime packages without temporary demo apps.

Fast manual serial run without Python/Node:

```sh
make run-core
```

UI layer checks and build:

```sh
npm run ui:check
npm run ui:fix
make ui
```

UI sources live in `src/ui/system-settings`. The initramfs only receives the built dist from `build/ui`: `index.html`, `styles.css`, `app.js`.
If UI sources did not change, `make ui` prints `ui bundle cache hit: build/ui` and skips the TypeScript build.

Manual run in a QEMU window:

```sh
make run-graphics
make run-core-graphics
```

On macOS, the current Homebrew QEMU opens the window through `-display cocoa`; `make run-graphics` currently uses `std` VGA as the most compatible early mode. In this mode init loads minimal DRM/framebuffer modules, then runs `suvos-splash`, which draws a framebuffer boot loader with a `SuvOS` wordmark and current status. If the framebuffer is unavailable, boot continues through the serial console and prints diagnostics. The green framebuffer screen is now reserved for crash/fallback state, for example when the GUI shell exits and the system falls back to the serial console.

The next GUI stage is documented in [SuvOS_CONCEPT.md](SuvOS_CONCEPT.md): Wayland runtime + Cage + ordinary Chromium. For the MVP, Chromium must not run in `--kiosk` or `--app` mode, because the SuvOS shell must keep tabs, the address bar, and extensions UI. Cage is the minimal compositor for one maximized browser window without GNOME/KDE/window manager.

Normal manual SuvOS run:

```sh
make run
# or explicitly:
make runos
# current backend:
make run-qemu
```

`make run` and `make runos` build the GUI profile with AEC, install Alpine packages for `cage`, `chromium`, `xwayland`, `dbus`, `seatd`, `eudev`, `su-exec`, fonts, a cursor theme, ALSA diagnostics, and Mesa/Wayland dependencies. On Apple Silicon, `make run` now defaults to the fast `aarch64` QEMU-HVF profile (`suvos.render=qemu-hvf`) with an arm64 AEC artifact. The old x86_64 QEMU/TCG runner remains available explicitly as `make run-qemu-x86`. This is a heavy profile: Chromium and AEC are embedded into the initramfs, so it is not used by `make test`. After the first successful build, GUI packages are restored from the rootfs layer cache until the package list or Alpine image changes. The QEMU GUI profile uses USB HID keyboard/tablet/mouse through `qemu-xhci` by default, CoreAudio + virtio-sound for sound, and starts Cage with `-d` to remove client-side window controls where possible. `eudev` is not a desktop environment dependency; it is the device discovery layer libinput/wlroots needs. Without it, the kernel can expose `/dev/input/event*` while Cage still receives no mouse devices.

Admin Explorer Code is part of the normal manual run:

```sh
make run
```

`make run` starts root-capable Admin Explorer Code inside the guest VM on `127.0.0.1:3030` and opens Chromium with `http://suv.os/` and `http://suv.os/aec/` tabs. The AEC artifact is prepared in the external sibling repo `../admin-explorer-code` as a Code - OSS source fork without marketplace/cloud/telemetry defaults; override it with `SUVOS_AEC_REPO` or `SUVOS_AEC_DIST`. This profile adds a small glibc payload for the AEC server. `make run-gui` and `make run-gui-aec` are kept as compatibility aliases for older manual commands.

An alternative Parallels runner is reserved as a separate command:

```sh
make run-parallels
```

On Apple Silicon this target intentionally stops with diagnostics: the fast `aarch64` kernel/initramfs/AEC artifact now exists, but Parallels CLI does not provide a direct QEMU-style `-kernel/-initrd` boot path for the current initramfs-only prototype. The next Parallels/GPU step is a bootable arm64 disk/ISO image and a separate 3D acceleration/WebGL check. For current manual work, use `make run` through QEMU-HVF.

The cursor theme is currently a replaceable GUI dependency, not part of SuvOS core logic. The default is Alpine's `adwaita-icon-theme`; it can be replaced like this:

```sh
SUVOS_CURSOR_THEME_PACKAGE=breeze-icons make run
```

Chromium still runs as a Wayland client through `--ozone-platform=wayland`. `xwayland` is present only because the current Alpine Cage 0.2.0 package tries to start an XWayland server during boot; this does not mean SuvOS is switching to X11. Cage/Chromium run as the `suvos-browser` system user, not as root, with browser state under `/data/suvos/chromium`. `--no-sandbox` is forbidden in the default GUI boot; it can only be enabled through the explicit dev escape `suvos.allow_no_sandbox=1` or `SUVOS_CHROMIUM_ALLOW_NO_SANDBOX=1`.

The current `http://suv.os/` tab remains a temporary control-plane diagnostics page, not the final settings surface. Closing Chromium through the browser `X` is not treated as a safe shutdown flow yet. The settings UI installs a web-level `beforeunload` warning, but Chromium does not guarantee that this warning is shown when closing browser chrome. The default behavior is now recovery-first: if the browser shell exits or crashes, `/init` restarts Cage/Chromium up to 3 times per 60 seconds. If the limit is exhausted, SuvOS shows the green crash/fallback screen and returns to the serial console. Final OS-facing settings, system chrome, and shutdown/power UX belong in the future Chromium fork or a privileged internal page, not as more logic inside the current TypeScript page.

The render profile is selected through `suvos.render=<profile>`. If the parameter is omitted, SuvOS uses `hardware` so the real OS does not become permanently software-only. The Mac M QEMU Cocoa/TCG dev run uses `suvos.render=qemu-tcg`, while the arm64/HVF run uses `suvos.render=qemu-hvf`: Chromium uses ANGLE `gl-egl` on Mesa llvmpipe in these profiles, while Vulkan/VAAPI are disabled. This is a fast and stable VM dev path, but not real host GPU/WebGL. The current status and the next Parallels/3D acceleration track are documented in `docs/gpu-webgl-roadmap.md`.

On macOS, `make run` automatically selects a startup resolution at roughly 90% of the main display's logical size. On the current MacBook Pro this yields about `1552x1000` instead of the old `1280x800`. The auto-size is capped by `SUVOS_GUI_MAX_WIDTH=1880` and `SUVOS_GUI_MAX_HEIGHT=1120` so an external 4K/5K monitor does not create an excessively heavy QEMU framebuffer.

The GUI profile startup resolution can be changed without editing scripts:

```sh
SUVOS_GUI_SCALE_PERCENT=95 make run
make run SUVOS_GUI_WIDTH=1440 SUVOS_GUI_HEIGHT=900
SUVOS_GUI_MAX_WIDTH=0 SUVOS_GUI_MAX_HEIGHT=0 make run
make test-gui-smoke SUVOS_GUI_WIDTH=1024 SUVOS_GUI_HEIGHT=768
make test-gui-resolutions
```

This sets the QEMU video device `virtio-vga,xres=...,yres=...,edid=on` and the kernel mode-setting parameter `video=Virtual-1:...-32`, so the guest does not merely list the DRM mode but actually starts at that resolution. `make test-gui-resolutions` automatically checks the 1024x768 and 1440x900 startup modes, including the QEMU screendump size. Live QEMU window resize depends on the QEMU Cocoa -> virtio-gpu -> Linux DRM -> wlroots/Cage path and remains a separate manual/hardware validation target, not a guaranteed feature.

Input/audio devices can also be overridden:

```sh
make run SUVOS_GUI_INPUT_DEVICES="-device virtio-keyboard-pci -device virtio-tablet-pci"
make run SUVOS_GUI_AUDIO_DEVICES="-audiodev coreaudio,id=suvos-audio,out.mixing-engine=on -device virtio-sound-pci,audiodev=suvos-audio,streams=1"
```

GUI smoke test:

```sh
make test-gui-smoke
```

It also builds the GUI profile and opens a QEMU window for a bounded period. The test checks the serial log for Cage/Chromium startup, the `suvos-browser` user, render profile, input/audio devices, absence of default `--no-sandbox`, early browser-shell exit, and fatal GL/GPU errors. At the end, it captures a QEMU `screendump`, checks its size against the requested startup resolution, and fails if the framebuffer is still on the boot loader or green crash/fallback screen.

The build creates an external bootstrap secret:

```text
build/secrets/root-bootstrap.secret
```

This file is not included in the initramfs and is ignored by git. The image only receives:

```text
/system/suvos/security/root-bootstrap.sha256
```

`make clean` keeps `build/secrets`, while `make distclean` removes the whole `build/` directory and causes a new bootstrap secret to be generated on the next build.

## Run

```sh
make run
```

This starts the QEMU GUI with Chromium tabs for `http://suv.os/` and `http://suv.os/aec/`.

For the old headless serial console:

```sh
make run-console
```

Inside the console:

```sh
help
ls
suvos status
suvos roles
suvos whoami
suvos auth status
suvos auth root <bootstrap-secret>
suvos list
suvosctl ping
suvosctl status
suvosctl list
wget -q -O - http://suv.os/api/status
wget -q -O - http://suv.os/api/roles
wget -q -O - http://suv.os/api/apps
wget -q -O - http://suv.os/
python3 --version
node --version
poweroff
```

This first filesystem is initramfs-only. Files created outside the read-only system area are temporary and disappear after reboot.

## Runtime Shape

```text
/init
  +-- /system/suvos/bin/suvosd
  |     +-- /run/suvosd/request
  |     +-- /run/suvosd/control.sock
  +-- /system/suvos/bin/suvosctl
  +-- /system/suvos/bin/suvos-gateway
  |     +-- http://suv.os/api/*
  +-- /system/suvos/bin/suvos-splash
  |     +-- optional /dev/fb0 loader/crash screen in suvos.graphics=1 mode
  +-- /system/suvos/bin/suvos-start-gui
  |     +-- optional Cage + ordinary Chromium shell in suvos.gui=1 mode
  +-- /system/suvos/bin/suvos-shell
        +-- suvos CLI
              +-- /run/suvosd/request
                    +-- suvosd validates and executes commands
```

`suvosd` keeps the main daemon loop free by forking a worker for each request. Worker fan-out is capped, app execution currently has a 30 second timeout, timed-out apps are killed by process group, and app output is capped.

`suvosctl` is a diagnostic client for the internal Unix socket API. The main interactive CLI remains `suvos`; the future HTTP gateway should call `/run/suvosd/control.sock` the same way `suvosctl` does.

`suvos-gateway` is the first HTTP boundary for the future web UI. It listens on loopback-only `127.0.0.1:80`, while guest `/etc/hosts` maps `suv.os` to `127.0.0.1`. The gateway serves the built UI dist from `/system/suvos/ui`, returns JSON, and proxies commands into `suvosd` through the Unix socket. `/api/status`, `/api/roles`, and `/api/apps` return structured JSON objects for the UI; `/api/run?name=<app>` remains a command endpoint for future allowlisted manifests. `/health` now represents baseline control-plane readiness: `suvosd` reachability, the loopback API socket, read-only system root, and the presence of the UI bundle, not just the fact that the gateway process started. Current endpoints: `http://suv.os/`, `/ui/app.js`, `/ui/styles.css`, `/health`, `/api/status`, `/api/roles`, `/api/apps`, `/api/run?name=<app>`. The next security step is a browser UI session token before adding state-changing browser actions.

SuvOS-owned files live under:

```text
/system/suvos/
  bin/
  apps/
    manifest.d/
  ui/
  config/
  lib/
  security/

/data/suvos/
  apps/
  extensions/
  logs/
  state/
  tmp/
```

`/opt/suvos` is a compatibility symlink to `/system/suvos`.

## App Manifests

Applications are described with manifest files. The current system image keeps the registry directory empty, without temporary demo apps:

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

`suvosd` only runs applications from the manifest registry, checks capabilities, and canonicalizes executable paths. TSV registry is no longer the primary format; the C++ daemon only keeps a legacy fallback.

## Roles and Bootstrap Secret

The current boot starts SuvOS in the `setup` runtime role. This role can read status, list app manifests, and attempt `auth root`. The full `root` runtime role is unlocked with:

```sh
suvos auth root <bootstrap-secret>
```

The secret exists only outside the image at `build/secrets/root-bootstrap.secret`. The read-only system area stores only its SHA-256 hash, so the secret itself is not exposed by the VM image.

This is a prototype bootstrap model. The future browser UI should require normal user creation on first run, while the bootstrap secret acts as a proof-of-ownership or claim code. For real hardware, the secret should be provisioned per device instead of reusing one shared secret across all images.

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

The current boot image defaults to Russian. Runtime apps and system messages are expected to read `SUVOS_LANG`.

## Alpine Boundary

SuvOS currently uses Alpine `v3.22` as an upstream package source for the kernel, BusyBox, musl, and runtime libraries. Python/Node are pulled only by the explicit full profile. The project-owned layer is `/init`, `/system/suvos`, `/data/suvos`, `suvosd`, app manifests, localization, and the future UI/service model.

This dependency is acceptable for the prototype. Later stages can replace it with Buildroot or another controlled build pipeline.
