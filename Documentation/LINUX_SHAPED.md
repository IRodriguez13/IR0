# Linux-shaped IR0 (names and ABI, not a tree clone)

> **Last verified:** 2026-09-22
> **Source of truth:** `includes/ir0/copy_user.h`, `includes/ir0/arch_cpu.h`,
> `includes/ir0/context.h`, `includes/ir0/cpu.h`, `includes/ir0/irq.h`,
> `scripts/architecture_guard.py`, [`DECOUPLING.md`](DECOUPLING.md),
> [`uaccess.md`](uaccess.md), [`HARNESS_MAP.md`](HARNESS_MAP.md)

IR0 takes **Linux learnings** (syscall ABI, observable contracts, facade names)
as training for later Linux work. It does **not** clone the Linux directory
layout. The IR0 tree stays IR0: `sched/` at repo root, public APIs in
`includes/ir0/`, no `include/linux/` dump.

Spanish mirror: [`esp/LINUX_SHAPED.md`](esp/LINUX_SHAPED.md).

## What to copy from Linux

| Take | Why | IR0 home |
|------|-----|----------|
| x86-64 syscall ABI (`rax`/`rdi`…, `-errno`) | musl / BusyBox / man pages are the spec | `kernel/syscalls/` |
| Usercopy layering | Current mm vs explicit pgd; never CR3+`memcpy` | `copy_user.h` |
| Simple public names (`copy_to_user`, `switch_to`, `cpu_relax`) | Portable code must not say `arch_*` on hot paths | `includes/ir0/*.h` |
| Observable contracts | Don't break userspace; classify before patching ports | `scripts/linux_abi/contracts.json` |
| Error returns (`0` or `-errno`) | Same as Linux internal APIs | `includes/ir0/errno.h` |

## What not to copy

| Skip | Why |
|------|-----|
| Linux `kernel/sched/` layout | IR0 `sched/` is the scheduler root; moving it is unpaid tree-clone debt |
| `include/linux/*.h` dump | Facades stay small and IR0-owned |
| One `.c` per syscall | Domain modules under `kernel/syscalls/` already exist |
| Linux Kconfig / `drivers/` clone | IR0 Kconfig + `includes/ir0/` facades |

## Architecture facades

`includes/ir0/arch_cpu.h` is a **compatibility umbrella**. New code includes
the domain header, not the umbrella.

| Domain | Header | Public names |
|--------|--------|--------------|
| CPU identity / relax / timer extras | `cpu.h`, `cpu_info.h` | `cpu_relax`, `cpuid` (portable), `timer_read` |
| IRQ | `irq.h` | `irq_save`, `irq_restore`, `enable_interrupts` |
| I/O ports | `arch_io.h` | `inb`, `outb`, … |
| MM / paging activate | `arch_mm.h` | `mm_activate`, `arch_mm_copy_kernel_half` |
| Page fault classify | `page_fault.h` | portable classify helpers |
| TLS | `tls.h` | `set_tls` |
| Context switch | `context.h` | `switch_to`, `switch_to_user`, `prepare_task_user_iretq` |
| Types / early clock / multiboot / TLB | `arch_types.h`, `early_clock.h`, `multiboot.h`, `tlb.h` | as named |

The umbrella **must not** include `cpu.h`: backend `cpuid()` signatures
conflict with the portable `void cpuid(...)` facade.

Ring-3 first entry uses the simple name `prepare_task_user_iretq` (not
`arch_prepare_*`). ISA bodies stay under `arch/<isa>/`.

## Usercopy stack (Linux-shaped layers)

| Layer | IR0 name | Linux analogue |
|-------|----------|----------------|
| Current mm | `copy_to_user` / `copy_from_user` / `clear_user` | `copy_to/from_user` |
| Explicit pgd (callers) | `copy_to_user_mm` / `copy_from_user_mm` / `zero_user_mm` | `access_remote_vm` / `access_process_vm` |
| Walk primitive | `copy_*_user_region_in_directory` / `zero_user_region_in_directory` | `raw_copy_*` + page walk |
| VA window | `access_ok` / `mm_user_va_ok` | `access_ok` + arch USER_DS |

Rule: callers prefer `copy_*_user` or `copy_*_user_mm`. Do not assume
`active_cr3` when the target is another process. Full contract:
[`uaccess.md`](uaccess.md).

## Harness names

Wave-era `FASE*` tokens are gone from C. Semantic serial tags and Make
targets live in [`HARNESS_MAP.md`](HARNESS_MAP.md). Old `smoke-fase*` /
`build-*-fase*` names remain **aliases**.

Kept on purpose (external or high-risk):

- ISD env `FASE50_BUSYBOX_BIN` (scripts in the ISD sibling still read it)
- ISD config paths `packages/busybox/fase58_*.config`
- `setup/pid1/fase52_staging/` (TCC sysroot; do not `git mv`)
- Historical migrate scripts under `scripts/migrate_fase*.py`
