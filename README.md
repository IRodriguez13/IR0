# IR0 Kernel

IR0 is a monolithic x86-64 operating system kernel under active development. The tree is organized around narrow interfaces between subsystems, Kconfig-driven feature selection, and deterministic serial diagnostics. It is a research and bring-up codebase, not a general-purpose production OS.

Approximate size of in-tree kernel sources (C, assembly, and Python build tooling), excluding vendored BusyBox and Doom upstream trees: **~61,000 lines** across **~280 files** (measured with `cloc` on the current tree).

For build steps, QEMU usage, and userspace bootstrap, see **[SETUP.md](SETUP.md)**.

Further subsystem notes live under **[Documentation/](Documentation/README.md)**.

## Introduction

IR0 targets **x86-64** with Multiboot boot, GRUB, ELF64 userspace binaries, and ring 0 / ring 3 separation. The default configuration builds a single linked kernel image (`kernel-x64.bin`) with optional ISO packaging.

**Goals:**

- Monolithic kernel with explicit facades (`includes/ir0/*`) between portable code and arch/drivers
- Incremental POSIX/Linux-ABI compatibility for musl-linked userspace (partial, not complete)
- Repeatable QEMU bring-up with serial logging
- VFS-backed root on MINIX with block storage via ATA

**Non-goals (today):** SMP scheduling, a complete Linux syscall surface, TCP sockets, or desktop-class userspace.

## Current State

