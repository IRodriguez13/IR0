# IR0 — UP to SMP readiness boundaries

Status: inventory, 2026-10-10.  IR0 is intentionally UP today.  The purpose of
this document is to make that assumption explicit before ARM and x86 acquire
independent CPUs, not to claim that IRQ masking is an SMP lock.

## Current UP contracts

- `current_process` is a single global selected by `sched/sched_switch.c` and
  declared in `kernel/process/registry.c`.  It is protected today by local IRQ
  masking around scheduler and registry transitions.  SMP replacement: a
  per-CPU current-task accessor; callers must not cache another CPU's task.
- `sched/rr_sched.c`, `sched/priority_sched.c` and `sched/sched_switch.c` use
  `irq_save()`/`irq_restore()` to serialize runnable-state lists and the
  current-task transition.  This is valid only because no second CPU executes
  the lists.  SMP replacement: scheduler runqueue locks plus local IRQ state.
- `mm/pmm.c`, `mm/paging.c` and `mm/page_fault.c` have critical regions that
  mask local IRQs while mutating page metadata or PTE state.  SMP replacement:
  frame allocator and address-space locks, followed by architecture TLB shoot-
  down callbacks.  A local invalidate is not sufficient after a remote CPU can
  run the same address space.
- `kernel/process/{registry,mm_struct,files_lifecycle,fork}.c` protects global
  process ownership and lifecycle lists with local IRQ masking.  SMP
  replacement: process-list and per-object lifetime locks with a documented
  lock order; descriptor/resource references need atomic or locked ownership.
- `kernel/lib/{named_fifo,named_devnode}.c` use the same local IRQ convention
  for their global registries.  SMP replacement: registry locks; device
  callbacks must execute outside them unless explicitly documented otherwise.

## Rules for new code before SMP

1. A new `irq_save()` critical section must state in a nearby comment whether
   it protects only local interrupt reentrancy or a future shared object.
2. Do not present IRQ masking as cross-CPU synchronization.  New shared state
   should have a small ownership facade so an SMP lock can be inserted without
   changing callers.
3. Architecture code owns inter-processor interrupt and TLB mechanics.  Common
   MM/scheduler code owns when a shoot-down or reschedule is required.
4. Preserve the existing UP proof matrix while splitting any critical section:
   x86 runit/ash, ARM64 BusyBox/PID1 EL0, RPi compile, architecture guard and
   ISA-security guard.

## Entry criteria for an SMP implementation wave

- Per-CPU current-task and CPU-local data accessors exist behind common APIs.
- Runqueue, process registry, PMM and address-space lock ordering is written
  and mechanically testable where possible.
- The architecture backend supplies IPI delivery and remote TLB invalidation
  callbacks; common code has no APIC/GIC register knowledge.
- A one-CPU configuration continues to use the same facades and passes the
  existing x86 and ARM smoke matrix.
