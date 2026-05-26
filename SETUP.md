# IR0 Setup and Bootstrap

This document covers reproducible build, ISO creation, QEMU execution, and minimal userspace bring-up. It intentionally excludes smoke harnesses, regression targets, and internal phase naming.

For architecture and capability boundaries, see [README.md](README.md).

## Prerequisites

### Required (kernel ISO + QEMU)

| Tool | Purpose |
|------|---------|
| `gcc`, `ld` | Kernel C compilation and ELF link |
| `nasm` | x86-64 assembly |
| `make` | Build orchestration |
| `python3` | Kconfig helpers, MINIX disk injection |
| `qemu-system-x86_64` | Execution |
| `grub-mkrescue` | ISO generation |

Debian/Ubuntu example:

```bash
sudo apt install build-essential nasm qemu-system-x86 grub-pc-bin python3
```

### Required for static userspace (BusyBox, irinit, Doom)

| Tool | Purpose |
|------|---------|
| `x86_64-linux-musl-gcc` or `musl-gcc` | Static musl binaries |

```bash
sudo apt install musl-tools
# or set MUSL_CC=/path/to/x86_64-linux-musl-gcc
```

### Optional

- `git` — cloning TinyCC during TCC build
- `sudo` — only if using host-mount disk scripts (`scripts/load_init.sh`); not required for `inject_init_minix.py`

Verify tools:

```bash
make deptest
```

## First-Time Configuration

Copy the default Kconfig snapshot:

```bash
make defconfig
```

This installs `setup/defconfig` as `.config` and regenerates `config.h`.

Default highlights:

- x86-64, MINIX root on `hda`, round-robin scheduler
- In-kernel debug shell enabled (`CONFIG_KERNEL_DEBUG_SHELL=y`)
- VBE framebuffer enabled (`CONFIG_ENABLE_VBE=y`)

To boot real `/sbin/init` instead of the kernel shell, use the **userspace ISO** build below (separate kernel binary with `IR0_USERSPACE_INIT_BOOT=1`).

## Basic Kernel Build

```bash
make kernel-x64.bin    # linked ELF kernel
make kernel-x64.iso    # GRUB rescue ISO with kernel-x64.bin
```

Default aggregate target (ISO on x86-64):

```bash
make ir0
```

Artifacts:

- `kernel-x64.bin` — kernel ELF
- `kernel-x64.iso` — bootable ISO
- `disk.img` — created on demand by run targets (200 MB MINIX by default)

Clean:

```bash
make clean
```

## Running in QEMU

### Graphical run (kernel debug shell default)

```bash
make run
```

Uses `kernel-x64.iso`, attaches `disk.img`, enables the standard IR0 hardware profile (RTL8139, ATA, PS/2, VGA/VBE, serial).

### Serial-only (no GUI)

```bash
make run-console
```

Equivalent to nographic mode with serial attached — useful when framebuffer is irrelevant.

### Serial debug to terminal

```bash
make run-debug
```

Guest serial and debug events go to the invoking terminal; QEMU monitor on telnet `127.0.0.1:1234`.

### Without disk

```bash
make run-nodisk
```

### Network (optional, host setup required)

```bash
make run-tap    # needs root + configured bridge/TAP
make run-ping   # TAP + scripted ping check
```

Requires TUN/TAP and bridge configuration on the host (see comments in `Makefile` near `run-tap`).

## Serial Logging

Kernel messages go to the COM1 serial port configured in QEMU flags.

| Target | Serial behavior |
|--------|-----------------|
| `make run-console` | `-serial stdio` (console) |
| `make run-debug` | `-serial stdio` + guest error/int logging |
| `make run` | Optional `qemu_debug.log` if writable in cwd |

For userspace bring-up, redirect serial to a file by running QEMU manually with `-serial file:boot.log` after following the disk layout steps below.

## Userspace Boot Path

Real ring-3 init requires:

1. Userspace-enabled kernel ISO
2. MINIX disk with `/sbin/init` and supporting binaries
3. QEMU with ISO + disk

### 1. Build userspace kernel ISO

```bash
make kernel-x64-userspace.iso
```

This produces a kernel built with userspace init boot (`USERSPACE_INIT_BUILD=1`) packaged as `kernel-x64-userspace.iso`. The default `kernel-x64.bin` in the repo root is restored after this step.

### 2. Create and populate disk image

Create a blank MINIX image:

```bash
python3 scripts/inject_init_minix.py --format disk.img
```

For larger rootfs layouts:

```bash
python3 scripts/inject_init_minix.py --format-large disk.img
```

