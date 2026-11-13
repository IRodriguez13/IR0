# IR0 Memory Subsystem

> **Last verified:** 2026-11-07  
> **Source of truth:** `mm/alloc/pmm.c`, `mm/paging/paging.c`, `mm/fault/page_fault.c`,  
> `mm/swap/`, `includes/ir0/swap.h`, `arch/x86-64/sources/pf_debug.c`,
> `Documentation/mandocs/en/mm.md`, `Documentation/uaccess.md`,  
> `make smoke-mm-cow-lazy`, `tests/host` swap contracts, `ktest_swap_header_and_area`

IR0 memory management currently combines PMM, kernel allocator, and paging-based
process isolation.

## Core Layers

- `mm/alloc/pmm.c`: physical page frame tracking, allocation, and per-frame refcount
  (`pmm_frame_get` / `pmm_frame_put`; `pmm_free_frame` drops one reference).
- `mm/alloc/allocator.c` plus `includes/ir0/kmem.h`: kernel dynamic allocator.
- `mm/paging/paging.c`: virtual mapping, page-table setup, fork share-on-fork (`PAGE_COW`).
- `mm/fault/page_fault.c`: portable demand-zero, anon mmap, write-fault COW break, SIGSEGV policy.
- `arch/x86-64/sources/pf_debug.c`: optional `DEBUG_D1_DIAG` register-frame forensics (`pf_debug_stack_adjacent`, `pf_debug_memmove_fault`); arm64 stubs in `arch/arm64/sources/pf_debug.c`.

## Source Layout

`mm/` holds only portable code, grouped by responsibility; MMU, TLB and
atomic instructions live under `arch/<isa>/` behind `<ir0/arch_mm.h>`,
`<ir0/tlb.h>` and `<ir0/atomic.h>`.

| Directory | Files | Responsibility |
|-----------|-------|----------------|
| `mm/alloc/` | `pmm.c`, `alloc_policy.c`, `allocator.[ch]`, `kmem.c`, `mm_port.c` | Physical frames, bounded frame allocation, kernel heap, stats facade |
| `mm/boot/` | `boot_memory.c` | Firmware memory inventory and PMM handoff |
| `mm/fault/` | `page_fault*.c`, `page_fault_internal.h` | Demand-zero, COW break, SIGSEGV and kernel-fault policy |
| `mm/paging/` | `paging*.c`, `paging_internal.h`, `page_table_pool.c`, `tlb_sync.c` | Page-table walker, fork sharing, identity maps, table pool, TLB policy |
| `mm/uaccess/` | `copy_user.c`, `paging_uaccess.c`, `user_va.h` | `access_ok`, `copy_{to,from}_user`, COW-safe user copies |
| `mm/reclaim/` | `reclaim.c`, `oom.c`, `oom_kill.c` | Reclaimer registry, OOM policy and killer |
| `mm/swap/` | `swap*.c` | Swap areas, swap I/O and the swap reclaimer |
| `mm/shmem/` | `memfd.c` | `memfd_create` objects |
| `mm/nommu/` | `no_mmu.c` | Direct address-space policy for no-MMU targets |
| `mm/` | `diag.c` | `<ir0/mm_diag.h>` hooks (fault reports, fault injection) |

## Operational Model

- Frame allocator provides physical pages for kernel and mapping paths.
- Kernel allocator serves most dynamic structures across subsystems.
- Paging provides address-space boundaries and execution context transitions.
- Fork shares present user PFNs; formerly writable pages are mapped read-only with
  software `PAGE_COW` (PTE bit 9) until the first write fault.
- Anonymous private pages can be swapped through `swapon`/`swapoff` (`__NR` 167/168
  on x86-64, 224/225 on ARM64). Areas are SWAPSPACE2 block devices or preallocated
  MINIX swapfiles with pinned extents. Swap PTEs are ISA-encoded; reclaim walks
  heap/stack/anon mmap in process context. File-backed/shared swap, zswap, and
  hibernation are out of scope. Non-MINIX swapfiles return `-EOPNOTSUPP`.

## Page-fault policy notes (2026-09-01)

- **Soft-grow removed:** heap/stack VMAs are no longer extended implicitly on a fault one page past `heap_end` or stack top. Out-of-VMA faults deliver SIGSEGV (Linux expects explicit `brk`/`mmap` growth).
- **COW break:** write faults on `PAGE_COW` always copy to a new frame (`pmm_frame_get` pin when refcount is non-zero); shared frames are never promoted RW in place. Unchanged from prior FASE40 path; gate: `make smoke-mm-cow-lazy`.

## Stack top and signal frames (2026-09-02)

- Faults in `[USER_STACK_TOP, USER_STACK_TOP+4K)` classify as `STACK_TOP_OVERRUN`
  (see [`KTM.md`](KTM.md) §7.3). **Do not** reintroduce soft-grow past TOP; do not
  treat enlarging `USER_STACK_SIZE` as the primary fix for `addr=7ffff000`.
- User signal delivery (`do_signal` and `signals_deliver_from_irq_frame`)
  places frames via `signal_pick_handler_sp()` with `SIGNAL_HANDLER_TOP_MARGIN`
  (`includes/ir0/abi/signal_contract.h`) so frames stay out of the canary band.
- Gate: `make smoke-pipeline-stress` (0× `STACK_TOP_OVERRUN` on that path);
  session chaos treats the tag as fatal (`make smoke-session-chaos`).

## Process Integration

- Process creation wires memory structures with per-process context.
- Scheduler/context switch path relies on paging state transition.
- User access validation and copy helpers enforce boundary checks.
  **Canonical contract:** [`uaccess.md`](uaccess.md) — never raw
  `memcpy`/`memset` into user VAs; always `copy_*_user` or
  `*_region_in_directory` (COW break). Kernel `#PF` on user VA with `cs=8`
  classifies as `KERNEL_UACCESS_FAULT` and is fatal in session soak.

## Strengths

- Clean separation of physical, heap, and virtual memory concerns.
- Real fork COW + lazy anon mmap/brk proven by `make smoke-mm-cow-lazy` (FASE40 A–F).
- Instrumentation available via proc endpoints and runtime logs.

## Weak Points

- No huge-page COW, no file-backed COW, no zswap/zram, no hibernation.
- Performance tuning for large workloads is not yet the main target.
- Some memory policies still prioritize simplicity over full POSIX-like depth.