The following areas are implemented and used in regular builds; maturity varies (see [Limitations](#limitations)).

| Area | Status (honest) |
|------|-----------------|
| **Boot / arch** | x86-64 Multiboot, GRUB ISO, identity-mapped paging with NX |
| **Scheduler** | Round-robin (`CONFIG_SCHEDULER_POLICY=0`); CFS symbol exists but aliases RR |
| **Processes** | `fork`, `execve`, `wait4`, `exit`; address-space copy on fork (no COW) |
| **ELF userspace** | ET_EXEC/ET_DYN load for x86-64; userspace init path via `kernel-x64-userspace.iso` |
| **Syscalls** | ~76 wired Linux x86-64 numbers of 450 slots; remainder return `-ENOSYS` |
| **Memory** | `brk`, `mmap`/`munmap`/`mprotect`, demand paging; `/dev/fb0` mmap handled specially |
| **VFS** | MINIX, tmpfs, simplefs, FAT16 registration; path-routed `/proc` and `/sys` |
| **devfs** | Static device nodes including `/dev/console`, `/dev/fb0`, `/dev/events0`, `/dev/tty*` |
| **Framebuffer** | VBE linear FB when `CONFIG_ENABLE_VBE=y`; unified VGA + FB text console |
| **Console / TTY** | Canonical line discipline, PS/2 input, scaled FB renderer (80×25 logical) |
| **Networking** | RTL8139 driver; IPv4, ARP, ICMP, UDP, DHCP, DNS client — **no TCP**, **no socket syscalls** |
| **Shell (kernel)** | In-kernel debug shell when `CONFIG_KERNEL_DEBUG_SHELL=y` (default in `defconfig`) |
| **Shell (userspace)** | BusyBox `ash` via `irinit` on `/dev/console` (see SETUP.md) |
| **BusyBox** | Vendored 1.36.1; rebuilt locally from `setup/busybox/*.config` (not committed as binaries) |
| **TCC** | Optional musl-static TinyCC built by `setup/tcc/build-fase52.sh` |
| **Doom** | doomgeneric port + `/dev/fb0` / input path; requires external IWAD |
| **Signals** | Basic `rt_sigaction`, `kill`, delivery hook; incomplete flags/semantics |
| **Permissions** | UID/GID syscalls and simple `access`; not a full Linux credential model |

ARM64 exists as **Makefile/Kconfig scaffold only** (`kernel-arm64.bin` links arch stubs, not the full kernel).

## Design Philosophy

IR0 favors **narrow, testable boundaries** over implicit coupling:

- **Facades** — Portable code calls `includes/ir0/*` and VFS ops, not driver headers directly (`fs/`, `mm/` avoid `#include <drivers/...>`).
- **Config-driven selection** — Filesystems, drivers, and scheduler policy come from `setup/Kconfig` / `setup/defconfig`, reflected in `config.h`.
- **Regression isolation** — Subsystems expose callbacks or tables (VFS `vfs_fstype`, pseudo-fs registry, driver registry) so bring-up can enable one layer at a time.
- **Deterministic debug** — Serial (`printk`, tagged boot lines) is the primary observability path; many paths log explicit markers for automated checks.
- **Honest partial POSIX** — Linux syscall numbers and struct layouts are reused where implemented; unstubs return `-ENOSYS` rather than silent success.

## Architecture Overview

```
                    +------------------+
                    |  Userspace (ring 3) |
                    |  musl / BusyBox /   |
                    |  doomgeneric / TCC  |
                    +----------+---------+
                               | SYSCALL / int 0x80 / MSR LSTAR
                    +----------v---------+
                    |  syscall dispatch   |  kernel/syscalls.c (+ split modules)
                    +----------+---------+
           +--------+----------+---------+--------+
           |        |          |         |        |
      +----v---+ +--v--+ +-----v----+ +---v---+ +--v---+
      | sched  | | mm  | | VFS/devfs | | proc  | | net  |
      | rr_sched| |paging| | minix/tmpfs | | elf   | | udp  |
      +----+---+ +--+--+ +-----+----+ +---+---+ +--+---+
           |        |          |         |        |
      +----v--------v----------v---------v--------v---+
      |  drivers: video, ATA, PS/2, RTL8139, serial, ... |
      +--------------------+---------------------------+
                           | IDT / IRQ / timers
                      +----v----+
                      |  x86-64 |
                      +---------+
```

### Scheduler

Timer-driven round-robin in `sched/rr_sched.c`, facade in `sched/scheduler_api.c`. Single runqueue; not SMP-safe today.

### Memory

Physical allocator, kernel heap, per-process page tables (`mm/paging.c`), user copy helpers with explicit page directory. Fork duplicates mappings (copy, not COW).

### VFS and pseudo filesystems

- **Block filesystems** — MINIX v1 root (default), tmpfs, simplefs engine, FAT16 hook (`fs/vfs.c` registration).
- **devfs** — Device nodes and file operations for console, framebuffer, input, block devices (`fs/devfs.c`).
- **procfs / sysfs** — Path-routed pseudo-fs with registry (`fs/pseudo_fs_registry.c`, `includes/ir0/path_routed.c`); not a separate mountable fstype.

### Drivers

ATA/IDE block, PS/2 keyboard and mouse, VBE framebuffer, VGA text, RTL8139, serial, PC speaker, Sound Blaster (config-gated). USB host and example C++/Rust drivers are off in default `defconfig`.

### Framebuffer and console

When VBE is available, `drivers/video/console.c` renders a **80×25 logical** text grid with **2× glyph scaling** on the framebuffer, centered in the mode. `drivers/video/console_renderer.c` handles scrolling, cursor inversion, and a **subset of ANSI SGR** colors. Kernel diagnostics can mirror to serial; userspace draws via `/dev/fb0` (mmap) and reads input from `/dev/events0` where enabled.

Default GRUB mode in `arch/x86-64/grub.cfg` is **1280×800×32** when VBE is enabled.

### Userspace

- **Kernel debug shell** — Default boot (`CONFIG_KERNEL_DEBUG_SHELL=y`): ring-3-like commands implemented inside the kernel for driver and VFS testing (`debug_bins/`).
- **Real init** — `kernel-x64-userspace.iso` sets `IR0_USERSPACE_INIT_BOOT=1` and expects `/sbin/init` on the MINIX disk image.
- **irinit** — PID1 harness spawning BusyBox `ash` on `/dev/console` (`setup/pid1/irinit.c`).

### Syscalls

Dual entry on x86-64: **INT 0x80** (legacy/debug ABI) and **SYSCALL** MSR path (Linux/musl layout). Dispatch table in `kernel/syscalls.c` with split for filesystem ops (`kernel/syscalls/fs_syscalls.c`).

## Supported Toolchain and Software

Validated in-tree (requires musl cross compiler for static userspace builds):

| Component | Role | Build / config |
|-----------|------|----------------|
| **BusyBox 1.36.1** | `ash`, coreutils subset | `make build-busybox-fase50-min`; configs under `setup/busybox/` |
| **irinit** | PID1 launcher | `make build-irinit` → `setup/pid1/sbin/irinit` |
| **musl** | Static userspace ABI | `x86_64-linux-musl-gcc` or `musl-gcc` |
| **TinyCC (optional)** | In-guest compiler smoke | `make build-tcc-fase52` |
| **doomgeneric** | FB + input demo | `make build-fase55e-doom-interactive`; upstream under `setup/doom/upstream/` |

Kernel itself builds with **GCC**, **NASM**, **GNU ld**, **Python 3**, **GRUB** (`grub-mkrescue`), and **QEMU**.

## Limitations

This list is intentional scope documentation, not a backlog promise:

- **Uniprocessor** — `CONFIG_ENABLE_SMP=n`; no AP bring-up, IPIs, or per-CPU runqueues.
- **Syscall coverage** — Majority of Linux x86-64 syscall numbers unimplemented; **all socket-related calls** are `-ENOSYS`.
- **TCP / TLS** — No TCP in `net/`; UDP/ICMP only.
- **VM** — No hypervisor; `mmap`/`brk` are process VM only.
- **Fork** — Full address-space copy; no copy-on-write.
- **Threads** — `clone(CLONE_THREAD)` returns `-ENOSYS`.
- **Signals** — Delivery exists; many `sigaction` flags and edge cases are incomplete.
- **Credentials** — Partial UID/GID and `access`; not Linux-grade security model.
- **Permissions / capabilities** — No real capability set; `sudo_auth` is IR0-specific.
- **Userspace maturity** — BusyBox shell is bring-up quality; libc coverage depends on implemented syscalls.
- **Bluetooth** — HCI/UART experimental; not a stable desktop feature.
- **ARM64** — Scaffold only.
- **C++/Rust drivers** — Example drivers disabled in default config.

See also [Documentation/UNIX_DIFFERENCES.md](Documentation/UNIX_DIFFERENCES.md) for ABI and semantic gaps.

## Roadmap (near term)

Ordered by current development focus; items are incremental, not commitments:

1. Expand userspace syscall surface needed by musl/BusyBox without growing monolithic coupling.
2. Improve TTY/console correctness (canonical mode, winsize, FB handoff to userspace).
3. Harden VFS path operations and pseudo-fs registry consistency.
4. Network: socket layer design before any TCP claim.
5. Memory: reduce fork cost (COW investigation) only after correctness gates hold.

## License

IR0 kernel sources are licensed under the **GNU General Public License v3.0**. See [LICENSE](LICENSE).

Third-party trees (BusyBox, doomgeneric, etc.) carry their own licenses under `setup/third-party/` and `setup/doom/upstream/`.
