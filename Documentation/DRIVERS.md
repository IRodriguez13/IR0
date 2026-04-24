# IR0 Driver Subsystem

IR0 uses a centralized registry and bootstrap path for core and optional drivers.

## Registry and Bootstrap

- Registry API: `includes/ir0/driver.h`, implementation in `kernel/driver_registry.c`.
- Bootstrap orchestration: `drivers/init_drv.c` and `drivers/driver_bootstrap.c`.
- `init_all_drivers()` is the single runtime entry point for staged init.
- Config-selected boot drivers are gated by `CONFIG_INIT_*` options.

## Main Driver Families

- Input: PS/2 controller, keyboard, mouse.
- Timers: PIT, RTC, HPET, LAPIC, clock abstraction.
- Storage: ATA core and ATA block path.
- Network: RTL8139 path used by network stack.
- Audio: Sound Blaster, Adlib, PC speaker.
- Video/console: typewriter, console backend, VBE path.
- Serial: UART logging/control path.
- Bluetooth: HCI and related support path.

## User-Facing Integration

- Devices surface through `/dev` nodes.
- Driver status surfaces through `/proc/drivers`.
- Kernel initialization and failures are logged through backend logging paths.

## Strengths

- Driver lifecycle is now easier to reason about due to unified bootstrap.
- Menuconfig-gated init reduces accidental startup of disabled hardware paths.
- Registry introspection improves runtime debugging and bring-up visibility.

## Weak Points

- No full hotplug model yet.
- Policy and fallback behavior still prioritize practical bring-up over strict isolation.
- Legacy-oriented device support remains a dominant profile.

