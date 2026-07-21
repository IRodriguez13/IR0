# IR0 Boot Pipeline

| Field | Value |
|-------|-------|
| Version | 0.1 |
| IR0 phase | T0 |
| Status | stable |
| Depends on | memory, drivers, vfs, process |
| Man page | IR0-boot (section 7) |
| Primary sources | `arch/x86-64/asm/boot_x64.asm`, `arch/x86-64/sources/arch_early.c`, `kernel/main.c`, `fs/vfs.c`, `kernel/elf_loader.c` |

## 1. Overview

The boot path on x86-64 runs from a Multiboot-compatible GRUB load through minimal
page tables, `kmain`, driver and VFS bring-up, syscall/IRQ enablement, and either
in-kernel debug shell or `kexecve("/sbin/init")`. There is no separate `boot/`
directory; boot code lives under `arch/` and `kernel/main.c`.

## 2. Internal architecture

| Stage | Component | File |
|-------|-----------|------|
| Loader entry | Multiboot check, PAE, long mode | `arch/x86-64/asm/boot_x64.asm` |
| Early CPU | GDT, TSS, SSE | `arch/x86-64/sources/arch_early.c` |
| Kernel entry | Orchestration | `kernel/main.c` (`kmain`) |
| Drivers | Staged bootstrap | `drivers/init_drv.c` |
| Root FS | Mount table init | `fs/vfs.c` (`vfs_init_root`) |
| Userspace | ELF load + schedule | `kernel/elf_loader.c` (`kexecve`) |

**Early paging (`boot_x64.asm`):** identity map 0–48 MiB with 2 MiB pages;
optional framebuffer window at `0xFD000000`. Stack at `0x8FF00`; Multiboot info
in `RDI` for `kmain`.

## 3. Data flow

```
GRUB → boot_x64.asm
         → kmain(multiboot_info)
              → set_boot_params / early_init
              → heap_init (0x800000)
              → [CONFIG_ENABLE_VBE] video_backend_init_from_multiboot
              → console_backend_init
              → pmm_init (32–48 MiB)
              → ir0_driver_registry_init + serial_init
              → init_all_drivers()
              → vfs_init_root()  → mount / or tmpfs fallback
              → process_init + ipc_init + clock_system_init
              → syscall_init + syscalls_init
              → irq_init + boot_irq_unmask + sti
              → [KERNEL_DEBUG_SHELL] start_init_process
                OR ir0_rootfs_prepare_userspace_base + kexecve("/sbin/init")
              → sched_schedule_next → ring 3
```

ASCII map:

```text
  Multiboot EBX ──► kmain
                      │
    arch_early ───────┤ GDT/TSS/SSE
    heap + PMM ───────┤ 8–32 MiB heap, 32–48 MiB frames
    drivers ──────────┤ bootstrap stages INPUT→NET
    vfs_init_root ────┤ /dev/hda → / (minix) or tmpfs
    syscalls + IRQ ───┤ int 0x80 + syscall insn
    kexecve ───────────► /sbin/init (musl static)
```

## 4. Responsibilities

- **boot_x64.asm:** CPU mode transition only; no C runtime.
- **kmain:** Ordered subsystem init; must not return to userspace without scheduler.
- **init_all_drivers:** Register and init Kconfig-gated hardware stacks.
- **vfs_init_root:** Provide a usable `/` before any file-based exec.
- **kexecve:** Load ELF from VFS, map segments, enqueue process.

## 5. Subsystem boundaries

- Boot assembly must not call VFS or kmalloc before `heap_init`.
- Driver init runs before VFS root mount so block devices exist for MINIX.
- Video init is optional (`CONFIG_ENABLE_VBE`); VGA fallback via `video_backend_init_fallback`.

## 6. Relations to other subsystems

| Neighbor | Link |
|----------|------|
| Memory | `heap_init`, `pmm_init` before most subsystems |
| Drivers | `init_all_drivers` before `vfs_init_root` block check |
| VFS | Root mount uses `CONFIG_ROOT_BLOCK_DEVICE`, `CONFIG_ROOT_FILESYSTEM` |
| Process | `process_init` before first `kexecve` |
| Scheduler | First user task entered via `sched_schedule_next` |

## 7. Visual maps

```text
  [GRUB]──►[boot_x64]──►[kmain]──►[drivers]──►[VFS /]
                                      │              │
                                      ▼              ▼
                                 [block_dev]    [kexecve /sbin/init]
```

Mermaid source: `Documentation/mandocs/diagrams/boot.mmd`

## 8. Important invariants

1. Multiboot magic must be `0x2BADB002` or boot halts with `"MN"` on VGA.
2. PMM pool (`0x2000000`–`0x3000000`) must not overlap kernel heap (`0x800000`–`0x2000000`).
3. `sti` runs only after IDT/PIC and syscall tables are initialized.
4. If `sched_schedule_next` returns after init handoff, `kmain` panics.
5. No separate kernel higher-half VA; boot identity map serves kernel and early user.

## 9. Debugging tips

Serial tags: `[ARCH]`, `klog` COMP `BOOT`, `[DRIVERS]`, `SERIAL: kmain: Loading userspace init`,
`[ts] [INFO] [FASE…] CLASSIFY ROOTFS_LAYOUT_OK` (no dialect `[COMP][CLASSIFY]`).

| Symptom | Check |
|---------|-------|
| `"MN"` on screen | Multiboot header mismatch |
| Root mount fail | `block_dev_is_present(CONFIG_ROOT_BLOCK_DEVICE)`; tmpfs fallback |
| Blank GUI | `[BOOT] vbe_fail_reason=` (1=mb_null, 2=no_fb, 3=bad_dims, 4=map_fail) |
| Init not found | MINIX image missing `/sbin/init`; use inject scripts |

Build: `make kernel-x64.iso`; userspace: `make kernel-x64-userspace.iso`.

## 10. Future roadmap

- ARM64 boot stub exists but is not production-ready (`arch/arm64/sources/boot_stub.c`).
- SMP/APIC-first boot not primary (`CONFIG_ENABLE_SMP=0`).
- Higher-half kernel mapping not implemented (identity low map only).
- UEFI direct boot not in tree (GRUB Multiboot path only).
