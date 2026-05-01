# Kernel Decoupling Map

This document maps how IR0 separates subsystems, where stable interfaces live, and where
coupling still exists. It complements `DRIVERS.md`, `TOOLING.md`, and the CTR checklist in
`.cursor/skills/ctr/SKILL.md`.

## Goals (design intent)

- **Facades:** Upper layers (especially `fs/` and syscall paths) reach hardware-facing code
  through `includes/ir0/*` rather than including `drivers/*` or `arch/*` directly where
  possible.
- **Callbacks / ops tables:** Drivers and pluggable backends expose behavior through function
  pointers (`ir0_driver_ops_t`, `block_dev_ops_t`, `vfs_ops`, `devfs_ops_t`, bootstrap
  hooks) registered at startup.
- **Config-driven composition:** Optional subsystems are selected via `CONFIG_*` symbols wired
  through `setup/Kconfig`, `setup/defconfig`, `Makefile` object lists, and
  `scripts/kconfig/menuconfig.py`.

“Total decouple” progresses as portable trees route optional services through façades (`serial_io.h`,
`clock.h`). Some subsystems (`main.c` drivers bring-up, `includes/ir0/` thin headers) still include
underlying `drivers/*` via those façades; direct `#include <drivers/serial/...>` outside `drivers/`
/`arch/` is reduced in **`kernel/`**, **`mm/`**, **`net/`** to prefer `includes/ir0/*`.

## Portable architecture boundary (implemented conventions)

| Area | Mechanism |
|------|-----------|
| Syscall ISA hook | **`arch_syscall_init()`** (x86‑64 installs int `0x80` trap to assembly stub in `arch/common/arch_interface.c`); portable [`kernel/syscalls.c`](../../kernel/syscalls.c) does not include `interrupt/arch/idt.h`. |
| Ring‑3 transition | **`arch_switch_to_user(entry, stack)`** in [`arch/x86-64/sources/user_mode.c`](../../arch/x86-64/sources/user_mode.c); scheduler uses [`arch/common/arch_portable.h`](../../arch/common/arch_portable.h) prototypes only. |
| Context switch | callers use **`arch_context_switch()`** ([`includes/ir0/context.h`](../../includes/ir0/context.h)); ISA labels `switch_context_x64` / `switch_context_arm64` remain private to ASM + [`kernel/scheduler/switch/arch_context_switch.c`](../../kernel/scheduler/switch/arch_context_switch.c). |
| **`fs/`** vs `arch/` | No `#include <arch/...>` in `fs/`; use **`includes/ir0/arch_port.h`** (`scripts/architecture_guard.py` enforces). |

### Binary stability check

After building **`kernel-x64.bin`**, a SHA‑256 of sorted globally defined symbols (`nm`) is:

```bash
python3 scripts/kernel_export_digest.py kernel-x64.bin
```

Use this to compare before/after refactors **with the same toolchain**; bytecode-identical reproducible ELF is a stronger (optional) goal.

## Layered overview

```text
  debug_bins / future userspace
           │
           ▼  syscalls only
       kernel/, fs/, mm/, net/
           │
           ├── includes/ir0/*     (preferred public API toward “drivers + services”)
           │
           ├── kernel/*           some files still include drivers/ or arch/ (see leaks)
           │
           ▼
       drivers/, arch/, interrupt/
```

**Policy (CTR):** In `fs/*` and `kernel/syscalls.c`, avoid adding direct
`#include <drivers/...>`. Prefer extending facades under `includes/ir0/`.

## Facade inventory (`includes/ir0/`)

These headers are the intended stable surface for callers that should not depend on a
specific driver implementation file:

| Area | Facade header(s) | Typical consumer |
|------|------------------|------------------|
| CPU / ISA queries for pseudo-fs | `arch_port.h` | **`fs/`** (`/proc` CPU text; wraps `arch_portable.h`) |
| Block I/O | `block_dev.h` | `fs/` (Minix LBA path, VFS helpers) |
| Partitions | `partition.h` | `fs/devfs.c`, proc/sys helpers |
| Console output | `console_backend.h` | `fs/`, sysfs paths |
| Clock / wall time | `clock.h`, `rtc.h` | Time-related code |
| Networking abstractions | `net.h` | Stack integration points |
| Input | `keyboard.h`, `input.h`, backends | `/dev`, console |
| Video | `vga.h`, `video_backend.h` | Diagnostics, optional UI |
| Serial (debug) | `serial_io.h` | Logging façade |
| Syscalls ABI | `syscall*.h`, `copy_user.h` | Arch-specific stubs |
| Driver model | `driver.h`, `driver_bootstrap.h` | Registry and boot order |
| VFS-backed devices | `devfs.h`, `procfs.h`, `sysfs.h` | Virtual filesystems |

Some facades are thin includes of concrete headers (`block_dev.h` →
`drivers/storage/block_dev.h`, `driver_bootstrap.h` → `drivers/driver_bootstrap.h`). Central
definitions still live beside implementations; tightening this would mean splitting “API
structs only” headers from driver `.c` files.

## Registration and callback patterns

| Mechanism | Types / APIs | Purpose |
|-----------|----------------|---------|
| **Driver registry** | `ir0_driver_ops_t`, `ir0_register_driver()` | Lifecycle + generic I/O; see `kernel/driver_registry.c`. |
| **Block devices** | `block_dev_ops_t`, `block_dev_register()` | Sector I/O bound to disk id; ATA registers an implementation in `drivers/storage/ata_block.c`. |
| **Filesystems** | `struct vfs_ops`, `vfs_fstype` | Mountable FS plugins (`minix_fs.c`, `tmpfs.c`, `simplefs.c`, …). |
| **Character devices** | `devfs_ops_t`, `devfs_register_device()` | Per-node read/write/ioctl/open/close in `fs/devfs.c`. |
| **Bootstrap** | `driver_boot_init_fn`, `driver_bootstrap_register()` | Staged early init wired from `drivers/init_drv.c` with `CONFIG_INIT_*`. |
| **Resources** | `resource_register_irq`, `resource_register_ioport` | Naming / tracking from drivers upward via `kernel/resource_registry.[ch]`. |