Inject files (destination path is relative to root, no leading slash):

```bash
python3 scripts/inject_init_minix.py disk.img LOCAL_FILE path/on/disk
```

## Minimal BusyBox Userspace

BusyBox is **not** shipped as a committed binary; build from vendored sources:

```bash
make build-busybox-fase50-min
```

Output: `setup/pid1/fase50_busybox_real` (static musl ELF). Configuration fragment: `setup/busybox/fase58_busybox.config`.

Build the PID1 launcher:

```bash
make build-irinit
```

Output: `setup/pid1/sbin/irinit`.

Example rootfs layout for interactive `ash`:

```bash
DISK=disk.img
python3 scripts/inject_init_minix.py --format-large "$DISK"
python3 scripts/inject_init_minix.py "$DISK" setup/pid1/sbin/irinit sbin/init
python3 scripts/inject_init_minix.py "$DISK" setup/pid1/fase50_busybox_real bin/busybox
python3 scripts/inject_init_minix.py "$DISK" setup/pid1/fase50_busybox_real bin/sh
python3 scripts/verify_minix_rootfs.py "$DISK" /sbin/init /bin/sh /bin/busybox
```

Run:

```bash
qemu-system-x86_64 \
  -cdrom kernel-x64-userspace.iso \
  -drive file="$DISK",format=raw,if=ide,index=0 \
  -m 256M -no-reboot -net none \
  -display gtk -serial stdio
```

`irinit` attaches `/dev/console`, disables Doom autostart, and execs BusyBox `ash`. Type in the QEMU window (keyboard focus required).

Extended applet set (optional):

```bash
make build-busybox-fase58-full   # uses setup/busybox/fase58_full.config
```

## Doom (optional)

Build the userspace doomgeneric binary:

```bash
make build-fase55e-doom-interactive
```

Provide a legal IWAD (e.g. `DOOM1.WAD`). Set path when injecting:

```bash
export REAL_WAD_PATH=/path/to/DOOM1.WAD
python3 scripts/inject_init_minix.py "$DISK" setup/pid1/fase55e_doom_interactive bin/doomgeneric
python3 scripts/inject_init_minix.py "$DISK" "$REAL_WAD_PATH" usr/share/doom/doom1.wad
```

From `ash`:

```text
doomgeneric /usr/share/doom/doom1.wad
```

Requires `/dev/fb0` and input devices as implemented in devfs. Without a WAD, skip these inject steps.

## TinyCC (optional)

```bash
make build-tcc-fase52
```

Clones/builds TinyCC via `setup/tcc/build-fase52.sh` (default output under `/tmp/tinycc-fase52`). Used for in-guest compilation experiments; not required for basic shell bring-up.

## Essential Make Targets

| Target | Description |
|--------|-------------|
| `defconfig` | Install default `.config` |
| `deptest` | Check build dependencies |
| `kernel-x64.bin` | Build kernel ELF |
| `kernel-x64.iso` | Build boot ISO |
| `kernel-x64-userspace.iso` | ISO booting `/sbin/init` path |
| `disk.img` / `create-disk` | Create MINIX disk image |
| `run` | QEMU GUI + disk |
| `run-console` | QEMU nographic + serial |
| `run-debug` | QEMU GUI + serial debug |
| `build-irinit` | Build `/sbin/init` harness |
| `build-busybox-fase50-min` | Build static BusyBox |
| `build-busybox-fase58-full` | BusyBox with extended applets |
| `build-fase55e-doom-interactive` | Build doomgeneric binary |
| `build-tcc-fase52` | Build TinyCC (optional) |
| `clean` | Remove build artifacts |

Configuration menu (optional):

```bash
make menuconfig
```

Requires Python 3 with tkinter for the graphical front-end.

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
| `musl cross compiler not found` | Install `musl-tools` or set `MUSL_CC` |
| `grub-mkrescue` missing | Install `grub-pc-bin` / `grub2-common` |
| `/sbin/init` not found at boot | Disk missing init; use userspace ISO + inject steps |
| Blank GUI, serial OK | QEMU display focus; or booted kernel shell ISO instead of userspace ISO |
| BusyBox missing applets | Rebuild with `build-busybox-fase58-full` or adjust `setup/busybox/*.config` |

## See Also

- [README.md](README.md) — project state and architecture
- [Documentation/README.md](Documentation/README.md) — subsystem documentation index
- [Documentation/mandocs/en/INDEX.md](Documentation/mandocs/en/INDEX.md) — internals mandocs (`make mandocs-en`, then `man IR0-vfs`)
- [LICENSE](LICENSE) — GPL v3
