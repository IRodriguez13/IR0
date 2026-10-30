# IR0 Makefile — Central Tooling Orchestrator

The root `Makefile` is the single entry point for building, configuring, testing, and
running the IR0 kernel. Sub-makefiles (`tests/host/Makefile`, `tests/kernel_memsafe/Makefile`)
handle isolated harnesses; everything else flows through the root file.

## Design role

| Concern | Makefile responsibility |
|---------|-------------------------|
| Architecture selection | `ARCH`, `CONFIG_ARCH_*`, cross-compile prefixes |
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
| `ktm.mk` | `ktest`, host harnesses, CTR/KTM gates, test catalog |

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
| `make ir0` | x86-64: `kernel-x64.iso`. ARM64: `kernel-arm64.bin`. Board: `build/arm64-<board>/kernel8.img` |
| `make ir0 arm64` | ARM64 aggregate (`kernel-arm64-all.bin` + common-core/provider gates) |
| `make ir0 arm64 rpi4` | RPi4 `kernel8.img` plus its ELF twin |
| `make all` | Same artifact as `ir0`, without the `-Werror` policy |
| `make kernel-x64.bin` | Linked x86-64 ELF only |
| `make kernel-x64.iso` | Bootable GRUB ISO |
| `make kernel-x64-test.bin` | Kernel built with the in-kernel ktest battery |
| `make clean` | Remove objects, deps, images and generated headers (disk images, `toolchain/`, `dist/`, `out/` preserved) |

`ir0` compiles with `-Werror`; use `make all` or the individual artifact targets
when a third-party compiler introduces a warning you need to inspect first.
When `CONFIG_TOOL_AUTO_RUN_DEPTEST=y`, `ir0` runs `deptest` first.

### Run (QEMU)

| Target | Notes |
|--------|-------|
| `make run` | GUI on x86-64; serial on ARM64 |
| `make run console` | `-nographic` |
| `make run debug` | Guest-error/int log + monitor on telnet `127.0.0.1:1234` |
| `make run fullscreen` | GTK full screen |
| `make run gdb` | Wait for GDB on localhost:1234 |
| `make run arm64 rpi4` | Boot `build/arm64-rpi4/kernel8.img` |

`run` **only launches an artifact that is already on disk**. It never invokes
make, never builds, and never touches ISD. `scripts/run_qemu.sh` resolves the
product from `ARCH`/`BOARD`, refuses to start when the artifact is missing, and
execs QEMU. Useful variables: `IR0_PRODUCT`, `DISK` (`none` opts out),
`IR0_MEM`, `IR0_NET=0`, `IR0_DENNIS_9P=0`, `IR0_AUDIO=1`, `IR0_QEMU_EXTRA`.

On x86-64, `disk.img` is attached automatically when it exists. It is optional;
without it the kernel boots the ISO and reaches the `/sbin/init` handoff, which
requires a userspace disk built outside this tree.

### Tests and gates

| Target | Script / mechanism |
|--------|-------------------|
| `make ktest` / `kernel-tests` | In-kernel ktest battery in QEMU (headless) |
| `make test` | Interactive test TUI (`scripts/test_tui.py`) |
| `make test-list` | List catalog suites |
| `make test-run SUITE=host,ktest` | Run catalog suites non-interactively |
| `make test-fast` | `kernel-x64.bin` + `arch-guard` + host tests + `ktm-check` |
| `make agent-fast` | `test-fast` + `ktest` |
| `make ctr` | `scripts/ctr.sh`: kernel + arch-guard + matrix-min + host tests |
| `make ktm-check` | Host tests + KTM classify/manifest/adoption/portability guards |
| `make build-matrix-min` | Minimal `.config` compile matrix; restores the caller's `.config` on every exit path |
| `make arch-guard` | Architecture boundary checks (`scripts/architecture_guard.py`) |
| `make repo-hygiene-guard` | Repository hygiene (`scripts/repo_hygiene_guard.py`) |
| `make health` | `kernel-analyze` + text budget + memsafe + kernel tests |
| `make tests` | Build the ktest kernel, kernel-memsafe harness and host tests |

The catalog (`scripts/test_catalog.yaml`) only lists targets that still exist.
`make test-run` and the TUI are thin wrappers around that catalog.

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
| `make format` | `clang-format` over C/H sources |
| `make compile-commands` | `compile_commands.json` for LSP |
| `make disasm` | `objdump -d -S kernel-x64.bin` |
| `make stack-usage` | Stack-usage report for kernel objects |
| `make print-build-objects` | Object inventory used by graph audits |
| `make build-graph-audit` | Audit the generated build graph |
| `make unibuild FILE=<file.c>` | Compile translation units in isolation |
| `make unibuild-cpp` / `unibuild-rust` / `unibuild-clean` | Other isolated compiler modes |
| `make headers_install` | Export `includes/uapi` to `DESTDIR` |
| `make check-env` / `deptest` | Host environment diagnostic |

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
