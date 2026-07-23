# IR0 Makefile — Central Tooling Orchestrator

The root `Makefile` is the single entry point for building, configuring, validating, and
running the IR0 kernel. Sub-makefiles (`tests/host/Makefile`, `tests/kernel_memsafe/Makefile`)
handle isolated harnesses; everything else flows through the root file.

## Design role

| Concern | Makefile responsibility |
|---------|-------------------------|
| Architecture selection | `ARCH`, `CONFIG_ARCH_*`, cross-compile prefixes |
| Feature composition | `-include .config` gates `OBJS`, `CFLAGS`, and ISO contents |
| Version stamping | `IR0_VERSION_*`, build number, date/user/host in `version.o` |
| Developer UX | `help`, `menuconfig`, `unibuild`, smoke and matrix targets |
| Quality gates | `arch-guard`, `repo-hygiene-guard`, `build-matrix-*`, `health` |

Python scripts under `scripts/` implement logic that would be unreadable in Make; the Makefile
invokes them with stable target names. Focused smoke fragments live under
`scripts/make/` (e.g. `boot-audio.mk` → `smoke-sb16-probe`, `class-b.mk` →
`smoke-class-b-mitigated`) and are `-include`d from the root Makefile.

## Configuration flow

```text
setup/defconfig  ──►  .config  ──►  -include .config
                         │
                         ├── CONFIG_* gates object lists (OBJS, DRIVER_OBJS, …)
                         ├── setup/generated/autoconf.h (via menuconfig / kconfig)
                         └── CFLAGS_TARGET profiles (-DIR0_DESKTOP, …)
```

- **`make defconfig`** — baseline `.config` from `setup/defconfig`.
- **`make menuconfig`** — interactive TUI (`scripts/kconfig/menuconfig.py`).
- **`python3 scripts/kconfig/menuconfig.py --set KEY=VALUE`** — non-interactive updates.

When `.config` is absent, `CONFIG_*` variables are empty and most `ifneq ($(CONFIG_FOO),n)` guards
default subsystems to **enabled** (bring-up friendly).

## Target taxonomy

### Build

| Target | Output |
|--------|--------|
| `make all` / `make ir0` | Kernel binary + ISO (default profile) |
| `make kernel-x64.bin` | Linked ELF only |
| `make kernel-x64.iso` | Bootable ISO |
| `make kernel-x64-userspace.iso` | Userspace-oriented ISO (init/musl smokes) |
| `make clean` | Remove objects, ISOs, generated smoke logs |

### Run (QEMU)

| Target | Notes |
|--------|-------|
| `make run` | GUI + `disk.img` (recommended) |
| `make run-console` | Serial-focused |
| `make run-gdb` | Wait for GDB on localhost:1234 |
| `make run-fase58e-ash-gui` | Interactive BusyBox ash |
| `make run-irinit-interactive-gui` | irinit + ash path |

Disk lifecycle: `create-disk`, `delete-disk`, `load-init`, `load-userspace-rootfs`.

### Validation and gates

| Target | Script / mechanism |
|--------|-------------------|
| `make arch-guard` | `scripts/architecture_guard.py` |
| `make repo-hygiene-guard` | `scripts/repo_hygiene_guard.py` |
| `make build-matrix-min` | Multiple `.config` profiles, compile-only |
| `make build-matrix-full` | Extended matrix + runtime checks |
| `make health` | `kernel-analyze` + memsafe + kernel-tests |
| `make smoke-*` | QEMU smokes via `scripts/smoke_qemu_run.sh` |

### Documentation

| Target | Output |
|--------|--------|
| `make sync-mandocs` | Rebuild + install → `~/.local/share/man` (no sudo) |
| `make man TOPIC=boot` | `man IR0-boot` without requiring MANPATH setup |
| `make mandocs` / `mandocs-en` | Build manuals (interactive / one language) |
| `make mandocs-view` | Open installed / built `IR0-krnl` |
| `make ai-dev-rules-install` | Install AI rules into gitignored `.cursor/` |

### Developer tooling

| Target | Purpose |
|--------|---------|
| `make help` | Full target listing (always current) |
| `make help-profiles` | Product profiles + honest ceilings |
| `make help-docs` / `help-pre-submit` | Docs and contributor gate help |
| `make format` | `clang-format` over C/H sources |
| `make compile-commands` | `compile_commands.json` for LSP |
| `make unibuild <file.c>` | Compile single translation units in isolation |
| `make check-env` / `deptest` | Host env diagnostic (`PROFILE=desktop-x86_64` default) |
| `make deptest PROFILE=…` | `desktop-x86_64` \| `userspace` \| `hub-rpi4` \| `watch` \| `all` |
| `make pre-submit [SUBSYSTEM=mm]` | Local gate → `PRE_SUBMIT_OK` (no push) |

## Object list composition

The Makefile builds `ALL_OBJS` from layered fragments:

- **Core:** `kernel/`, `fs/`, `mm/`, `sched/`, `includes/ir0/`
- **Arch:** `arch/x86-64/` or `arch/arm64/` selected by `ARCH`
- **Drivers:** gated by `CONFIG_ENABLE_*` and `CONFIG_INIT_*`
- **Debug:** `debug_bins/` when `CONFIG_KERNEL_DEBUG_SHELL=y`
- **Generated:** `version.o`, kconfig headers

Adding a new subsystem requires **four wiring points** (CTR rule):

1. `setup/Kconfig` symbol
2. `setup/defconfig` default
3. `Makefile` object list / `CFLAGS`
4. Optional menuconfig preset in `scripts/kconfig/menuconfig.py`

## Smoke and fase targets

Historical bring-up targets use `faseNN` / `smoke-faseNN` prefixes. They share:

- Init binary build (`build-init-*`, `build-busybox-*`)
- MINIX rootfs injection (`scripts/inject_init_minix.py`)
- QEMU runner with timeout and done-tag matching (`scripts/smoke_qemu_run.sh`)

Prefer extending existing smoke patterns over ad-hoc QEMU shell loops.

## Relationship to Documentation

| Doc | Makefile topic |
|-----|----------------|
| `TOOLING.md` | Day-to-day targets and config keys |
| `DECOUPLING.md` | Why arch-guard exists |
| `Documentation/ai_driven_dev/` | Agent rules for Makefile/Kconfig changes |
| `SETUP.md` | First-time bootstrap sequence |

For the authoritative list of targets, run **`make help`** — it is maintained alongside the Makefile.
