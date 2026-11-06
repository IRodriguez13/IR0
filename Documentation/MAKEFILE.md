# IR0 Makefile — Lean Build Entry Point

The root `Makefile` is the single entry point for building, configuring, testing, and
running the IR0 kernel. Sub-makefiles (`tests/host/Makefile`, `tests/kernel_memsafe/Makefile`)
handle isolated harnesses; everything else flows through the root file.

The public surface is deliberately small: one build target per architecture/board,
configuration TUIs, the KTM/ktest gates needed by contributors, docs, and unibuild.
There is no extended QA/ISD/release/smoke catalog in this tree; those entries were
removed in the 2026-10-29 build-system redesign.

## Design role

| Concern | Makefile responsibility |
|---------|-------------------------|
| Architecture selection | `ARCH`, `CONFIG_ARCH_*`, cross-compile prefixes |
| Board selection | `BOARD=qemu-virt\|rpi3\|rpi4\|rpi5` → `ARM64_BOARD` |
| Feature composition | `-include .config` gates `OBJS`, `CFLAGS`, and ISO contents |
| Version stamping | `IR0_VERSION_*`, build number, date/user/host in `version.o` |
| Developer UX | `help`, `menuconfig`, `unibuild`, test TUI |
| Quality gates | `arch-guard`, `ctr`, `build-matrix-min`, `health` |

Python scripts under `scripts/` implement logic that would be unreadable in Make; the
Makefile invokes them with stable target names. Target recipes live in small fragments
under `scripts/make/`:

| Fragment | Contents |
|----------|----------|
| `arch-sources.mk` | Architecture object inventories and freestanding exceptions |
| `arm64.mk` | ARM64 boot/aggregate/board build graph (parsed only for `ARCH=arm64`) |
| `arm64-board.mk` | RPi3/4/5 `kernel8.img` packaging (ARM64 only) |
| `clean.mk` | `make clean` |
| `docs.mk` | Man pages and docs helpers |
| `host-portable.mk` | POSIX host helpers (`HOST_TMP`) |
| `ktm.mk` | `kernel-tests`, host harnesses, CTR/KTM gates, test catalog |

There is no ISD, product, release, profile, QA, or Windows fragment. The root file is
the complete graph for a kernel checkout.

## Parse cost

Every recursive make re-parses the root file, so the parse path is kept cheap on
purpose:

- `.SUFFIXES:` is cleared at the top. Every rule this tree uses is explicit;
  leaving the built-in suffix list active makes make search it for the
  thousands of prerequisites recorded in generated `.d` files, which costs
  more than parsing the Makefile.
- Build metadata (date, time, user, host, counter) comes from one `$(shell)`
  instead of five; the compiler banner costs one fork.
- Dependency includes are wrapped in `$(wildcard ...)` so a first build never
  asks make to remake hundreds of missing `.d` files.
- The ARM64 graph is only parsed when `ARCH=arm64`.
- There is no conditional QA catalog anymore: `help`, `clean`, `ir0`, `run`
  and every gate parse the same small set of fragments.

Measure with `make -n help` and `make -n ir0 arm64`; both should stay well under
a tenth of a second on a normal workstation.

## Positional syntax

Architecture and board are ordinary make goals as well as variables:

```bash
make ir0 x86-64
make ir0 arm64
make ir0 arm64 rpi4
make run arm64 console
```

| Token class | Values | Effect |
|-------------|--------|--------|
| Architecture | `x86-64`, `x86_64`, `amd64`, `arm64`, `aarch64` | Sets `ARCH` |
| Board (ARM64) | `qemu-virt`, `rpi3`, `rpi4`, `rpi5` | Sets `BOARD` |
| Run mode | `gui`, `console`, `debug`, `fullscreen`, `gdb` | Alias of `make run <mode>` |

The tokens are also declared as no-op goals so make accepts them. `ARCH=` and
`BOARD=` environment variables keep working.

Goals whose names start with **`kernel-arm64`**, **`smoke-arm64`**, or **`arm64-`**
(including `make kernel-arm64.bin` or `make smoke-arm64-hypervisor-log`)
**override `ARCH` to `arm64`** so the ARM64 makefile fragments load even when
`.config` still selects x86-64 as the default architecture.

## Configuration flow

