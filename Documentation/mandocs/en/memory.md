# IR0 Memory Management

| Field | Value |
|-------|-------|
| Version | 0.1 |
| IR0 phase | T0–T1 |
| Status | stable |
| Depends on | boot, process, syscalls |
| Man page | IR0-memory (section 7) |
| Primary sources | `mm/pmm.c`, `mm/paging.c`, `mm/allocator.c`, `mm/kmem.c`, `kernel/syscalls.c`, `kernel/elf_loader.c` |

## 1. Overview

IR0 uses a fixed low-memory layout: identity-mapped boot region, dedicated kernel
heap, a PMM frame pool, and per-process page directories for user tasks. There is
**no kernel higher-half**; kernel and user both use canonical low VAs with separate
PML4s for user processes.

## 2. Internal architecture

| Layer | File | Role |
|-------|------|------|
| PMM | `mm/pmm.c` | 4 KiB frame bitmap (`pmm_alloc_frame`, `pmm_free_frame`) |
| Heap | `mm/allocator.c` | Boundary-tag allocator at `0x800000`, 24 MiB |
| kmem | `mm/kmem.c` | `kmalloc`/`kfree` → `alloc()` |
| Paging | `mm/paging.c` | `map_page_in_directory`, user/supervisor maps, OOM audit |
| Syscalls | `kernel/syscalls.c` | `sys_mmap`, `sys_brk`, `sys_munmap` |
| ELF | `kernel/elf_loader.c` | PT_LOAD via `map_user_region_in_directory` |

**Fixed layout (`config.h` / boot):**

```text
  0 ─────────────── 48 MiB   identity map (boot)
  0x800000 ─────── 0x2000000  kernel heap (24 MiB)
  0x2000000 ────── 0x3000000  PMM pool (16 MiB)
  USER_HEAP_BASE   0x2000000  brk start
  USER_MMAP_START  0x8000000  mmap arena
  user stack top   ~0x7FFFF000
```

## 3. Data flow

**Kernel allocation:** `kmalloc` → `alloc()` on `[0x800000, 0x2000000)`.

**User process bring-up (ELF):**

1. `spawn_user` → new `page_directory` (isolated PML4).
2. Phase 1: map all PT_LOAD with `map_user_region_in_directory` under **kernel CR3**.
3. Phase 2: copy segment bytes / zero BSS via physical frame access.
4. `elf_setup_stack` — argc/argv/envp on user stack.
5. `sched_add_process`.

**`sys_brk`:** extends from `USER_HEAP_BASE` up to `USER_HEAP_MAX_SIZE` (256 MiB cap).

**`sys_mmap`:**

```text
  sys_mmap
     ├─ MAP_ANONYMOUS → map pages + mmap_list VMA
     ├─ file-backed → vfs read path + map
     └─ /dev/fb0 (devfs id 15, CONFIG_ENABLE_VBE)
            → ir0_fb_get_info()
            → map physical FB pages PAGE_USER|PAGE_RW
            → mmap_list entry
```

**Page fault (`arch/x86-64/sources/fault.c`):** validates against user heap and mmap VMA ranges.

## 4. Responsibilities

- PMM: track physical frames in configured pool only.
- Paging: never assume `active_cr3` when target is another process — use explicit `page_directory`.
- `copy_user`: walk **current process** page tables via MM facades.
- ELF loader: complete mapping before adding to scheduler.

## 5. Subsystem boundaries

- `fs/` must not `#include <mm/...>` — use `ir0/mm_port.h`, `ir0/kmem.h`.
- `mm/` must not `#include <arch/...>` — use `ir0/arch_port.h`.
- User copies from pipes/wake paths must use `copy_*_region_in_directory(pml4, …)`.

## 6. Relations to other subsystems

| Neighbor | Interaction |
|----------|---------------|
| Boot | Identity map in `boot_x64.asm`; PMM init range from `kmain` |
| Process | `page_directory`, `mmap_list`, `owns_page_directory` |
| VFS | ELF `vfs_read_file`; mmap file-backed I/O |
| Devfs | Framebuffer mmap via fb0 device id |
| Syscalls | brk/mmap/mprotect/munmap |

## 7. Visual maps

```text
  Physical RAM
  ┌────────────────────────────────────┐
  │ 0–48M identity (boot/kernel access) │
  │ heap 8–32M   PMM frames 32–48M     │
  └────────────────────────────────────┘
           ▲                    ▲
           │ kmalloc            │ pmm_alloc_frame
           │                    │
  Per-process PML4 ──map_user_region──► user PT_LOAD / mmap / stack
```

## 8. Important invariants

1. PMM manages only `PMM_PHYS_BASE`..`PMM_PHYS_SIZE` (default 32–48 MiB).
2. Kernel heap is bounded; no grow into PMM at runtime.
3. User/kernel isolation via separate PML4 per process (except idle shares kernel CR3).
4. OOM paths classified: `FASE43` boot_fatal / kernel_fatal / user_recoverable.
5. `process_destroy` unmaps under **process PML4**, not necessarily active CR3.

## 9. Debugging tips

Tags: `[PMM]`, `[FASE43][OOM_CLASS]`, `SERIAL: ELF: Mapping segment`, `FB_MMAP_*`.

- `/proc/meminfo` — runtime memory stats (procfs generated text).
- Page fault loops: check unmapped mmap or heap overrun.
- Framebuffer mmap fails: verify `CONFIG_ENABLE_VBE` and fb0 open.

## 10. Future roadmap

- Copy-on-write fork — **not implemented** (full copy today).
- Demand paging for anonymous mmap — partial; fault handler covers some cases.
- Higher-half kernel VA — not planned in current tree.
- Larger PMM pool / dynamic physical memory discovery beyond Multiboot mem map debt.
