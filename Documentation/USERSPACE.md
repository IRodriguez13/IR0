# IR0 kernel ↔ ISD userspace

> **Last verified:** 2026-10-29
> **Source of truth:** this file, the root `Makefile`, `IR0_ISD_INTERFACE`,
> sibling [ISD](https://github.com/IRodriguez13/ISD), [SETUP.md](../SETUP.md).
> **Spanish:** [`esp/USERSPACE.md`](esp/USERSPACE.md)

## Boundary

The kernel alone is not a Unix distribution. Product PID1 (**runit**),
**BusyBox**, login, packages, the rootfs, and the finished MINIX `disk.img`
live in **ISD** (IR0 Software Distribution). IR0 compiles the kernel and boots
a userspace image that someone else built.

| Lives in **IR0** | Lives in **ISD** |
|------------------|------------------|
| Kernel, drivers, UAPI (`includes/uapi/`) | Packages, services, rootfs, profiles |
| Kernel ISO / ARM64 board images | `out/<arch>/images/<profile>/disk.img` |
| QEMU launcher (`scripts/run_qemu.sh`) | `.isdconfig` extras, stage/pack scripts |

**Hard rule:** the IR0 Makefile does not clone, build, or pack ISD. There are no
`first-boot`, `isd`, `run PROFILE=…`, `kmang`, or `poweron` targets. The kernel
build graph is complete without a sibling checkout.

## Build and boot

### 1. Kernel

```bash
git clone https://github.com/IRodriguez13/IR0.git
cd IR0
make check-env
make defconfig
make ir0
```

`make ir0` produces `kernel-x64.iso`. ARM64 equivalents:

```bash
make ir0 arm64
make ir0 arm64 rpi4
```

### 2. Userspace image

Build the sibling distribution with its own Makefile/tooling:

```bash
git clone https://github.com/IRodriguez13/ISD.git ../ISD
make -C ../ISD help          # ISD owns its profiles and targets
```

The exact ISD target names belong to that repository; IR0 does not mirror or
wrap them.

### 3. Boot

```bash
make run DISK=../ISD/out/x86_64/images/minimal/disk.img
```

`DISK` is optional. Without it the kernel boots the ISO and panics at the
`/sbin/init` handoff — there is no in-kernel fallback shell. `DISK=none`
explicitly opts out when a local `disk.img` exists.

`make run` never builds. Rebuild the kernel with `make ir0` after source
changes, and rebuild the userspace image with ISD after userspace changes.

## Interface contract

| File | Repo | Role |
|------|------|------|
| `IR0/IR0_ISD_INTERFACE` | IR0 | Published integration version (`VERSION=1`) + required adapter scripts |
| `ISD/IR0_ISD_INTERFACE_SUPPORTED` | ISD | Maximum interface version this ISD checkout supports |

IR0 exports UAPI with `make headers_install DESTDIR=…`; an external userspace
build consumes that export. ISD checks the interface version before building
packages. The kernel must not name BusyBox or other user programs.

## Configuration layers

| File | Owner | Meaning |
|------|-------|---------|
| `IR0/.config` | kernel | Kconfig |
| `ISD/profiles/<p>/profile.conf` | ISD | login/root/fsck policy |
| `ISD/profiles/<p>/packages.txt` | ISD | mandatory packages |
| `ISD/.isdconfig` | ISD | optional packages + applets |

`PROFILE=…` is an ISD concept. It is not a kernel build variable anymore; the
kernel selects `ARCH` and, on ARM64, `BOARD`.

## ARM64

ARM64 QEMU is a **kernel** target (`qemu-virt`, RPi3/4/5 board images). ISD has
no AArch64 product rootfs today. Do not assume an x86-64 `disk.img` can be
attached to an ARM64 boot.

## Kernel-side userspace contracts

### Shebang scripts (`#!`)

`execve` / `exec_replace_current` in `kernel/elf_loader.c` recognize a leading
`#!` line before ELF validation: interpreter path, optional argument, then the
script path is appended to `argv`. Recursion depth is capped at 4; non-ELF files
without a valid shebang return `-ENOEXEC`. Example: `./script.sh` with
`#!/bin/sh` runs the interpreter on the script path.

### Login / getty notes

After ISD firstboot, `etc/runit/sv/console/run` (`runit_console_run`) prompts for
username/password. Smoke markers such as `LOGIN_USER_READ` go to **`/dev/serial`**
only (not the human TTY). Emitting that tag on every empty/`EINTR`/`EOF` read
used to busy-loop the prompt and flood the serial log. Fixed in ISD:

- `ir0_read_line` retries `EINTR`, treats `r==0` as EOF (not a blank name).
- Login loop tags `LOGIN_USER_READ` only after a non-empty username; backs off
  with `sleep(1)` on read failure.

**Unix reminders (not bugs):** `chown root` with mode `0755` still lets others
execute the file; restrict with `chmod`. `/tmp` as tmpfs is intentional RAM FS.

## Userspace versioning (ISD-owned)

| Layer | Mechanism |
|-------|-----------|
| ISD release | Top-level `ISD/VERSION` → `/etc/os-release` `VERSION_ID` + `/usr/share/isd/release.txt` |
| Package origins | `/usr/share/isd/package-manifest.txt` (`first-party` / `third-party` / `third-party+isd-patches`) |
| Userland base | `USERLAND_BASE=busybox` in `profiles/*/profile.conf` (`coreutils` reserved) |
| Guest CLI | `ir0-status version \| profile \| kernel \| packages \| userland \| busybox` |
| Host CLI | `python3 scripts/userspace_manager.py` (ISD inspector; not a make target) |

Keep IR0 adapting to the Linux/musl ABI; leave package recipes in ISD. Do **not**
share the kernel `.build_number` counter with ISD releases.

Desktop ABI contract: [`DESKTOP_ABI.md`](DESKTOP_ABI.md) (IR0) and ISD
`Documentation/DESKTOP_ABI.md`.