**Proc/sys:** Many pseudo-files use central `strcmp`/`strncmp` dispatch in `proc_open` /
read paths rather than one global callback table per file. That is workable but duplicates
machinery; tests must keep **open/read** handlers aligned when adding entries.

## Configuration pipeline

1. **`setup/Kconfig`** defines symbols (`ENABLE_*`, `ARCH_*`, `INIT_*`, filesystem toggles).
2. **`.config`** is produced by `make menuconfig` / `make defconfig` (defaults from
   `setup/defconfig`).
3. **`Makefile`** maps `CONFIG_*` to object lists (`*_OBJS`), `CFLAGS` defines, and
   architecture objects.
4. **Guards:** `build-matrix-min`, `build-matrix-full`, `config-wiring-check`,
   `arch-config-check`, and `arch-guard` validate that configs still build together.

Unset or missing `.config` often treats optional features as **enabled** (`ifneq (...),n)`
pattern); treat explicit `n` as the compile-out signal.

## Multi-architecture state (x86-64 vs ARM64)

| Aspect | x86-64 | ARM64 |
|--------|--------|-------|
| **Link rule** | `kernel-x64.bin` links **`$(ALL_OBJS)`** (full kernel). | `kernel-arm64.bin` links **only `$(ARCH_OBJS)`** (scaffold). |
| **Boot** | Multiboot path in `arch/x86-64/asm/boot_x64.asm` into kernel bring-up. | `arch/arm64/sources/boot_stub.c` provides `_start`; minimal stub behavior. |

So **menuconfig toggles primarily affect the full x86-64 image**. ARM64 today is an
architecture **scaffold**, not parity with the x86 kernel link. Stabilizing ARM for real
bring-up implies growing `ALL_OBJS` (or equivalent) for `ARCH=arm64`, syscall/mm/fs
integration, and board-specific drivers (USB stacks would eventually plug in here).

## Known upward coupling (not via `includes/ir0/` alone)

Direct `#include <drivers/...>` or tight `#include <arch/...>` in non-driver trees (grep
baseline; evolves with refactors):

- **`kernel/`:** many modules use **`arch/common/arch_portable.h`** for IRQ/CPUID/faults; high-level
  bring-up may still include specific drivers (`init_drv`, block, video) per `main.c` policy.
- **`mm/`:** use **`ir0/serial_io.h`** for logging where applicable.
- **`net/`:** use **`ir0/serial_io.h`** and **`ir0/clock.h`** for diagnostics and time.
- **`fs/`:** CPU queries go through **`includes/ir0/arch_port.h`** (no direct `<arch/...>` includes).
- **`includes/ir0/`:** thin facades may still include concrete driver headers (`serial_io.h` →
  `drivers/serial/serial.h`) until split API-only headers exist.

Drivers themselves may include **`kernel/resource_registry.h`**, **`kernel/scheduler_api.h`**,
and **`arch/common/*`**—expected for IRQ/port registration on PC-class hardware.

## Assessment: are we on a scalable path?

**Yes, for subsystem composition:** facades + registry + Kconfig/Makefile gating give a
clear pattern for optional drivers (including a future USB host stack) **if** new code wires
through `includes/ir0/*`, `CONFIG_*`, and `Makefile` consistently.

**Gaps vs “total” decoupling:**

1. **`kernel/` and `net/`:** reduce remaining direct `arch_portable` surface where a narrower façade
   helps testing (optional split of “CPU info” vs “I/O port” APIs).
2. **Facades that include drivers:** optionally split **API-only** headers from `.c`.
3. **ARM64:** `kernel-arm64.bin` still links **only** `$(ARCH_OBJS)`; full parity with the x86 image
   requires linking portable `$(ALL_OBJS)` once MMU/syscall/fault paths are complete—no change to
   this document until that wiring lands.

## Testing and menuconfig ergonomics

- **Unit / harness:** ops tables (`block_dev_*`, vfs, devfs) are natural seam for fake
  backends; keep tests on the façade side where possible.
- **Integration:** rely on `build-matrix-*`, `runtime-*` checks from the Makefile as
  exercised in CTR.
- **Menuconfig:** user-chosen profiles work best when each `CONFIG_*` has one Makefile owner
  and avoids “silent default on when unset”; document quirks in `TOOLING.md` if behavior
  changes.

## Related files

| File | Role |
|------|------|
| `includes/ir0/driver.h` | `ir0_driver_ops_t` and registry API |
| `drivers/driver_bootstrap.c` | Bootstrap callback list |
| `drivers/storage/block_dev.h` | `block_dev_ops_t` |
| `fs/vfs.h` | `vfs_ops` / fstype registration |
| `includes/ir0/arch_port.h` | Arch-neutral CPU queries for `fs/` |
| `scripts/architecture_guard.py` | Driver + interrupt + `fs` arch include policy |
| `scripts/kernel_export_digest.py` | SHA-256 of sorted `nm -g` export list |
| `Makefile` | `ALL_OBJS`, `ARCH_OBJS`, gated `*_OBJS` |

---

*Document generated from codebase review (facades, registries, Makefile, Kconfig). Update when
major refactors change include boundaries.*