```text
setup/defconfig  ──►  .config  ──►  -include .config
                         │
                         ├── CONFIG_* gates object lists (OBJS, DRIVER_OBJS, …)
                         ├── includes/generated/autoconf.h (via menuconfig / kconfig)
                         └── CFLAGS_TARGET profiles (-DIR0_DESKTOP, …)
```

- **`make defconfig`** — baseline `.config` from `setup/defconfig`.
- **`make menuconfig`** — interactive TUI (`scripts/kconfig/menuconfig.py`).
  `MENUCONFIG_LANG=es` or `MENUCONFIG_LANG=en` forces the language. Press `/`
  to search symbols, PgUp/PgDn/Home/End to navigate, and `?` for help.
- **`python3 scripts/kconfig/menuconfig.py --set KEY=VALUE`** — non-interactive updates.

When `.config` is absent, `CONFIG_*` variables are empty and most `ifneq ($(CONFIG_FOO),n)` guards
default subsystems to **enabled** (bring-up friendly).

## Target taxonomy

### Build

| Target | Output |
|--------|--------|
| `make onboard` | First contributor build: host check → `defconfig` only if `.config` is absent → `kernel-x64.iso` → `test-fast` |
| `make ir0` | x86-64: `kernel-x64.iso`. ARM64: `kernel-arm64.bin`. Board: `build/arm64-<board>/kernel8.img` |
| `make ir0 arm64` | ARM64 aggregate (`kernel-arm64-all.bin` + common-core/provider gates) |
| `make ir0 arm64 rpi4` | RPi4 `kernel8.img` plus its ELF twin |
| `make all` | Same artifact as `ir0`, without the `-Werror` policy |
| `make kernel-x64.bin` | Linked x86-64 ELF only |
| `make kernel-x64.iso` | Bootable GRUB ISO |
| `make kernel-x64-test.bin` | Kernel built with the in-kernel ktest battery |
| `make kernel-arm64.bin` | ARM64 aggregate (forces `ARCH=arm64` when used as the goal) |
| `make clean` | Remove objects, deps, images and generated headers (disk images, `toolchain/`, `dist/`, `out/` preserved) |

`ir0` compiles with `-Werror`; use `make all` or the individual artifact targets
when a third-party compiler introduces a warning you need to inspect first.
When `CONFIG_TOOL_AUTO_RUN_DEPTEST=y`, `ir0` runs `check-env` first.

ARM64 board images also emit `build/arm64-<board>/kernel8.img`, which is what QEMU's
raspi machines consume.

### Run (QEMU)

`run` only boots an artifact that already exists. It never compiles and never
touches ISD.

| Target | Notes |
|--------|-------|
| `make run` | GUI on x86-64; serial on ARM64 |
| `make run console` | `-nographic` |
| `make run debug` | Guest-error/int log + monitor on telnet `127.0.0.1:1234` |
| `make run fullscreen` | GTK full screen |
| `make run gdb` | Wait for GDB on localhost:1234 |
| `make run arm64 rpi4` | Boot `build/arm64-rpi4/kernel8.img` |

`run` boots the artifact from the last `make ir0` when the command line does
not name an architecture. A board word alone (`make run rpi4`) selects ARM64.
`make run x86-64` forces x86 after an ARM build.

On x86-64 the disk is the ISD profile root (`PROFILE=minimal` by default,
`../ISD/out/x86_64/images/<profile>/`). The repo `disk.img` is used only when
that image is missing. `DISK=none` boots the kernel with no root disk.

### Tests and gates

| Target | Script / mechanism |
|--------|-------------------|
| `make kernel-tests` | In-kernel test battery in QEMU (headless) |
| `make test` | Interactive test TUI (`scripts/test_tui.py`; `/` filters suites) |
| `make test SUITE=list` | List catalog suites |
| `make test SUITE=host,ktest` | Run catalog suites non-interactively |
| `make test-fast` | `kernel-x64.bin` + `arch-guard` + host tests + KTM guards |
| `make agent-fast` | `test-fast` + `kernel-tests` |
| `make ctr` | `scripts/ctr.sh`: kernel + arch-guard + matrix-min + host tests |
| `make build-matrix-min` | Daily `.config` matrix (defconfig, tiny, net off, storage off, tmpfs root, arm64 scaffold, lazy MM off); restores `.config` |
| `make build-matrix-full` | `build-matrix-min` plus scheduler, split storage, USB and Bluetooth; for CI |
| `make arch-guard` | Architecture boundary checks (`scripts/architecture_guard.py`) |
| `make repo-hygiene-guard` | Repository hygiene (`scripts/repo_hygiene_guard.py`) |
| `make health` | `kernel-analyze` + text budget + memsafe + kernel tests |
| `make tests` | Build the in-kernel test kernel, kernel-memsafe harness and host tests |

