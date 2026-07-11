# IR0 Memory Subsystem

> **Last verified:** 2026-07-10  
> **Source of truth:** `mm/pmm.c`, `mm/paging.c`, `arch/x86-64/sources/fault.c`,  
> `Documentation/mandocs/en/mm.md`, `make smoke-mm-cow-lazy`

IR0 memory management currently combines PMM, kernel allocator, and paging-based
process isolation.

## Core Layers

- `mm/pmm.c`: physical page frame tracking, allocation, and per-frame refcount
  (`pmm_frame_get` / `pmm_frame_put`; `pmm_free_frame` drops one reference).
- `mm/allocator.c` plus `includes/ir0/kmem.h`: kernel dynamic allocator.
- `mm/paging.c`: virtual mapping, page-table setup, fork share-on-fork (`PAGE_COW`).
- `arch/x86-64/sources/fault.c`: lazy heap/stack fill and write-fault COW break.

## Operational Model

- Frame allocator provides physical pages for kernel and mapping paths.
- Kernel allocator serves most dynamic structures across subsystems.
- Paging provides address-space boundaries and execution context transitions.
- Fork shares present user PFNs; formerly writable pages are mapped read-only with
  software `PAGE_COW` (PTE bit 9) until the first write fault.

## Process Integration

- Process creation wires memory structures with per-process context.
- Scheduler/context switch path relies on paging state transition.
- User access validation and copy helpers enforce boundary checks.

## Strengths

- Clean separation of physical, heap, and virtual memory concerns.
- Real fork COW + lazy anon mmap/brk proven by `make smoke-mm-cow-lazy` (FASE40 A–F).
- Instrumentation available via proc endpoints and runtime logs.

## Weak Points

- No huge-page COW, no file-backed COW, no swap-class reclaim.
- Performance tuning for large workloads is not yet the main target.
- Some memory policies still prioritize simplicity over full POSIX-like depth.
