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
- Keep `suvos-splash` framebuffer-only for now; do not add Wayland/Cage/Chromium until explicitly moving to the browser kiosk stage.
- Root/bootstrap auth must keep the secret outside the image; the image may contain only verification material such as a hash.
- Apps must be declared in `/system/suvos/apps/manifest.d/*.app`.
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

## Git

- Keep commits small enough to review.
- Do not commit generated `build/` files.
- Check `git status --short` before committing.
