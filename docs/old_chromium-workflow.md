Актуальный план написан в reaadme.

# Chromium Workflow

This document defines how SuvOS works with `SuvOS_Chromium`.

## Rules

- `SuvOS_Chromium` is a real Chromium fork.
- Changes are made directly in the Chromium source tree.
- Do not keep a separate SuvOS change queue outside the fork.
- Do not add a separate SuvOS-only source zone as the main integration model.
- Do not delete Chromium platform directories before a clean baseline build works.
- Do not test browser changes first inside the SuvOS VM.
- The default development loop is host-first: build and run Chromium on the current workstation, against a local SuvOS gateway.
- SuvOS VM integration comes after the Chromium build works on the host.
- The main SuvOS repo stores only the fork URL and pinned commit in `third_party/vendors.lock.json`.

## Target Workflow

This is the workflow SuvOS should converge to.

1. Work in the macOS `SuvOS_Chromium` fork.
   - Build and run Chromium directly on the workstation.
   - Validate browser UI patches in the host Chromium binary first.
   - Validate SuvOS-specific toolbar, navigation, layout, and shell behavior
     before packaging anything into the OS image.

2. Move SuvOS OS pages into Chromium-owned surfaces.
   - The final direction is `suvos://...` internal pages and browser chrome
     integration, not ordinary web pages opened at `http://suv.os/`.
   - `http://suv.os/` is an interim guest gateway route, useful while the OS
     shell is still browser-hosted.
   - Settings, power/shutdown flows, privileged internal pages, and other
     OS-level screens should eventually become Chromium fork surfaces.

3. Connect those pages to either a mock backend or a local SuvOS gateway.
   - Use a mock backend for pure UI, toolbar, layout, and navigation work.
   - Use the Docker-hosted local gateway when real `suvosd` state, roles,
     health checks, or API behavior matter.
   - Host-served development pages must talk to the mock or Docker gateway,
     not to QEMU.

4. Only after host Chromium works, package it for SuvOS.
   - Build a Chromium artifact from the tested `SuvOS_Chromium` commit.
   - Install that artifact into the SuvOS image.
   - Run the SuvOS GUI/QEMU smoke path as the final integration check.
   - Debug guest-specific graphics, sandbox, profile, and packaging issues
     only at this stage.

5. Pin the tested fork revision.
   - Update `third_party/vendors.lock.json` in the main SuvOS repo to the
     tested `SuvOS_Chromium` ref.
   - The main SuvOS repo stores only the fork URL and pinned commit/tag.

## Current TODO

This is the active Chromium integration track.

1. Build and run the Chromium fork locally on macOS.
   - Start with a clean upstream-equivalent build.
   - Keep Chromium source changes directly inside `SuvOS_Chromium`.
   - Do not add patch queues for Chromium in the main SuvOS repo.

2. Prepare the browser-side SuvOS shell surfaces.
   - Add `suvos://` pages when the fork is ready for Chromium-owned pages.
   - Move toolbar/settings/power/shutdown integration into the Chromium fork.
   - Keep the current `http://suv.os/` route as the guest fallback until those
     surfaces exist.

3. Implement the first Chromium patch set.
   - Add a clock near the current window close area.
   - Replace the normal close button with a SuvOS shutdown button.
   - Add `suvos://settings` as a SuvOS-owned settings surface.
   - `suvos://settings` must supplement Chromium settings, not replace or fork
     the existing Chromium settings page wholesale.

4. Make the SuvOS gateway available from the host through Docker.
   - Use `make chromium-dev-gateway`.
   - The gateway must expose `http://127.0.0.1:8080/` on the host.
   - The real `suvos-gateway` process must still bind only to `127.0.0.1`
     inside the container.
   - `/system/suvos` must be mounted read-only in the container.
   - `/run` and `/data` must stay runtime-only container state.

5. Make host-served SuvOS pages use a mock backend or the Docker gateway.
   - If a page is served by `suvos-gateway`, relative requests such as
     `/health` and `/api/status` are already correct.
   - If a page is served by a separate host dev server, that dev server must
     proxy `/health` and `/api/*` to `http://127.0.0.1:8080`.
   - If a page cannot use a proxy, it needs an explicit development API base
     pointed at `http://127.0.0.1:8080`.
   - Do not hardcode the Docker gateway URL into production UI bundles.

6. After host Chromium works, run Chromium through SuvOS/QEMU.
   - Build a Chromium artifact from the pinned `SuvOS_Chromium` commit.
   - Install that artifact into the SuvOS image.
   - Run the existing GUI smoke path.
   - Only then debug guest-specific graphics, sandbox, profile, and packaging
     issues.

## Local Gateway

The local gateway is a Docker-hosted development harness. It exists so Chromium
can be tested on the workstation before it is packaged into the OS image.

Start it from the main SuvOS repo:

```sh
make chromium-dev-gateway
```

Default URL:

```text
http://127.0.0.1:8080/
```

Useful overrides:

```sh
SUVOS_DEV_GATEWAY_PORT=8081 scripts/run-chromium-dev-gateway.sh
SUVOS_ARCH=x86_64 scripts/run-chromium-dev-gateway.sh
SUVOS_ARCH=aarch64 scripts/run-chromium-dev-gateway.sh
SUVOS_DEV_GATEWAY_CONTAINER=suvos-chromium-dev-gateway-2 scripts/run-chromium-dev-gateway.sh
```

The script builds:

- `build/ui`
- `build/suvosd/suvosd-<arch>`
- `build/suvos-gateway/suvos-gateway-<arch>`

Then it starts an Alpine container with:

- `/system/suvos` mounted read-only from a generated local stage
- `/data` and `/run` as tmpfs
- `suvosd`
- `suvos-gateway`
- a small TCP forwarder from container port `8080` to gateway port `80`

The host maps `127.0.0.1:$SUVOS_DEV_GATEWAY_PORT` to the container forwarder.

## Local Chromium Run

Use the Chromium binary from the fork build output and point it at the Docker
gateway:

```sh
/path/to/SuvOS_Chromium/out/<config>/chrome \
  --user-data-dir=/tmp/suvos-chromium-profile \
  --no-first-run \
  --no-default-browser-check \
  http://127.0.0.1:8080/
```

For work that needs the `suv.os` hostname, use a temporary host resolver rule:

```sh
/path/to/SuvOS_Chromium/out/<config>/chrome \
  --user-data-dir=/tmp/suvos-chromium-profile \
  --host-resolver-rules='MAP suv.os 127.0.0.1' \
  http://suv.os:8080/
```

Do not use `--no-sandbox` as the default. If a local development build needs a
sandbox escape for a specific failure, keep that as an explicit one-off command
and document why it was needed.

## What Not To Remove Yet

Do not remove Chromium directories such as:

- `android_webview`
- `ash`
- `chromeos`
- `components`
- `content`
- `ios`
- `third_party`
- top-level build files such as `DEPS`, `.gn`, `BUILD.gn`

Those directories are not SuvOS runtime features, but they can still be part of
Chromium's source and build graph. Removal is a separate experiment after the
baseline build is green.
