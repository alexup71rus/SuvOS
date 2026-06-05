# AGENTS.md

## Project

SuvOS is an x86_64 Linux-based OS prototype. The project currently owns the initramfs userspace, `/init`, `/system/suvos`, `suvosd`, app registry, localization, and QEMU boot/test flow. Alpine `v3.22` is an upstream package source, not the product identity.

## Default Language

Use Russian for user-facing explanations unless the user asks otherwise. Code identifiers, comments, commit messages, and technical docs may stay English when that is clearer.

## Safety

- Do not install SuvOS onto the host machine.
- Test through QEMU only.
- Do not remove generated boot artifacts unless the user asks or they are clearly temporary cache/log/rootfs output.
- Do not commit `build/` artifacts.
- Do not weaken the `/system/suvos` read-only boot behavior without calling it out explicitly.

## Architecture Rules

- Keep project-owned system files under `/system/suvos`.
- Keep `/opt/suvos` as a compatibility symlink only.
- `suvosd` is the privileged control plane and should stay compiled C++.
- `suvos` CLI and future UI must talk to `suvosd`; they must not launch arbitrary system paths directly.
- Apps must be declared in `/system/suvos/apps/registry.tsv`.
- Runtime files may be shell/Python/Node during prototyping, but privileged logic belongs in `suvosd` or another compiled system component.
- Keep localization wired through `SUVOS_LANG`; currently supported values are `ru` and `en`.

## Verification

Before handing off functional changes, run:

```sh
make test
```

The test must boot QEMU, verify the system area is read-only, run `suvosd`, and execute shell/C++/Python/Node demo apps.

Useful manual run:

```sh
make run
```

## Git

- Keep commits small enough to review.
- Do not commit generated `build/` files.
- Check `git status --short` before committing.
