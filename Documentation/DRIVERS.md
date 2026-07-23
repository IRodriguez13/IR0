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

## Audio (SB16 / Adlib)

- Sources: `drivers/audio/sound_blaster.c`, `drivers/audio/adlib.c`.
- Successful SB16 DSP probe emits `klog_smoke("SB16_DSP_OK")` and logs DSP version.
- QEMU 8+ needs an audiodev before the ISA device:
  `-audiodev none,id=snd0 -device sb16,audiodev=snd0` (Adlib similarly).
- Variables / smoke: `scripts/make/boot-audio.mk` → `make smoke-sb16-probe`.
  Gate is **SB16 DSP detect**; Adlib may still report ABSENT on some QEMU builds
  (logged as note, not a fail).
- `make run` attaches `QEMU_AUDIO_ALL` when `CONFIG_ENABLE_SOUND≠n`.

