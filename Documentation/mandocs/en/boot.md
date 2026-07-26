# IR0 Boot Pipeline

| Field | Value |
|-------|-------|
| Version | 0.2 |
| IR0 phase | T0 / T1 product |
| Status | stable (product: runit only; dbgshell removed) |
| Depends on | memory, drivers, vfs, process |
| Man page | IR0-boot (section 7) |
| Primary sources | `arch/x86-64/asm/boot_x64.asm`, `arch/x86-64/sources/arch_early.c`, `kernel/main.c`, `fs/vfs.c`, `kernel/elf_loader.c`, `includes/ir0/klog_event.h`, `ktm/klog.c` |

## 1. Overview

The boot path on x86-64 runs from a Multiboot-compatible GRUB load through minimal
page tables, `kmain`, driver and VFS bring-up, syscall/IRQ enablement, and
`kexecve("/sbin/init")` (runit from **IR0-userspace**). The former in-kernel
dbgshell / `debug_bins/` path was **removed** (2026-07-25); see
[`USERSPACE.md`](../../USERSPACE.md). Structured boot logging:
[`KLOG.md`](../../KLOG.md).

## 2. Internal architecture

| Stage | Component | File |
|-------|-----------|------|
| Loader entry | Multiboot check, PAE, long mode | `arch/x86-64/asm/boot_x64.asm` |
| Early CPU | GDT, TSS, SSE, **early IDT** | `arch/x86-64/sources/arch_early.c`, `interrupt/arch/x86-64/early_idt*.{c,asm}` |
| Kernel entry | Orchestration + boot phases | `kernel/main.c` (`kmain`) |
| Log profile | Kconfig + `ir0.loglevel=` cmdline | `kernel/cmdline.c` |
| Event core | `klog_record` + early clock | `ktm/klog.c`, `arch_early_clock_*` |
| Drivers | Staged bootstrap + probe events | `drivers/init_drv.c` |
| Root FS | Mount table init | `fs/vfs.c` (`vfs_init_root`) |
| Bare-metal milestone | `/etc/ir0-baremetal-booted` | `kernel/main.c` (post-VFS) |
| Userspace | ELF load + schedule | `kernel/elf_loader.c` (`kexecve`) |

**Early paging (`boot_x64.asm`):** identity map 0–48 MiB with 2 MiB pages;
optional framebuffer window at `0xFD000000`. Stack at `0x8FF00`; Multiboot info
in `RDI` for `kmain`.

## 3. Data flow

```
GRUB → boot_x64.asm
         → kmain(multiboot_info)
              → set_boot_params / ir0_cmdline_apply_log_profile / early_init
                 (GDT + TSS + early IDT)
              → heap_init (0x800000)
              → [CONFIG_ENABLE_VBE] video_backend_init_from_multiboot
              → console_backend_init
              → pmm_init (32–48 MiB)
              → logging_init + klog_promote_normal_ring
              → ir0_driver_registry_init + serial_init
              → phase EARLY_ARCH: ir0_boot_serial_ready() (BOOT banner)
              → PLATFORM / HYPERVISOR lines
              → phase DRIVERS: init_all_drivers() + DRIVER_PROBE_RESULT*
              → vfs_init_root() + optional FIRST_BAREMETAL_BOOT sentinel
              → process_init + ipc_init + clock_system_init (MONOTONIC)
              → syscall_init + syscalls_init
              → irq_init + boot_irq_unmask + sti  (full IDT replaces early table)
              → "kernel core initialization complete"
              → [CONFIG_KTM] suite → "KTM validation complete"
              → "system ready for userspace" + kexecve("/sbin/init")
              → sched_schedule_next → ring 3
```

**Early IDT:** installed immediately after TSS in `early_init_x86_64()` so a
page fault or GPF during bring-up dumps `vec/err/rip/cr2` on COM1 and halts
instead of triple-faulting silently. `irq_init()` installs the full table later.

## 4. Responsibilities

- **boot_x64.asm:** CPU mode transition only; no C runtime.
- **kmain:** Ordered subsystem init; must not return to userspace without scheduler.
- **init_all_drivers:** Register and init Kconfig-gated hardware stacks; emit probe results.
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
| Klog | [`KLOG.md`](../../KLOG.md) — phases, sinks, `/proc/kmsg` |

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
6. Framed klog starts with the BOOT banner; every record carries `#sequence` and
   boot phase; timestamps stay `[    ?.???]` until monotonic clock
   ([`KLOG.md`](../../KLOG.md)).
7. Boot always hands off to `/sbin/init`; exploration is ash + `/proc/kmsg`
   (no in-kernel shell).

## 9. Debugging tips

New contributors: start with `make man TOPIC=onboarding` (first bug walkthrough).

| Symptom | Check |
|---------|-------|
| No serial after banner | `klog_boot_hold` / serial_init order |
| Stuck before getty | `smoke-runit-boot` tags / disk inject |
| Missing `/proc/kmsg` content | `klog_read_records` vs old logging buffer |

## 10. Known limits

- SMP/APIC-first boot not primary (`CONFIG_ENABLE_SMP=0`).
- Bare-metal first-boot sentinel only when no hypervisor is detected.
