# IR0 Memory Subsystem

IR0 memory management currently combines PMM, kernel allocator, and paging-based
process isolation.

## Core Layers

- `mm/pmm.c`: physical page frame tracking and allocation.
- `mm/allocator.c` plus `includes/ir0/kmem.h`: kernel dynamic allocator.
- `mm/paging.c`: virtual mapping and page-table setup.

## Operational Model

- Frame allocator provides physical pages for kernel and mapping paths.
- Kernel allocator serves most dynamic structures across subsystems.
- Paging provides address-space boundaries and execution context transitions.

## Process Integration

- Process creation wires memory structures with per-process context.
- Scheduler/context switch path relies on paging state transition.
- User access validation and copy helpers enforce boundary checks.

## Strengths

- Clean separation of physical, heap, and virtual memory concerns.
- Stable enough for ongoing process/fs/network stabilization work.
- Instrumentation available via proc endpoints and runtime logs.

## Weak Points

- Advanced VM features remain limited (for example, full COW/swap-class behavior).
- Performance tuning for large workloads is not yet the main target.
- Some memory policies still prioritize simplicity over full POSIX-like depth.

