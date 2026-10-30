# IR0 Userspace Bootstrap

| Field | Value |
|-------|-------|
| Version | 0.3 |
| IR0 phase | T1–T2 |
| Status | stable |
| Depends on | boot, process, vfs, tty |
| Man page | IR0-ISD coupling (section 7) |
| Primary sources | `kernel/main.c`, root `Makefile`, `IR0_ISD_INTERFACE`, `Documentation/USERSPACE.md`, sibling `ISD/` |

> **Last verified:** 2026-10-29

> **Build-surface note (2026-10-29):** the IR0 Makefile no longer clones or
> builds ISD. Product rootfs and profiles are built by the sibling **`ISD/`**
> tree (`IR0_ISD_ROOT`). Kernel targets are `make ir0`, `make run DISK=…`,
> `make headers_install`, and the CTR/ktest gates. Targets such as
> `first-boot`, `build-runit`, `load-userspace-runit`, `run-ash-gui`,
> `smoke-tier1`, `kmang`, `poweron`, and `usmang` were removed.

> **Note (2026-07-24):** transitional PID1 **irinit** was removed. Product and
> tests use **runit** only; the rootfs is assembled by ISD.


## 1. Overview

Production boot always loads **`/sbin/init`** via `kexecve` from `kmain`.
The canonical PID1 is **runit** (`ISD/out/bin/runit-init`
and stage services), built static with musl by the sibling repository and
injected into the MINIX root image. BusyBox, TCC, and DoomGeneric are optional rootfs payloads for smoke and
T2 graphics milestones.

## 2. Internal architecture

| Artifact | Role |
|----------|------|
| `runit-init` + stages | PID1: stage1 → stage2 services → console/ash |
| `init_musl.c` | musl syscall smoke binary |
| `rootfs_base.c` | Creates `/bin`, `/sbin`, `/dev`, `/proc`, … on disk |
| `inject_init_minix.py` | Writes binaries into MINIX v1 image |
| `busybox-1.36.1` | Applets; recipe and configs in `ISD/packages/busybox/` |
| `kernel-x64.iso` | Kernel ISO built by this tree |
| `disk.img` | ISD-built rootfs image, attached at runtime with `DISK=…` |

**runit behavior:** stage 1 prepares the rootfs; stage 2 supervises services; the
console service runs getty/login (`runit_console_run`): auth against
`/etc/passwd`+`/etc/shadow` (musl `crypt(3)` for `$…` hashes), then
`setgid`/`setgroups`/`setuid`/`chdir`, export of `HOME`/`USER`/`HOSTNAME`, and
exec of BusyBox ash as a **login shell** (`argv[0] = "-sh"`) so `/etc/profile`
runs. Product BusyBox enables `CONFIG_ASH_EXPAND_PRMT` and `CONFIG_ASH_TEST`
(`[`/`test` builtins). Prompt (`PS1`): `# ` for root, `user@host:$PWD$ ` for
others (`HOSTNAME` from `/etc/hostname`, default `unix`). Accounts: `root`
(empty password), `ir0`/`ivan` (MD5 crypt). Lab disks may include
`/etc/ir0-autologin` (see FASE58E / `run-ash-gui`). Smokes:
`smoke-runit-login` (root autologin), `smoke-runit-login-nonroot` (ivan + crypt).

**Product profiles.** `/etc/ir0-profile` (written by
`ISD/scripts/install-to-disk.sh` from `IR0_PRODUCT_PROFILE`) selects
the console policy: `development` keeps root autologin and prints a warning,
`desktop` runs `hostname login:` and denies direct root login through
`/etc/ir0-noroot` (name kept ≤14 bytes for MINIX v1 directory entries),
`appliance` starts no interactive login at all (`CONSOLE_NO_LOGIN`).

## 3. Data flow

```text
  make ir0                        # kernel-x64.iso
       │
       ▼
  make run DISK=../ISD/out/.../disk.img
       │
       ▼
  QEMU boot → kexecve("/sbin/init")
       │
       ▼
  runit stage1 → stage2 → console → /bin/sh (BusyBox ash)
```

The rootfs image is produced by ISD's own tooling. IR0 does not inject
binaries into it as part of the kernel build.

## 4. Make targets

| Target | Role |
|--------|------|
| `make run DISK=…` | Boot the kernel ISO with an ISD-built userspace disk |
| `make headers_install DESTDIR=…` | Export public UAPI (`includes/uapi/`) for an external userspace build |
| `make check-env PROFILE=userspace` | Host toolchain used by external ISD builds |
| `make ir0` | Kernel ISO only (no userspace) |

ISD owns its own target names (`build`, `stage-rootfs`, `pack-minix`, …); IR0
does not mirror or wrap them. See `Documentation/USERSPACE.md` and
[`../USERSPACE.md`](../../USERSPACE.md).

Coupling guide: [`USERSPACE.md`](../../USERSPACE.md), [`TOOLING.md`](../../TOOLING.md).

**Build number honesty:** build `#N` is **machine-local** (`.build_number` per
host). Compare hosts by SHA-256 and provenance, not by `#N` alone. ISD release
versioning is independent of the kernel counter.

The retired `irinit` aliases and the old userspace make targets were removed.
Use `make ir0` + `make run DISK=…` and ISD's own build commands.

## 5. Identity

- Kernel serial banner: `IR0 kernel <IR0_VERSION_STRING>` via `ir0_boot_serial_ready()`.
- Userspace: `uname` → sysname `IR0`, nodename `unix`, version `IR0/Unix`.
- `/proc/version` human line matches the same stamp.

## 6. Open (post-0.0.1)

- `IR0-system` release manifest pinning kernel + userspace + desktop commits.
- Interactive first-boot wizard (TTY Q&A) — defaults via `ir0-firstboot` today.
- BusyBox `login`/`su`/`passwd` applets (optional; custom console already drops privileges).
