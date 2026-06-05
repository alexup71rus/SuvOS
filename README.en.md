# SuvOS

SuvOS is currently a minimal x86_64 Linux-based OS prototype:

- prebuilt Alpine `v3.22` `linux-virt` kernel;
- custom initramfs;
- SuvOS `/init`;
- SuvOS console shell;
- BusyBox for basic Unix-compatible commands;
- role-system stub that currently grants root-like access;
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
