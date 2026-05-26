# IR0 Drivers

| Field | Value |
|-------|-------|
| Version | 0.1 |
| IR0 phase | T0 |
| Status | stable |
| Depends on | interrupts, memory |
| Man page | IR0-drivers (section 7) |
| Primary sources | `kernel/driver_registry.c`, `drivers/driver_bootstrap.c`, `drivers/init_drv.c`, `interrupt/arch/pic.c`, `interrupt/arch/isr_handlers.c` |

## 1. Overview

Hardware support is organized through a driver registry, staged bootstrap plan,
and Kconfig-gated init functions under `drivers/`. User-visible access is usually
via devfs nodes or facades in `includes/ir0/*`, not direct driver calls from
portable kernel code.

## 2. Internal architecture

| Component | Role |
|-----------|------|
| `driver_registry.c` | Linked list, states UNREGISTERED→ACTIVE, max 128 |
| `driver_bootstrap.c` | Up to 32 staged registrations, `driver_bootstrap_run_all` |
| `init_drv.c` | `init_all_drivers()` — INPUT→PLATFORM→STORAGE→AUDIO→NETWORK |
| `resource_registry` | IRQ/IOPORT registration from drivers |
| `multilang_drivers.c` | Example Rust/C++ drivers when `KERNEL_ENABLE_EXAMPLE_DRIVERS` |

**Bootstrap stages (`init_drv.c`):**

```text
  INPUT     ps2_controller, ps2_keyboard, ps2_mouse
  PLATFORM  pc_speaker, usb_host
  STORAGE   ata_core, ata_block
  AUDIO     sound_stack (SB16, AdLib, DMA)
  NETWORK   network_stack, bluetooth_stack (Kconfig gated)
```

## 3. Data flow

```text
  kmain → ir0_driver_registry_init()
       → init_all_drivers()
            → driver_bootstrap_run_all() per stage
                 → each driver init/register ops
       → block_dev ready for MINIX
       → devfs_register_node() for /dev/*

  IRQ n (32–47 PIC)
       → isr_handlers.c
            → pic_send_eoi64
            → device handler (e.g. keyboard_handler64)
                 → keyboard_poll_ps2 + stdin_wake_check
```

## 4. Responsibilities

- Drivers register capabilities; devfs/procfs expose user paths.
- Failed init increments bootstrap failure count but run continues.
- Storage drivers provide `block_dev` facade for filesystems.

## 5. Subsystem boundaries

- `kernel/` must not `#include <drivers/...>` — use `includes/ir0/*`.
- `drivers/` must not `#include <arch/...>` — use `ir0/arch_port.h`.
- Bluetooth headers only under `drivers/bluetooth/`.

## 6. Relations to other subsystems

| Neighbor | Interaction |
|----------|---------------|
| devfs | `/dev/disk`, `/dev/fb0`, `/dev/net`, … |
| procfs | `/proc/drivers`, interrupts, blockdevices |
| VFS | minix via block_dev |
| Timer | PIT in `clock_system.c`; LAPIC code present |
| Net | `net/` stack + rtl8139 when enabled |

## 7. Visual maps

```text
  init_all_drivers
        │
   ┌────┴────┬─────────┬─────────┬────────┐
   ▼         ▼         ▼         ▼        ▼
  PS/2     ATA/VBE   SB16     RTL8139   USB
   │         │         │         │        │
   └────┬────┴────┬────┴────┬────┴────────┘
        ▼         ▼         ▼
     devfs    block_dev   /proc
```

IRQ path:

```text
  hardware IRQ ──► IDT ──► PIC ISR ──► driver ISR ──► wake/poll
```

## 8. Important invariants

1. `init_all_drivers` is one-shot (`g_bootstrap_done`).
2. Registry max 128 drivers; bootstrap max 32 entries.
3. PIC IRQs 32–47 on x86-64 primary path.
4. Example multilang drivers off unless Kconfig enabled.

## 9. Debugging tips

- `/proc/drivers` — registry snapshot at read time.
- `/proc/interrupts` — IRQ counters.
- Serial `[DRIVERS]` tags during `kmain`.
- `CONFIG_ENABLE_NETWORKING=0` default — net stack may compile but be disabled.

## 10. Future roadmap

- APIC/LAPIC as primary IRQ demux — partial (LAPIC timer code exists).
- Hotplug USB storage — host init only at bootstrap today.
- Driver unload/module model — **not implemented**.
- SMP-safe driver locks — single-CPU assumption.

Legacy: `Documentation/DRIVERS.md`, `Documentation/INTERRUPTS.md`.
