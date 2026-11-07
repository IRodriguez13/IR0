# IR0 Driver Subsystem

> **Last verified:** 2026-11-07
> **Source of truth:** `drivers/init_drv.c`, `drivers/net/bcmgenet.c`, `sound/sound_blaster.c`, `arch/x86-64/sources/interrupt/isr_handlers.c`, `scripts/run_qemu.sh`

IR0 uses a centralized registry and bootstrap path for core and optional drivers.

## Registry and Bootstrap

- Registry API: `includes/ir0/driver.h`, implementation in `kernel/driver_registry.c`.
- Bootstrap orchestration: `drivers/init_drv.c` and `drivers/driver_bootstrap.c`.
- `init_all_drivers()` is the single runtime entry point for staged init.
- Config-selected boot drivers are gated by `CONFIG_INIT_*` options.

## Main Driver Families

- Input: PS/2 controller, keyboard, mouse.
- Timers: PIT, RTC, HPET, LAPIC, clock abstraction.
- Storage: ATA core and ATA block path. Odd userspace buffers use a 512-byte
  bounce in `drivers/storage/ata.c`; MINIX fast-path skips when dst/src is odd
  (`fs/minix_fs.c`) to avoid alignment storms.
- Network: RTL8139 and virtio-net on x86-64. ARM64 Raspberry Pi 4 uses BCM2711
  GENET v5 (`drivers/net/bcmgenet.c`, `CONFIG_DRV_NIC_BCMGENET`) with FDT
  `brcm,bcm2711-genet-v5`, DMA burst `0x08`, PHY@1 `rgmii-rxid`. Probe is honest:
  missing FDT node logs `GENET_PROBE_ABSENT`; hardware L2/ARP/ICMP is **BLOCKED**
  until a real RPi4 or QEMU GENET model is available. Raspberry Pi 3 LAN9514
  remains blocked pending DWC2+USB.
- Audio: Sound Blaster, Adlib, PC speaker.
- Video/console: typewriter, console backend, VBE path.
- Serial: UART logging/control path.
- Bluetooth: HCI and related support path.

## User-Facing Integration

- Devices surface through `/dev` nodes.
- Driver status surfaces through `/proc/drivers`.
- Boot emits `KLOG_EVENT_DRIVER_PROBE_RESULT` per registered driver and a
  `KLOG_EVENT_DRIVER_SUMMARY` line (`ready/absent/deferred/unsupported/failed`)
  plus smoke tag `DRIVER_SUMMARY_OK` — see [`KLOG.md`](KLOG.md).

## Strengths

- Driver lifecycle is now easier to reason about due to unified bootstrap.
- Menuconfig-gated init reduces accidental startup of disabled hardware paths.
- Registry introspection improves runtime debugging and bring-up visibility.

## Audio (SB16 / Adlib)

- Sources: `sound/sound_blaster.c`, `sound/adlib.c`.
- Facade: `includes/ir0/sound_blaster.h`; glue: `kernel/lib/audio_backend.c` → `/dev/audio` in `fs/devfs.c`.
- Successful SB16 DSP probe emits `klog_smoke("SB16_DSP_OK")` and logs DSP version.
- **Playback (2026-09-01):** `sb16_play_pcm()` copies PCM into static ping-pong buffers (8192 B max, identity-mapped). DMA is programmed **before** `SB16_DSP_PLAY_8BIT` (0x14). Buffers are never `kfree`'d while DMA may still reference them (fixes Doom-class UAF on `/dev/audio` writes).
- **IRQ:** PIC IRQ **5** → `sb16_irq_handler()` in `arch/x86-64/sources/interrupt/isr_handlers.c` (ack DSP data port, clears `sb16_hw_playing`). Registered with `resource_register_irq(5, "sb16")` on successful probe.
- QEMU 8+ needs an audiodev before the ISA device.
- Interactive (`make run` / Doom GUI): audio is **opt-in** with `IR0_AUDIO=1`
  (`IR0_AUDIO=1 make run`). Default backend is PulseAudio; override with
  `QEMU_AUDIO_BACKEND=alsa|sdl|pipewire|none`.
- Automated/host runs leave audio off by default, so CI does not require a mixer.
- Gate is **SB16 DSP detect**; Adlib may still report ABSENT on some QEMU builds
  (logged as note, not a fail).
