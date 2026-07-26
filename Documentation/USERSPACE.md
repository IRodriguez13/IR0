# Coupling IR0 (kernel) ↔ IR0-userspace

> **Last verified:** 2026-07-26  
> **Source of truth:** this file, `Makefile` (`IR0_USERSPACE_ROOT`, `check-userspace`, `headers_install`, `bootstrap-userspace`), sibling [IR0-userspace](https://github.com/IRodriguez13/IR0-userspace), [SETUP.md](../SETUP.md).  
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

From an empty parent directory:

```bash
git clone https://github.com/IRodriguez13/IR0.git
cd IR0
make check-env
make defconfig

# Clones ../IR0-userspace if missing, exports UAPI, builds runit+BusyBox → disk.img + ISO
make first-boot

# Boot the minimal distro (getty → BusyBox ash)
make run
```

Equivalent one-liner from the kernel tree after `defconfig`:

```bash
./scripts/bootstrap-userspace.sh && make run
```

Inside the guest (development profile often autologins as root):

```text
busybox
ls /
cat /proc/version
echo hello
man IR0-boot
man IR0-uspace
man -w IR0-tty
```

| Target | Role |
|--------|------|
| `make first-boot` / `bootstrap-userspace` | Clone sibling + minimal rootfs + ISO |
| `make run` | QEMU GTK — runit + BusyBox (no TinyCC required) |
| `make run-console` | Same disk, serial only |
| `IR0_WITH_DEVTOOLS=1 make run` | Also inject TinyCC + GNU make (optional) |
| `make smoke-runit-boot` | Non-interactive boot gate |
| `make prepare-guest-mandocs` | Host-render IR0 `cat7` pages for guest `man` |
| `make check-guest-mandocs` | Assert ASCII pages (not raw mdoc) |

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
