# IR0 Setup and Bootstrap

This document covers reproducible kernel builds, ISO/board image creation, and
QEMU execution. It intentionally excludes smoke harnesses and internal phase
naming.

For architecture and capability boundaries, see [README.md](README.md).
For the kernel ↔ userspace boundary, see
[Documentation/USERSPACE.md](Documentation/USERSPACE.md).

## Prerequisites

### Required for `make ir0` (x86-64 kernel ISO + QEMU)

| Tool | Purpose |
|------|---------|
| `gcc`, `g++`, `ld` | Kernel C/C++ compile and ELF link |
| `nasm` | x86-64 assembly |
| `make` | Build orchestration |
| `python3` (+ curses) | menuconfig / Kconfig helpers |
| `grub-mkrescue` + `xorriso` | ISO generation (`kernel-x64.iso`) |
| `qemu-system-x86_64` | Execution |

Debian/Ubuntu example:

```bash
sudo apt install build-essential nasm qemu-system-x86 grub-pc-bin xorriso python3
```

Rust (`rustc` / `cargo` / nightly `rust-src` via [rustup](https://rustup.rs)) is
**optional**. It is required only when `.config` has
`CONFIG_ENABLE_EXAMPLE_DRIVERS=y` (off in `make defconfig`).

### Required for ARM64 (`make ir0 arm64`, board images)

| Tool | Purpose |
|------|---------|
| `aarch64-linux-gnu-gcc` / `ld` | Freestanding ARM64 images |
| `qemu-system-aarch64` | QEMU `virt` and Raspberry Pi machines |

```bash
sudo apt install gcc-aarch64-linux-gnu binutils-aarch64-linux-gnu qemu-system-arm
```

Optional AArch64 musl cross for the hello payload:

```bash
./scripts/setup_musl_aarch64.sh
```

The host diagnostic covers both architectures:

```bash
make check-env                    # x86-64 desktop profile
make check-env PROFILE=hub-rpi4   # ARM64 RPi4 UART profile
make check-env PROFILE=all        # union of the profiles
```

`check-env` / `deptest` classify missing tools as **required**, **optional**,
**unsupported_version**, or **present_but_unusable**, with apt/pacman/dnf
install hints. Exit 0 only when every required check for that profile passed.

### Userspace images (optional, built outside this tree)

The kernel Makefile does not build or clone userspace. If you want a shell,
X11, BusyBox, or Doom, build the sibling [ISD](https://github.com/IRodriguez13/ISD)
distribution with its own tooling, then attach its `disk.img` at runtime.

Host tools used by an external ISD build:

| Tool | Purpose |
|------|---------|
| `musl-gcc` / `x86_64-linux-musl-gcc` | Static musl binaries (BusyBox, runit, …) |
| `git`, `curl`, `patch`, `sha256sum`, `file`, `flock` | Fetch/verify/build packages |
| `yacc` (from `bison`) | Build opendoas (`parse.y`) |

```bash
sudo apt install musl-tools git curl patch file util-linux bison
```

`make check-env PROFILE=userspace` checks this toolchain.

### WSL (Windows Subsystem for Linux)

WSL2 is supported for build + QEMU:

| Topic | Guidance |
|-------|----------|
| Distro | Ubuntu/Debian x86_64 under WSL2 (not WSL1) |
| Tree location | Keep `IR0/` (and an optional sibling `ISD/`) on the Linux filesystem (`~/…`), not `/mnt/c` |
| KVM | Optional. Without `/dev/kvm`, QEMU uses TCG (slower, OK). Nested KVM needs Windows 11 + `nestedVirtualization=true` in `%UserProfile%\.wslconfig`, then `wsl --shutdown` |
| GUI | WSLg or an X server; if GTK fails, use `make run console` |
| Windows cross builds | Not supported. Use WSL; there is no MinGW path |

### Man pages (subsystem docs)

```bash
make sync-mandocs          # install → ~/.local/share/man (no sudo)
make man TOPIC=onboarding  # first clone → first bug
make man TOPIC=boot
make man TOPIC=multi-arch
```

Interactive / one language: `make mandocs`, `make mandocs-en`, `make mandocs-es`.
See also `make help-docs`.

## First-Time Configuration

Copy the default Kconfig snapshot:

```bash
make defconfig
```

This installs `setup/defconfig` as `.config` and regenerates `config.h`.

Default highlights:

- x86-64, MINIX root on `hda`, round-robin scheduler
- `/sbin/init` handoff from an attached userspace disk
- VBE framebuffer enabled (`CONFIG_ENABLE_VBE=y`)

Interactive configuration:

```bash
make menuconfig       # curses TUI
make menuconfig-en    # force English
make menuconfig-es    # force Spanish
```

If `.config` is missing, the Makefile still builds with bring-up defaults; run
`make defconfig` for a reproducible baseline.

## Basic Kernel Build

x86-64:

```bash
make ir0               # kernel-x64.iso (default artifact)
make kernel-x64.bin    # linked ELF only
make kernel-x64.iso    # bootable GRUB ISO
```

ARM64:

```bash
make ir0 arm64             # kernel-arm64.bin (QEMU virt aggregate)
make ir0 arm64 rpi4        # kernel-arm64-rpi4-min.bin + build/arm64-rpi4/kernel8.img
make ir0 arm64 rpi3        # RPi3 image
make ir0 arm64 rpi5        # RPi5 image
```

Artifacts:

- `kernel-x64.bin` — x86-64 kernel ELF
- `kernel-x64.iso` — bootable ISO
- `kernel-arm64.bin` — validated ARM64 aggregate for QEMU `virt`
- `kernel-arm64-<board>-min.bin` — ELF twin for a board
- `build/arm64-<board>/kernel8.img` — raw Raspberry Pi firmware image
- `disk.img` — optional userspace disk, created only when you ask for it

`make ir0` adds `-Werror`; `make all` builds the same artifact without that
policy. Clean with `make clean` (disk images and toolchain caches are preserved).

## Running in QEMU

`make run` **only launches an artifact that is already on disk**. It never
builds and never invokes ISD. Build first, then run:

```bash
make ir0
make run                    # GTK window + serial
```

| Command | Behavior |
|---------|----------|
| `make run` | GUI on x86-64; serial on ARM64 |
| `make run console` | `-nographic` |
| `make run debug` | Guest-error/int log + monitor on telnet `127.0.0.1:1234` |
| `make run fullscreen` | GTK full screen |
| `make run gdb` | Wait for GDB on `localhost:1234` |
| `make run arm64` | QEMU `virt`, PL011 serial |
| `make run arm64 rpi4` | QEMU `raspi4b` with `build/arm64-rpi4/kernel8.img` |

Runtime variables:

| Variable | Meaning |
|----------|---------|
| `DISK=path` | Attach a raw userspace disk (x86-64) |
| `DISK=none` | Never attach a disk, even if `disk.img` exists |
| `IR0_PRODUCT=path` | Boot an explicit artifact |
| `IR0_MEM=1G` | Guest memory (default 512M x86, 1G ARM64) |
| `IR0_NET=0` | Disable the default user-mode NIC |
| `IR0_DENNIS_9P=0` | Disable the host-tree 9p share |
| `IR0_AUDIO=1` | Enable SB16/AdLib (x86 only) |
| `IR0_QEMU_EXTRA="…"` | Extra QEMU arguments appended verbatim |

The launcher (`scripts/run_qemu.sh`) is POSIX `sh` and can be called directly
from any shell.

### Attaching a userspace disk

```bash
make run DISK=../ISD/out/x86_64/images/minimal/disk.img
```

The x86-64 ISO boots the kernel; `disk.img` provides `/sbin/init` and the
rootfs. Without a disk the kernel panics at the `/sbin/init` handoff — there is
no in-kernel fallback shell.

`make create-disk` creates a blank MINIX image. `make load-init` injects a
single init binary into it. Full rootfs packing belongs to ISD.

## Serial Logging

Kernel event records go to COM1, the boot console, `/proc/kmsg`, and
`/dev/kmsg`. Records always carry sequence + boot phase; time remains `?.???`
until the monotonic clock is online.

| Command | Serial behavior |
|---------|-----------------|
| `make run console` | `-serial stdio` (`-nographic`) |
| `make run debug` | `-serial stdio` + guest error/int logging to `qemu_debug.log` |
| `make run` | `-serial stdio` alongside the GTK window |

To capture a boot log without QEMU stdio, redirect the host shell:

```bash
make run console > boot.log 2>&1
```

## Essential Make Targets

| Target | Description |
|--------|-------------|
| `defconfig` | Install default `.config` |
| `menuconfig` | Curses configuration TUI |
| `check-env` / `deptest` | Check build dependencies |
| `ir0` | Default artifact for `ARCH`/`BOARD` |
| `kernel-x64.bin` | Build kernel ELF |
| `kernel-x64.iso` | Build boot ISO |
| `run` | QEMU with an already-built artifact |
| `ktest` / `kernel-tests` | In-kernel ktest battery in QEMU |
| `test` | Interactive test TUI |
| `ctr` | Kernel + arch-guard + matrix-min + host tests |
| `arch-guard` | Architecture boundary guard |
| `build-matrix-min` | Minimal `.config` compile matrix |
| `create-disk` | Create a blank disk image |
| `load-init` | Inject an init binary into a disk |
| `clean` | Remove build artifacts |

## Console and Framebuffer Notes

With `CONFIG_ENABLE_VBE=y` (default):

- GRUB sets a linear framebuffer mode (default **1280×800×32** in `arch/x86-64/grub.cfg`).
- Early kernel text uses VGA at `0xB8000` or the FB path when VBE info is available.
- Logical console size is **80 columns × 25 rows**; glyphs are scaled **2×** on the framebuffer and centered (letterbox).
- Default character attribute is VT-style **0x07** (light gray on black).
- `console_renderer.c` interprets a **subset** of ANSI SGR sequences for foreground/background color.
- TTY echo and userspace writes share one cursor model through the console backend.
- Userspace full-screen clients use **`/dev/fb0`** (mmap) and **`/dev/events0`** for input where enabled.

No additional visual tuning is documented here; this section describes current behavior only.

## Troubleshooting

| Symptom | Likely cause |
|---------|----------------|
| `✗ run: product not built` | Run `make ir0` (or `make ir0 arm64 …`) first |
| `grub-mkrescue` missing | Install `grub-pc-bin` / `grub2-common` and `xorriso` |
| `/sbin/init` not found at boot | No disk attached; pass `DISK=…` or run `make load-init` |
| Blank GUI, serial OK | QEMU display focus, or the guest has no graphical userspace |
| `qemu-system-aarch64` missing | Install `qemu-system-arm` (provides aarch64) |
| ARM64 board machine missing | Upgrade QEMU; `raspi5b` needs a recent release |
| `musl cross compiler not found` | Install `musl-tools` or set `MUSL_CC` (external userspace builds) |

## See Also

- [README.md](README.md) — project state and architecture
- [Documentation/README.md](Documentation/README.md) — subsystem documentation index
- [Documentation/MAKEFILE.md](Documentation/MAKEFILE.md) — target taxonomy and build graph
- [Documentation/mandocs/en/INDEX.md](Documentation/mandocs/en/INDEX.md) — internals mandocs (`make mandocs-en`, then `man IR0-vfs`)
- [LICENSE](LICENSE) — GPL v3
