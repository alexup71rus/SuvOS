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
- localhost-only HTTP gateway `suvos-gateway` on `127.0.0.1:8080`;
- structured JSON endpoints for status, roles, and the app registry;
- first web UI page served through `suvos-gateway`;
- app manifests at `/system/suvos/apps/manifest.d/*.app`;
- read-only `/system/suvos` system area at boot;
- writable `/data/suvos` area for future data and extensions;
- shell-level localization for `ru` and `en`;
- statically linked x86_64 C++ demo program;
- Python 3 runtime;
- Node.js runtime.

## Build

```sh
make
```

The build obtains the x86_64 kernel, minimal graphics modules, and static BusyBox from Alpine `v3.22`, installs Python/Node runtime dependencies into the initramfs rootfs, and builds the C++ demo plus `suvosd`, `suvosctl`, `suvos-splash`, and `suvos-gateway` through Docker/OrbStack.

Alpine assets are cached in `build/cache`, `build/kernel`, and `build/assets`. If the outputs already exist, the build does not hit the network. To force-refresh upstream assets:

```sh
SUVOS_REFRESH_ASSETS=1 make assets
```

Alpine runtime/GUI rootfs layers are cached too. The first `make`, `make test-full`, or `make run-gui` creates a tar layer under `build/cache/rootfs-layers` and an APK download cache under `build/cache/apk`; later builds with the same Alpine image and package list just extract the cached layer without the long `Installing ...` package list.

Layer cache controls:

```sh
SUVOS_REFRESH_LAYER_CACHE=1 make run-gui
SUVOS_DISABLE_LAYER_CACHE=1 make run-gui
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

By default this is the fast core test. It builds the initramfs without Python/Node runtime packages, boots SuvOS in QEMU with `suvos.autotest=1`, runs basic commands, checks roles, verifies `/system/suvos` is read-only, runs the shell app and C++ app, and powers off.

Full test:

```sh
make test-full
```

`make test-full` installs Python/Node runtime dependencies into the initramfs and additionally verifies `suvos run py-hello` plus `suvos run node-hello`.

Fast manual run without Python/Node:

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

On macOS, the current Homebrew QEMU opens the window through `-display cocoa`; `make run-graphics` currently uses `std` VGA as the most compatible early mode. In this mode init loads minimal DRM/framebuffer modules, then runs `suvos-splash`, which attempts to fill `/dev/fb0` with a solid color. If the framebuffer is unavailable, boot continues through the serial console and prints diagnostics. The full browser shell UI comes later, after the image gains a Wayland/Chromium stack.

The next GUI stage is documented in [SuvOS_CONCEPT.md](SuvOS_CONCEPT.md): Wayland runtime + Cage + ordinary Chromium. For the MVP, Chromium must not run in `--kiosk` or `--app` mode, because the SuvOS shell must keep tabs, the address bar, and extensions UI. Cage is the minimal compositor for one maximized browser window without GNOME/KDE/window manager.

Experimental Chromium shell run:

```sh
make run-gui
```

`make run-gui` builds a separate GUI profile with `SUVOS_WITH_GUI=1`, installs Alpine packages for `cage`, `chromium`, `xwayland`, `dbus`, `seatd`, `eudev`, fonts, a cursor theme, ALSA diagnostics, and Mesa/Wayland dependencies, then boots QEMU with `suvos.graphics=1 suvos.gui=1`. This is a heavy profile: Chromium is downloaded and embedded into the initramfs, so it is not used by `make test`. After the first successful build, GUI packages are restored from the rootfs layer cache until the package list or Alpine image changes. The QEMU GUI profile uses USB HID keyboard/tablet/mouse through `qemu-xhci` by default, CoreAudio + virtio-sound for sound, and starts Cage with `-d` to remove client-side window controls where possible. `eudev` is not a desktop environment dependency; it is the device discovery layer libinput/wlroots needs. Without it, the kernel can expose `/dev/input/event*` while Cage still receives no mouse devices.

The cursor theme is currently a replaceable GUI dependency, not part of SuvOS core logic. The default is Alpine's `adwaita-icon-theme`; it can be replaced like this:

```sh
SUVOS_CURSOR_THEME_PACKAGE=breeze-icons make run-gui
```

Chromium still runs as a Wayland client through `--ozone-platform=wayland`. `xwayland` is present only because the current Alpine Cage 0.2.0 package tries to start an XWayland server during boot and fails without `/usr/bin/Xwayland`. This does not mean SuvOS is switching to X11. In the MVP, Chromium runs as root with `--no-sandbox`; this is an early dev compromise until SuvOS gets a proper user/session layer.

The GUI profile startup resolution can be changed without editing scripts:

```sh
make run-gui SUVOS_GUI_WIDTH=1440 SUVOS_GUI_HEIGHT=900
make test-gui-smoke SUVOS_GUI_WIDTH=1024 SUVOS_GUI_HEIGHT=768
```

This sets `virtio-vga,xres=...,yres=...,edid=on`. Live QEMU window resize depends on the QEMU Cocoa -> virtio-gpu -> Linux DRM -> wlroots/Cage path and is currently a separate validation target, not a guaranteed feature.

Input/audio devices can also be overridden:

```sh
make run-gui SUVOS_GUI_INPUT_DEVICES="-device virtio-keyboard-pci -device virtio-tablet-pci"
make run-gui SUVOS_GUI_AUDIO_DEVICES="-audiodev coreaudio,id=suvos-audio,out.mixing-engine=on -device virtio-sound-pci,audiodev=suvos-audio,streams=1"
```

GUI smoke test:

```sh
make test-gui-smoke
```

It also builds the GUI profile and opens a QEMU window for a bounded period. The test checks the serial log for Cage/Chromium startup and known Cage/wlroots errors, but it does not visually prove that the browser rendered the page correctly. Manual `make run-gui` is still needed for visual validation.

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

To run with a window instead of the headless serial console:

```sh
make run-graphics
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
suvos run hello
suvos run cpp-hello
suvosctl ping
suvosctl status
suvosctl list
suvosctl run hello
wget -q -O - http://127.0.0.1:8080/api/status
wget -q -O - http://127.0.0.1:8080/api/roles
wget -q -O - http://127.0.0.1:8080/api/apps
wget -q -O - 'http://127.0.0.1:8080/api/run?name=hello'
wget -q -O - http://127.0.0.1:8080/
suvos run py-hello
suvos run node-hello
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
  |     +-- http://127.0.0.1:8080/api/*
  +-- /system/suvos/bin/suvos-splash
  |     +-- optional /dev/fb0 color fill in suvos.graphics=1 mode
  +-- /system/suvos/bin/suvos-start-gui
  |     +-- optional Cage + ordinary Chromium shell in suvos.gui=1 mode
  +-- /system/suvos/bin/suvos-shell
        +-- suvos CLI
              +-- /run/suvosd/request
                    +-- suvosd validates and executes commands
