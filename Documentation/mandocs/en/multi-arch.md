# IR0 Multi-Architecture Support

| Field | Value |
|-------|-------|
| Version | 0.1 |
| IR0 phase | T0 |
| Status | stable |
| Depends on | boot, syscalls, scheduler |
| Man page | IR0-multi-arch (section 7) |
| Primary sources | `arch/common/arch_interface.c`, `arch/x86-64/`, `arch/arm64/`, `scripts/architecture_guard.py`, `includes/ir0/arch_port.h` |

## 1. Overview

IR0 separates portable kernel code from architecture backends under `arch/`.
**x86-64** is the production target (ISO, musl userspace, full syscall path).
**arm64** has early identity-map MMU (F7.1), VBAR/SVC EL1 (F7.2), EL0 drop +
lower-EL SVC + PSCI SYSTEM_OFF (F7.3). Context switch / full kernel link /
musl userspace remain **out of the freestanding boot image**.

## 2. Internal architecture

| Layer | Location | Role |
|-------|----------|------|
| Portable facade | `includes/ir0/arch_port.h` | CPU queries, IRQ enable, port I/O facade |
| Common interface | `arch/common/arch_interface.c` | Cross-arch dispatch |
| x86-64 | `arch/x86-64/` | boot, IDT, PIC, user mode, syscalls |
| arm64 | `arch/arm64/sources/` | boot_stub, mmu_early, vectors/exc_early (F7.2), scaffold |
| Context switch | `sched/switch/switch_x64.asm`, `switch_arm64.c` | per-ISA |
| Config | `setup/Kconfig`, `ARCH=` in Makefile | object selection |

**Guard tags (`architecture_guard.py`):** portable trees must not include
`<arch/...>` or `<drivers/...>` directly; use facades.

## 3. Data flow

**x86-64 syscall path:**

```text
  musl → syscall insn → syscall_insn_entry_64.asm
       → syscall_dispatch (kernel/syscalls.c)
       → handler → sysret

  debug_bins → int 0x80 → syscall_entry_64.asm → dispatch
```

**Context switch:**

```text
  sched_schedule_next → arch_context_switch.c
       → switch_context_x64 (ASM) or arch_switch_to_user (first entry)
```

**arm64 (current):**

```text
  _start → BOOT_OK → MMU_OK → VBAR → EL1 svc → SVC_RET_OK
        → EL0_DROP → EL0 svc → EL0_SVC_OK → EL0_RET_OK → PSCI_OFF
  switch_arm64.c → empty stub (full sched not in boot image)
```

## 4. Responsibilities

- `arch/` implements hooks declared in `arch_portable.h` / `arch_port.h`.
- Portable code selects behavior via `CONFIG_*` and facades, not `#ifdef` in `fs/`.
- Makefile gates object lists per `ARCH=x86-64|arm64`.

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
| Scheduler | per-arch switch implementation |
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
  (full)      (scaffold)
     │           │
  boot/syscall  boot_stub
  switch_x64    switch stub
```

Porting checklist:

```text
  new arch → linker.ld + boot entry
          → arch_early_init
          → syscall entry + dispatch ABI
          → switch_context_* 
          → fault/MMU setup
          → architecture_guard exemptions review
```

## 8. Important invariants

1. `task_t` layout fixed for x86-64 ASM — changing offsets breaks switch.
2. `ARCH_SUPPORTS_APIC` is 1 on x86-64 config, 0 on arm64.
3. Scaffold files listed in guard must exist for arm64 matrix builds.
4. Production smokes and musl toolchain target x86-64 only today.

## 9. Debugging tips

- `make build-matrix-min` — builds arch variants per matrix.
- `make arch-guard` — facade violations before merge.
- `arch_get_name()` / `/proc/cpuinfo` for runtime ISA string.
- arm64 boot: `make smoke-arm64` (boot+mmu+vbar+el0 on QEMU virt).
- arm64 scaffold link: `make ARCH=arm64 kernel-arm64.bin` (no full userspace ISO path).

## 10. Future roadmap

- Full arm64 `ALL_OBJS` kernel link + musl userspace / context switch.
- Remove x86-only `#ifdef` clusters in keyboard/console for true portability.
- UEFI boot on x86 — GRUB Multiboot only today.
- RISC-V / x86-32 — **not in tree** (`arch/README.md` may be stale).

Legacy: `Documentation/DECOUPLING.md`, `arch/README.md`.
