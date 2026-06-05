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
- app registry at `/system/suvos/apps/registry.tsv`;
- read-only `/system/suvos` system area at boot;
- shell-level localization for `ru` and `en`;
- statically linked x86_64 C++ demo program;
- Python 3 runtime;
- Node.js runtime.

## Build

```sh
make
```

The build downloads the x86_64 kernel and static BusyBox from Alpine `v3.22`, installs Python/Node runtime dependencies into the initramfs rootfs, and builds the C++ demo plus `suvosd` through Docker/OrbStack.

Outputs:

```text
build/kernel/vmlinuz-x86_64
build/initramfs/suvos-initramfs.cpio.gz
```

## Test

```sh
make test
```

This boots SuvOS in QEMU with `suvos.autotest=1`, runs basic commands, checks roles, verifies `/system/suvos` is read-only, runs shell/C++/Python/Node apps, and powers off.

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
  +-- /system/suvos/bin/suvos-shell
        +-- suvos CLI
              +-- /run/suvosd/request
                    +-- suvosd validates and executes commands
```

`suvosd` keeps the main daemon loop free by forking a worker for each request. Worker fan-out is capped, app execution currently has a 30 second timeout, timed-out apps are killed by process group, and app output is capped.

SuvOS-owned files live under:

```text
/system/suvos/
  bin/
  apps/
  config/
  lib/
  security/
  src/
```

`/opt/suvos` is a compatibility symlink to `/system/suvos`.

## Roles and Bootstrap Secret

The current boot starts SuvOS in the `setup` runtime role. This role can read status, list the app registry, run demo applications, and attempt `auth root`. The full `root` runtime role is unlocked with:

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

SuvOS currently uses Alpine `v3.22` as an upstream package source for the kernel, BusyBox, Python, Node.js, musl, and runtime libraries. The project-owned layer is `/init`, `/system/suvos`, `suvosd`, the app registry, localization, and the future UI/service model.

This dependency is acceptable for the prototype. Later stages can replace it with Buildroot or another controlled build pipeline.
