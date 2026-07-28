# Coupling IR0 (kernel) ↔ IR0-userspace

> **Last verified:** 2026-07-28  
> **Source of truth:** this file, `Makefile` (`IR0_USERSPACE_ROOT`, `check-userspace`, `bootstrap-userspace` / `first-boot`), sibling [IR0-userspace](https://github.com/IRodriguez13/IR0-userspace), [SETUP.md](../SETUP.md), [`testing/BUSYBOX_MATRIX.md`](testing/BUSYBOX_MATRIX.md).  
> **Spanish:** [`esp/USERSPACE.md`](esp/USERSPACE.md)

## Why two repositories?

The kernel alone is not a Unix distro. Product PID1 (**runit**), **BusyBox**, login/doas, and `/etc` live in **IR0-userspace**. Boot always does `kexecve("/sbin/init")` from `disk.img`.

| Lives in **IR0** | Lives in **IR0-userspace** |
|------------------|----------------------------|
| Kernel, drivers, UAPI (`includes/uapi/`) | runit, BusyBox, login/doas, product `/etc` |
| Kernel test fixtures (`setup/pid1/`) | Package recipes, services, rootfs profiles |
| Orchestration Make targets that *delegate* | Built ELFs under `out/` |

**Hard rule:** if PID 1 can replace it without recompiling the kernel, it does not live in IR0.

## Fastest path (first time)

Clone **both** trees as siblings (names matter for the default paths):

```bash
git clone https://github.com/IRodriguez13/IR0.git
git clone https://github.com/IRodriguez13/IR0-userspace.git
cd IR0
make check-env
make defconfig

# Or only clone IR0 and let bootstrap clone the sibling:
make first-boot          # → ../IR0-userspace + UAPI + rootfs + ISO
make run                 # QEMU GTK → getty → BusyBox ash
```

Layout expected by defaults:

```text
parent/
├── IR0/                 # this tree (IR0_ROOT)
└── IR0-userspace/       # IR0_USERSPACE_ROOT (override if needed)
```

| Variable | Default | Role |
|----------|---------|------|
| `IR0_ROOT` | set by userspace to `../IR0` | kernel tree for UAPI + MINIX/ISO adapters |
| `IR0_USERSPACE_ROOT` | `../IR0-userspace` | distro builder |
| `IR0_PRODUCT_PROFILE` | `minimal` | First-boot account wizard + doas (`development` = lab autologin) |
| `IR0_WITH_DEVTOOLS` | `1` on `make run` | Inject TinyCC + GNU make; **profile stays `minimal`** (not development) |

Manual wire-up (same result as `first-boot`):

```bash
export IR0_ROOT=$PWD
export IR0_USERSPACE_ROOT=$PWD/../IR0-userspace
make -C "$IR0_USERSPACE_ROOT" fetch headers build ARCH=x86_64
make load-userspace-runit
make run
```

**Note:** the userspace builder uses `ARCH=x86_64`; the kernel accepts that as an alias of `x86-64`, so a leftover env var no longer breaks `make kernel-x64.bin`.

### Product boot vs lab

| Path | Profile | Login |
|------|---------|-------|
| `make run` / `load-userspace-devtools` (default) | **minimal** | Interactive **Create your account** (`ir0-firstboot --wizard`) |
| Lab autologin | `IR0_PRODUCT_PROFILE=development make load-userspace-devtools` | root via `/etc/ir0-autologin` |

Inside the guest after firstboot:

```text
busybox --list          # ~380 applets (nearly-full BusyBox 1.36)
ls --help
df                      # needs sys_statfs (MINIX free zones)
mount                   # lists /proc/mounts
man IR0-boot
```

| Target | Role |
|--------|------|
| `make first-boot` / `bootstrap-userspace` | Clone sibling + minimal rootfs + ISO |
| `make run` | QEMU GTK — **minimal + firstboot**; TinyCC/make ON by default |
| `make run-console` | Same disk, serial only |
| `IR0_WITH_DEVTOOLS=0 make run` | Skip TinyCC/make inject |
| `make busybox-matrix` | Guest applet + `--help` gates → `bb_status.tsv` |
| `make smoke-runit-boot` | Non-interactive boot gate |
| `make prepare-guest-mandocs` | Host-render IR0 `cat7` pages for guest `man` |
| `make check-guest-mandocs` | Assert ASCII pages (not raw mdoc) |

### BusyBox (BUSY-3) — nearly full + flags

Product binary (`IR0-userspace` `ir0_full.config`, base `defconfig`):

- ~**381** applets (~95% of a BusyBox 1.36 defconfig set); login/su stay in `busybox-auth`.
- `SHOW_USAGE` / `VERBOSE_USAGE` / `LONG_OPTS` / fancy flags ON → `ls --help`, `ls -h`, etc.
- Kernel: `USER_STACK_SIZE` **512 KiB** (64 KiB overflowed fat BusyBox on `cp`/`ln`).
- `sys_statfs` / `sys_fstatfs` (MINIX free zones) so BusyBox `df` does not hang walking mounts without a filesystem.
- `sys_mount` is Linux **5-arg** (`flags`, `data`); `MS_REMOUNT|MS_RDONLY` remount works (BusyBox `mount -o remount,ro`). Bind mounts still `-EINVAL`. Legacy `mount("remount", path, "ro"|"rw")` kept for recovery.
- `/proc/mounts` reports `ro`/`rw` from VFS mount flags; `/etc/mtab` → `/proc/mounts`.
- PMM pool **[32 MiB, 512 MiB)** with `USER_MMAP_START` at **512 MiB** (`0x20000000`); mmap hints only in `[USER_MMAP_START, USER_MMAP_END)`.
- `make busybox-matrix`: drain worker pipe **until EOF** after exit (never stop on `EAGAIN`); streaming needle matcher; `BBCASE_*` / `BBMATRIX_END` protocol via `write(1)`; static store (no post-fork mmap); parent uses only `poll(fd,0)` + `poll(NULL,0,ms)` (no blocking poll waiter). See [`testing/BUSYBOX_MATRIX.md`](testing/BUSYBOX_MATRIX.md).
- Still skip `CONFIG_TC` (host UAPI) and BusyBox init/runit applets.

## Guest manuals (`man`) — Implemented

BusyBox `man` + pre-rendered ASCII under `/usr/share/man/cat7/` (no `nroff`/`mandoc` in the guest). Host builds pages with `mandoc -Tascii` via `make prepare-guest-mandocs`; `load-userspace-runit` / `first-boot` inject them by default (`IR0_GUEST_MANDOCS=0` to skip).

MINIX v1 names are ≤14 characters, so two long host titles are shortened on disk:

| Host page | Guest `man` | Path |
|-----------|-------------|------|
| IR0-boot | `IR0-boot` | `/usr/share/man/cat7/IR0-boot.7` |
| IR0-userspace | `IR0-uspace` | `…/IR0-uspace.7` |
| IR0-onboarding | `IR0-onboard` | `…/IR0-onboard.7` |
| IR0-vfs / syscalls / tty / process | same name | `…/IR0-<name>.7` |

| Contract | State |
|----------|--------|
| `man IR0-boot` (readable text, not `.Sh` macros) | **Implemented** |
| Subset above (7 pages) | **Implemented** |
| Full mandoc catalog / Spanish in guest | **Partial** — host `make sync-mandocs` / `man TOPIC=…` |
| Generic Linux pages (`man ls`) | Out of scope |

```text
man IR0-boot
man IR0-uspace
man IR0-onboard
man -w IR0-tty          # → /usr/share/man/cat7/IR0-tty.7
```

## Suggested layout

```text
parent/
├── IR0/                 # this tree
├── IR0-userspace/       # https://github.com/IRodriguez13/IR0-userspace
└── IR0-desktop/         # optional
```

```bash
export IR0_USERSPACE_ROOT=/path/to/IR0-userspace   # if not ../IR0-userspace
export IR0_ROOT=/path/to/IR0                         # from the userspace tree
```

`make check-userspace` **fails** if the sibling is missing (never silent skip-as-PASS).

## Manual wire-up (same steps as the script)

```bash
git clone https://github.com/IRodriguez13/IR0-userspace.git ../IR0-userspace
make headers_install DESTDIR=../IR0-userspace/out/sysroot
make check-userspace
make load-userspace-runit          # builds BusyBox/runit via sibling + injects disk.img
make kernel-x64-userspace.iso
make run
```

From `IR0-userspace`:

```bash
export IR0_ROOT=../IR0
make fetch build rootfs
```

## Boot contract: no init / init dies

| Situation | Kernel behavior |
|-----------|-----------------|
| `disk.img` missing `/sbin/init` or `kexecve` fails | **`panic("Failed to load /sbin/init")`** — there is no in-kernel shell |
| Init loads and scheduler runs | Normal product path (runit → getty → ash) |
| Init **exits** | Children reparented; kernel **idle** loop keeps the CPU alive — not a second panic by default. Userspace is effectively dead (no getty). |
| Init never present because sibling not loaded | Same as first row: panic at handoff |

There is **no** dbgshell fallback. Always inject a product rootfs (`make first-boot` / `load-userspace-runit`) before expecting a shell.

## What this tree must not re-add

- BusyBox / runit / doas sources or product `/etc` under `setup/`
- In-kernel mono shell (`dbgshell`) or `debug_bins/` command registry
- `#include` of product userspace into the kernel link