```

`suvosd` keeps the main daemon loop free by forking a worker for each request. Worker fan-out is capped, app execution currently has a 30 second timeout, timed-out apps are killed by process group, and app output is capped.

`suvosctl` is a diagnostic client for the internal Unix socket API. The main interactive CLI remains `suvos`; the future HTTP gateway should call `/run/suvosd/control.sock` the same way `suvosctl` does.

`suvos-gateway` is the first HTTP boundary for the future web UI. It listens only on `127.0.0.1:8080`, serves the built UI dist from `/system/suvos/ui`, returns JSON, and proxies commands into `suvosd` through the Unix socket. `/api/status`, `/api/roles`, and `/api/apps` return structured JSON objects for the UI; `/api/run?name=<app>` remains a command endpoint and returns `exitCode` plus `output`. Current endpoints: `/`, `/ui/app.js`, `/ui/styles.css`, `/health`, `/api/status`, `/api/roles`, `/api/apps`, `/api/run?name=<app>`.

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

Applications are described with manifest files:

```text
/system/suvos/apps/manifest.d/hello.app
```

The format is currently simple `key=value`:

```text
name=hello
version=0.1.0
runtime=shell
path=/system/suvos/apps/hello.sh
capability=app.hello
ui_entry=
description.en=allowlisted shell demo
description.ru=демо shell-приложение из manifest
```

`suvosd` only runs applications from the manifest registry, checks capabilities, and canonicalizes executable paths. TSV registry is no longer the primary format; the C++ daemon only keeps a legacy fallback.

## Roles and Bootstrap Secret

The current boot starts SuvOS in the `setup` runtime role. This role can read status, list app manifests, run demo applications, and attempt `auth root`. The full `root` runtime role is unlocked with:

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

SuvOS currently uses Alpine `v3.22` as an upstream package source for the kernel, BusyBox, Python, Node.js, musl, and runtime libraries. The project-owned layer is `/init`, `/system/suvos`, `/data/suvos`, `suvosd`, app manifests, localization, and the future UI/service model.

This dependency is acceptable for the prototype. Later stages can replace it with Buildroot or another controlled build pipeline.