Optional ARM64 QEMU checks (require `qemu-system-aarch64` where noted): see
`make smoke-arm64-hypervisor-log`, `smoke-arm64-rpi3-boot`, `smoke-arm64-rpi4-boot`
in `scripts/make/arm64.mk` / `arm64-board.mk`.

The catalog (`scripts/test_catalog.yaml`) lists only public gates that still
exist. `make test` opens the picker; `SUITE=<id>` runs suites non-interactively.

### Documentation

| Target | Output |
|--------|--------|
| `make sync-mandocs` | Rebuild + install → `~/.local/share/man` (no sudo) |
| `make man TOPIC=boot` | `man IR0-boot` without requiring MANPATH setup |
| `make mandocs` / `mandocs-en` / `mandocs-es` | Build manuals (interactive / one language) |
| `make mandocs-view` | Open installed / built `IR0-krnl` |
| `make mandocs-uninstall` | Remove installed pages |
| `make ai-dev-rules-install` | Install tracked rules into gitignored `.cursor/` |
| `make help-docs` | Documentation target help |

### Developer tooling

| Target | Purpose |
|--------|---------|
| `make help` | Full target listing (always current) |
| `make format` | `clang-format` on tracked `*.c`/`*.h`, excluding `toolchain/` and `setup/` |
| `make analyze` | `cppcheck` on `kernel/`, `mm/`, `fs/`, `net/`, `drivers/`, `arch/`, `sched/` when installed |
| `make compile-commands` | `compile_commands.json` for LSP |
| `make disasm` | `objdump -d -S kernel-x64.bin` |
| `make stack-usage` | Forced kernel rebuild with `-fstack-usage`; does not delete the ISO or disk |
| `make print-build-objects` | Object inventory used by graph audits |
| `make build-graph-audit` | Audit the generated build graph |
| `make unibuild FILE=<file.c>` | Compile translation units in isolation |
| `make unibuild-cpp` / `unibuild-rust` / `unibuild-clean` | Other isolated compiler modes |
| `make headers_install` | Export `includes/uapi` to `DESTDIR` |
| `make check-env` | Host environment diagnostic (`scripts/deptest.sh`) |

### Utilities

| Target | Purpose |
|--------|---------|
| `make create-disk [FS=minix] [SIZE=200]` | Create a blank disk image |
| `make delete-disk` / `disk-reset` | Remove or reset the local disk |
| `make load-init` | Inject an init binary into a disk |
| `make sync-menuconfig` | Re-sync generated config headers |
| `make sync-menuconfig-defconfig` | Re-sync from `setup/defconfig` |
| `make sync-menuconfig-check` | Verify generated config is current |

## Object list composition

The Makefile builds `ALL_OBJS` from layered fragments:

- **Core:** `kernel/`, `fs/`, `mm/`, `sched/`, `includes/ir0/`
- **Arch:** `arch/x86-64/` or `arch/arm64/` selected by `ARCH`
- **Drivers:** gated by `CONFIG_ENABLE_*` and `CONFIG_INIT_*`
- **Generated:** `version.o`, kconfig headers
- **ARM64:** `scripts/make/arm64.mk` and `scripts/make/arm64-board.mk`

There is no userspace coupling in the object list: the kernel never compiles or
links ISD packages. Userspace images come from the sibling ISD repository and
are attached at runtime as `disk.img`.

Adding a new subsystem requires **four wiring points** (CTR rule):

1. `setup/Kconfig` symbol
2. `setup/defconfig` default
3. `Makefile` object list / `CFLAGS`
4. Optional menuconfig preset in `scripts/kconfig/menuconfig.py`

## Relationship to Documentation

| Doc | Makefile topic |
|-----|----------------|
| `TOOLING.md` | Day-to-day targets and config keys |
| `DECOUPLING.md` | Why arch-guard exists |
| `Documentation/ai_driven_dev/` | Agent rules for Makefile/Kconfig changes |
| `SETUP.md` | First-time bootstrap sequence |
| `USERSPACE.md` | External ISD boundary and manual userspace images |

For the authoritative list of targets, run **`make help`** — it is maintained alongside the Makefile.
