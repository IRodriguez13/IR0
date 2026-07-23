# IR0 Multi-Architecture Support

| Field | Value |
|-------|-------|
| Version | 0.3 |
| IR0 phase | T0 |
| Status | stable |
| Depends on | boot, syscalls, scheduler |
| Man page | IR0-multi-arch (section 7) |
| Primary sources | `arch/common/arch_interface.c`, `arch/x86-64/`, `arch/arm64/sources/` (`boot_stub.c`, `board.c`, `mmu_early.c`, `switch_early.S`, `process_early.c`, `first_switch.c`), `includes/ir0/arm64_board.h`, `sched/switch/switch_arm64.c`, `scripts/architecture_guard.py`, `includes/ir0/arch_port.h` |

## 1. Overview

IR0 separates portable kernel code from architecture backends under `arch/`.
**x86-64** is the production target (ISO, musl userspace, full syscall path).
**arm64** freestanding QEMU virt boot covers MMU identity map, VBAR/SVC EL1,
EL0 drop, GIC/timer scaffold, early context switch (**F7h**), dual TTBR roots
(**F7i**), and freestanding process+TTBR switch (**F7j**). Full `process_t`
fork/exec, musl aarch64, and `ALL_OBJS` product link remain **BLOCKED**
(toolchain / interrupt wall).

Boot CFLAGS include `-mgeneral-regs-only` so early EL1 code does not trap on
NEON.

## 2. Internal architecture

| Layer | Location | Role |
|-------|----------|------|
| Portable facade | `includes/ir0/arch_port.h` | CPU queries, IRQ enable, port I/O facade |
| Common interface | `arch/common/arch_interface.c` | Cross-arch dispatch |
| First switch | `first_switch_to` | x86 `user_mode.c`; ARM `first_switch.c` |
| x86-64 | `arch/x86-64/` | boot, IDT, PIC, user mode, syscalls |
| arm64 | `arch/arm64/sources/` | boot_stub, mmu_early, vectors, switch_early, process_early |
| Context switch | `sched/switch/switch_x64.asm`, `switch_arm64.c` | per-ISA; ARM calls `arm64_cpu_switch_mm` |
| Config | `setup/Kconfig`, `ARCH=` in Makefile | `ARCH_OBJS_ARM64` includes `switch_early.S` |

**Guard tags (`architecture_guard.py`):** portable trees must not include
`<arch/...>` or `<drivers/...>` directly; use facades.

## 3. Data flow

**x86-64 syscall path:**

```text
  musl → syscall insn → syscall_insn_entry_64.asm
       → syscall_dispatch
       → handler → sysret

  debug_bins → int 0x80 → syscall_entry_64.asm → dispatch
```

**Context switch (product x86):**

```text
  sched_schedule_next → first_switch_to(next)   # first entry
                     → switch_to(prev, next)    # later
```

**arm64 freestanding (current smoke path):**

```text
  _start → BOOT_OK → MMU_OK → VBAR → SVC / EL0 tags
        → F7h switch_early (callee-saved)
        → F7i dual TTBR (l1_table / l1_table_b) → ARM64_TTBR_*_OK
        → F7j process_early: arm64_cpu_switch_mm A↔B
             → ARM64_PROCESS_SWITCH_OK / ARM64_PROCESS_TTBR_OK
        → PSCI SYSTEM_OFF
```

## 4. Responsibilities

- `arch/` implements hooks declared in `arch_portable.h` / `arch_port.h`.
- Portable code selects behavior via `CONFIG_*` and facades, not `#ifdef` in `fs/`.
- Makefile gates object lists per `ARCH=x86-64|arm64`.
- Freestanding ARM smokes must not claim product `process_t` / musl readiness.

## 5. Subsystem boundaries

| Rule | Scope |
|------|-------|
| `fs-no-direct-arch` | `fs/` → `ir0/arch_port.h` only |
| `mm-net-no-arch-include` | `mm/`, `net/` → facades |
| `kernel-use-arch-port-facade` | no direct `arch_portable.h` in kernel |
| `drivers-no-arch` | drivers → `ir0/arch_port.h` |

