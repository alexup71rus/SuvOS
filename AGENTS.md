# AGENTS.md

## Project

SuvOS is an x86_64 Linux-based OS prototype. The project currently owns the initramfs userspace, `/init`, `/system/suvos`, `/data/suvos`, `suvosd`, app manifests, localization, and QEMU boot/test flow. Alpine `v3.22` is an upstream package source, not the product identity.

## Default Language

Use Russian for user-facing explanations unless the user asks otherwise. Code identifiers, comments, commit messages, and technical docs may stay English when that is clearer.

## Safety

- Do not install SuvOS onto the host machine.
- Test through QEMU only.
- Do not remove generated boot artifacts unless the user asks or they are clearly temporary cache/log/rootfs output.
- Do not commit `build/` artifacts.
- Keep Alpine package caches out of git; `build/cache`, `build/kernel`, and `build/assets` are generated.
- Keep rootfs package layer caches out of git; `build/cache/rootfs-layers` and `build/cache/apk` are generated. Use `SUVOS_REFRESH_LAYER_CACHE=1` to rebuild them and `make clean-layer-cache` to remove them.
- Do not print, paste, commit, or move `build/secrets/root-bootstrap.secret` into the image.
- If root/bootstrap auth is discussed, refer to the secret path instead of revealing its value unless the user explicitly asks to inspect it.
- Do not weaken the `/system/suvos` read-only boot behavior without calling it out explicitly.
- Do not make writable `/data/suvos` extensions trusted automatically; manifest validation and capabilities must remain explicit.

## Architecture Rules

- Keep project-owned system files under `/system/suvos`.
- Keep runtime/user-writable state under `/data/suvos`.
- Keep `/opt/suvos` as a compatibility symlink only.
- `suvosd` is the privileged control plane and should stay compiled C++.
- `suvos` CLI and future UI must talk to `suvosd`; they must not launch arbitrary system paths directly.
- Prefer the Unix socket `/run/suvosd/control.sock` as the internal API boundary. Keep localhost HTTP in the separate `suvos-gateway` process above it.
- Keep `suvosctl` as the diagnostic client for `/run/suvosd/control.sock`; update autotest when the socket protocol changes.
- `suvos-gateway` must bind to `127.0.0.1` only unless the user explicitly approves a different exposure model.
- Keep the first web UI under `/system/suvos/ui` and serve it through `suvos-gateway`; UI code must use the HTTP API instead of bypassing the gateway.
- Keep browser-facing read endpoints structured JSON; avoid parsing localized CLI text in UI code.
- Keep UI source under `src/ui`; copy only built `build/ui` artifacts into the initramfs image.
- Build/check the frontend bundle through `make ui` and `npm run ui:check`; initramfs assembly should only verify and copy `build/ui`, not rebuild UI sources directly.
- Keep `suvos-splash` framebuffer-only as the current graphics smoke baseline.
- When moving to the browser shell stage, follow `SuvOS_CONCEPT.md`: Wayland runtime + Cage + ordinary Chromium, not `--kiosk`/`--app`, unless the user explicitly changes the browser-shell requirement.
- Store the Chromium profile under `/data/suvos/chromium`; do not put browser user state in `/system/suvos`.
- Run Cage/Chromium as `suvos-browser`, not root. Do not add `--no-sandbox` to the default GUI boot; use only the explicit dev escape documented in `SuvOS_CONCEPT.md`.
- Keep QEMU software-rendering flags scoped to `suvos.render=qemu-tcg`; the implicit default render profile is `hardware`. For the current Alpine Chromium package, qemu-tcg uses ANGLE `gl-egl` over Mesa llvmpipe, not `--disable-gpu`.
- Do not add GNOME, KDE, or a full desktop/session manager for the default SuvOS UI path.
- `xwayland` may be kept only as a Cage package/runtime dependency; Chromium should stay on the Wayland/Ozone path for the default GUI.
- If graphics behavior changes, keep the boot resilient when `/dev/fb0` is unavailable and report diagnostics over serial.
- Root/bootstrap auth must keep the secret outside the image; the image may contain only verification material such as a hash.
- Apps must be declared in `/system/suvos/apps/manifest.d/*.app`.
- Internal system actions may use app manifests with `ui_entry=internal`; keep them hidden from the settings app list and give them explicit capabilities.
- Runtime files may be shell/Python/Node during prototyping, but privileged logic belongs in `suvosd` or another compiled system component.
- Keep localization wired through `SUVOS_LANG`; currently supported values are `ru` and `en`.

## Verification

Before handing off most functional changes, run the fast core boot test:

```sh
make test
```

This is equivalent to `make test-core`. It builds without Python/Node runtime packages, boots QEMU, verifies the system area is read-only, runs `suvosd`, checks roles/auth, verifies `suvosctl` socket calls, verifies localhost HTTP gateway/UI calls, and executes shell/C++ demo apps.

Run the full runtime test when touching Python/Node apps, runtime packaging, manifests for runtime apps, or release-like validation:

```sh
make test-full
```

The full test installs Python/Node runtime packages into the initramfs and executes shell/C++/Python/Node demo apps.

When touching UI source or frontend tooling, run:

```sh
npm run ui:check
```

Use `npm run ui:fix` before committing UI formatting/lint changes.

Useful manual run:

```sh
make run
```

Fast manual run without Python/Node runtime packages:

```sh
make run-core
```

Manual graphics-window run:

```sh
make run-graphics
make run-core-graphics
```

Experimental GUI run:

```sh
make run-gui
make test-gui-smoke
make test-gui-resolutions
```

`make run-gui` and `make test-gui-smoke` are intentionally heavier than the normal tests because they embed Chromium into the initramfs. Do not use them as the default verification path unless the change touches the browser shell boot flow. `make test-gui-smoke` opens a QEMU window briefly, validates serial-log startup health, and captures a QEMU screendump to reject the framebuffer loader or green crash/fallback screen as false positives. Manual validation is still needed for interaction quality.

The GUI boot supervisor in `/init` treats browser-shell exit as recoverable: Cage/Chromium are restarted up to 3 times per 60 seconds, then SuvOS shows the green crash/fallback screen and returns to the serial console. A normal `make test-gui-smoke` run should not trigger any restart.

`make run-gui` auto-detects a larger macOS GUI size through `scripts/detect-gui-size.sh`, roughly 90% of the main display with an upper clamp near 2K. GUI resolution can still be overridden with `SUVOS_GUI_WIDTH` and `SUVOS_GUI_HEIGHT`, for example `make run-gui SUVOS_GUI_WIDTH=1440 SUVOS_GUI_HEIGHT=900`. The default GUI path sets both the QEMU `virtio-vga` mode list and the kernel `video=Virtual-1:...-32` mode-setting parameter; otherwise the guest can list the requested mode but still boot the GUI at 720x400.

Use `make test-gui-resolutions` when changing QEMU video setup, render profiles, or startup resolution handling. It validates startup modes through DRM logs and QEMU screendump size; live window resize still needs manual/hardware validation.

Cursor theme, QEMU input devices, udev/libinput discovery, and audio backend are GUI runtime details. Keep them replaceable through build/run variables and do not move them into SuvOS core policy or control-plane logic. If mouse devices exist under `/dev/input` but Cage has no usable mouse, check whether `eudev` started and whether `libinput list-devices` returns devices.

## Git

- Keep commits small enough to review.
- Do not commit generated `build/` files.
- Check `git status --short` before committing.
