# IR0 — UP to SMP readiness boundaries

Status: active scaffold, 2026-10-18.  IR0 is intentionally UP today.  The purpose of
this document is to make that assumption explicit before ARM and x86 acquire
independent CPUs, not to claim that IRQ masking is an SMP lock.

## Current UP contracts

- `current_process` remains the compatibility registry symbol, but all writers
  use `process_set_current()` and update the CPU-local current-task slot.  The
  scheduler also publishes the selected kernel stack through the same per-CPU
  state.  SMP replacement: make the compatibility symbol read-only or remove
  it after all readers use the CPU-local accessor.
- `sched/runqueue_lock.c` owns serialization for RR, priority and context
  selection.  Its UP backend masks local IRQs and deliberately refuses an SMP
  build.  SMP replacement is localized here, followed by per-CPU runqueue
  storage; policy backends must not call `irq_save()` directly.
- `mm/pmm.c`, `mm/paging.c` and `mm/page_fault.c` have critical regions that
  mask local IRQs while mutating page metadata or PTE state.  SMP replacement:
  frame allocator and address-space locks.  Common MM now requests coherence
  through `mm_tlb_sync_page/all(root, ...)`; its UP backend invalidates only an
  active root and refuses SMP builds.  The SMP replacement belongs behind that
  facade and must combine CPU residency masks, IPIs and architecture-local TLB
  invalidation.  A local invalidate is not sufficient after a remote CPU can
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
   Common code must not activate a foreign root merely to invalidate its TLB.
4. Preserve the existing UP proof matrix while splitting any critical section:
   x86 runit/ash, ARM64 BusyBox/PID1 EL0, RPi compile, architecture guard and
   ISA-security guard.

## Required lock order

The future SMP backend must preserve this outer-to-inner order:

1. scheduler runqueue ownership;
2. global process/PID registry ownership;
3. individual process lifetime state;
4. process MM, then files/descriptor state;
5. driver or filesystem-private registries.

No callback into VFS, a driver, allocation, or userspace copying may occur
while holding the runqueue or process-registry lock.  Current UP facades only
mask local IRQs, but keeping this order now prevents the eventual spinlock
backend from inheriting an accidental inversion.

## Entry criteria for an SMP implementation wave

- Per-CPU current-task and CPU-local data accessors exist behind common APIs.
- Runqueue, process registry, PMM and address-space lock ordering is written
  and mechanically testable where possible.
- The architecture backend supplies IPI delivery and remote TLB invalidation
  callbacks; common code has no APIC/GIC register knowledge.
- A one-CPU configuration continues to use the same facades and passes the
  existing x86 and ARM smoke matrix.