## 6. Relations to other subsystems

| Neighbor | Interaction |
|----------|---------------|
| Scheduler | per-arch switch; first entry via `first_switch_to` |
| Syscalls | per-arch entry ASM / stub |
| Interrupts | PIC (x86) vs GIC scaffold (arm64) |
| Drivers | port I/O via facade; keyboard x86-only blocks |
| VFS/procfs | CPU info via `arch_port` |

## 7. Visual maps

```text
  portable kernel (fs/mm/net/kernel)
           │
           ▼
    includes/ir0/arch_port.h
           │
     ┌─────┴─────┐
     ▼           ▼
  x86-64      arm64
  (full)      (freestanding + probe)
     │           │
  boot/syscall  boot_stub + switch_early
  switch_x64    process_early / TTBR
```

Porting checklist:

```text
  new arch → linker.ld + boot entry
          → early_init
          → syscall entry + dispatch ABI
          → switch_context_* + first_switch_to
          → fault/MMU setup
          → architecture_guard exemptions review
```

## 8. Important invariants

1. `task_t` layout fixed for x86-64 ASM — changing offsets breaks switch.
2. `ARCH_SUPPORTS_APIC` is 1 on x86-64 config, 0 on arm64.
3. Scaffold files listed in guard must exist for arm64 matrix builds.
4. Production musl/ISO smokes target x86-64 only today.
5. `arm64_cpu_switch_mm`: activate `next_ttbr` when non-zero and ≠ current TTBR0.
6. **Boot serial contract is ISA-agnostic:** after UART init, `ir0_boot_serial_ready()`
   emits the same `[BOOT] IR0 Kernel v… Boot routine` banner first; ISA detail uses
   COMP `ARCH`, stage tags COMP `SMOKE` (`includes/ir0/boot_log.h`).
7. **ARM64 board is compile-time** (`includes/ir0/arm64_board.h`, `ARM64_BOARD=` /
   Kconfig): `qemu-virt` (PL011 `0x09000000`), `rpi4` (PL011 `0xfe201000`, QEMU
   `raspi4b`), `rpi5` (UART stub — no RP1 yet). No DTB parse yet.

## 9. Debugging tips

- `make build-matrix-min` — builds arch variants per matrix.
- `make arch-guard` — facade violations before merge.
- `get_arch_name()` / `/proc/cpuinfo` for runtime ISA string.
- arm64 boot: `make smoke-arm64` / `smoke-arm64-syscall` (requires
  `ARM64_PROCESS_TTBR_OK` among other tags). First framed line must be the
  portable BOOT banner (`ir0_boot_serial_ready`), then `ARM64_*` smoke tags.
- arm64 F7b pack: `make smoke-arm64-port` / `smoke-arm64-gic`.
- arm64 portable compile: `make arm64-portable-compile` (curated objs — **not** `ALL_OBJS`).
- Probe: `make arm64-all-objs-probe` (MEMORY + KERNEL sample compile-only).
- arm64 scaffold link: `make ARCH=arm64 kernel-arm64.bin`.
- arm64 boards: `make smoke-arm64-rpi4-boot` (QEMU `raspi4b` or **SKIP** if machine
  missing); `make arm64-rpi5-compile` (stub strings only). See `scripts/make/arm64-board.mk`.

## 10. Future roadmap

- Product ARM `fork`/`exec` / `rr_sched` with real `process_t` — **not** in freestanding image.
- **ALL_OBJS + musl aarch64** — BLOCKED (toolchain SETUP / interrupt wall).
- GIC behind full `register_irq` product path.
- Raspberry Pi 4: GIC-400, SD, mailbox, DTB (beyond UART min smoke).
- Raspberry Pi 5: RP1 UART + real bring-up (board id stub only today).
- Remove x86-only `#ifdef` clusters in keyboard/console for true portability.
- UEFI boot on x86 — GRUB Multiboot only today.
- RISC-V / x86-32 — **not in tree**.

Legacy: `Documentation/DECOUPLING.md`, `arch/README.md`.
