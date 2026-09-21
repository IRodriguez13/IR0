# IR0 Userspace Bootstrap

| Field | Value |
|-------|-------|
| Version | 0.3 |
| IR0 phase | T1–T2 |
| Status | stable |
| Depends on | boot, process, vfs, tty |
| Man page | IR0-userspace (section 7) |
| Primary sources | `kernel/main.c`, `scripts/make/isd.mk`, `scripts/kernel_manager.py`, `scripts/userspace_manager.py`, sibling `ISD/` |

> **Last verified:** 2026-09-18

> **Note (2026-09-18):** product rootfs and profiles are built from the sibling **`ISD/`**
> tree (`IR0_ISD_ROOT`). The kernel repo drives boot via `scripts/make/isd.mk`:
> persistent machine disks, `make kmang` (kernel ISO catalog TUI), and `make usmang`
> (host ISD inspector). Legacy `IR0-userspace/` naming is aliased to `IR0_ISD_ROOT`.

> **Note (2026-07-24):** transitional PID1 **irinit** was removed. Product and tests use **runit** only (`make build-runit` / `load-userspace-runit` / `smoke-runit-boot`).

## 1. Overview

Production boot always loads **`/sbin/init`** via `kexecve` from `kmain`.
The canonical PID1 is **runit** (`IR0-userspace/out/bin/runit-init`
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
| `busybox-1.36.1` | Applets; recipe and configs in `IR0-userspace/packages/busybox/` |
| `kernel-x64-userspace.bin` | Kernel built with `IR0_USERSPACE_INIT_BOOT=1` |

**runit behavior:** stage 1 prepares the rootfs; stage 2 supervises services; the
console service runs getty/login (`runit_console_run`): auth against
`/etc/passwd`+`/etc/shadow` (musl `crypt(3)` for `$…` hashes), then
`setgid`/`setgroups`/`setuid`/`chdir`, export of `HOME`/`USER`/`HOSTNAME`, and
exec of BusyBox ash as a **login shell** (`argv[0] = "-sh"`) so `/etc/profile`
runs. Product BusyBox enables `CONFIG_ASH_EXPAND_PRMT` and `CONFIG_ASH_TEST`
(`[`/`test` builtins). Prompt (`PS1`): `# ` for root, `user@host:$PWD$ ` for
others (`HOSTNAME` from `/etc/hostname`, default `unix`). Accounts: `root`
(empty password), `ir0`/`ivan` (MD5 crypt). Lab disks may include
`/etc/ir0-autologin` (see FASE58E / `run-fase58e-ash-gui`). Smokes:
`smoke-runit-login` (root autologin), `smoke-runit-login-nonroot` (ivan + crypt).

**Product profiles.** `/etc/ir0-profile` (written by
`IR0-userspace/scripts/install-to-disk.sh` from `IR0_PRODUCT_PROFILE`) selects
the console policy: `development` keeps root autologin and prints a warning,
`desktop` runs `hostname login:` and denies direct root login through
`/etc/ir0-noroot` (name kept ≤14 bytes for MINIX v1 directory entries),
`appliance` starts no interactive login at all (`CONSOLE_NO_LOGIN`).

## 3. Data flow

```text
  make kernel-x64-userspace.iso + disk.img
       │
       ▼
  inject_init_minix.py  (runit-init → /sbin/init, busybox → /bin/...)
       │
       ▼
  QEMU boot → kexecve("/sbin/init")
       │
       ▼
  runit stage1 → stage2 → console → /bin/sh (BusyBox ash)
```

## 4. Make targets

| Target | Role |
|--------|------|
| `make headers_install DESTDIR=…` | Export public UAPI (`includes/uapi/`) for the userspace build |
| `make build-runit` | Delegates to `IR0-userspace` (runit + service ELFs) |
| `make load-userspace-runit` | Format MINIX disk and install the product rootfs from the sibling |
| `make smoke-runit-boot` | Headless PID1 boot smoke |
| `make smoke-runit-login` | Root autologin (empty password) |
| `make smoke-runit-login-nonroot` | Non-root: crypt(3) + uid 1001 + PS1 |
| `make run-fase58e-ash-gui` | Interactive ash on GTK |
| `make smoke-tier1` | Bundle: runit boot + ash |
| `make kmang` | Interactive kernel ISO catalog (`scripts/kernel_manager.py`); `poweron` boots Default — see **`man IR0-kmang`** |
| `make usmang` | Host ISD summary; desktop detail only when `PROFILE=desktop` |
| `make smoke-x11-pointer PROFILE=desktop` | Fullscreen TinyX + dual xterm + desktop clients |

Coupling guide: [`USERSPACE.md`](../../USERSPACE.md), [`TOOLING.md`](../../TOOLING.md).

**kmang honesty:** build `#N` is **machine-local** (`.build_number` per host). Compare
hosts by SHA-256 and provenance (`compare`, `info --json`), not by `#N` alone.
The TUI warns when Workspace ISO diverges from Default — `poweron` never silently
boots an unenrolled workspace build. Full contract: **`man IR0-kmang`**.

**usmang honesty:** reads real `ISD/VERSION`, `profiles/<PROFILE>/packages.txt`, and
`resolve-packages.sh`; desktop X clients are listed only for `PROFILE=desktop`.
ISD version is independent of kernel local build numbers.

Retired fail-fast aliases: `build-irinit`, `load-userspace-irinit`,
`smoke-userspace-irinit`, `run-irinit-interactive-gui` (print redirect and exit 2).

## 5. Identity

- Kernel serial banner: `IR0 kernel <IR0_VERSION_STRING>` via `ir0_boot_serial_ready()`.
- Userspace: `uname` → sysname `IR0`, nodename `unix`, version `IR0/Unix`.
- `/proc/version` human line matches the same stamp.

## 6. Open (post-0.0.1)

- `IR0-system` release manifest pinning kernel + userspace + desktop commits.
- Interactive first-boot wizard (TTY Q&A) — defaults via `ir0-firstboot` today.
- BusyBox `login`/`su`/`passwd` applets (optional; custom console already drops privileges).
