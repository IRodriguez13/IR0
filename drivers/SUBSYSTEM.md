# Drivers Subsystem

Hardware device drivers for IR0 kernel.

## Architecture

```
drivers/
├── IO/              - Input/Output devices
│   ├── ps2.c/h      - PS/2 controller
│   └── ps2_mouse.c  - PS/2 mouse driver
├── audio/           - Audio devices
│   └── sound_blaster.c - Sound Blaster 16
├── dma/             - DMA controller
│   └── dma.c/h
├── serial/          - Serial port
│   └── serial.c/h
├── storage/         - Storage devices
│   └── ata.c/h      - ATA/IDE disk driver
├── timer/           - Timer devices
│   ├── pit/         - Programmable Interval Timer
│   ├── rtc/         - Real-Time Clock
│   ├── hpet/        - High Precision Event Timer
│   ├── lapic/       - Local APIC timer
│   └── clock_system.c - Unified clock interface
└── video/           - Video devices
    └── vbe.c/h      - VESA BIOS Extensions
```

## Driver Categories

### Input Devices
- **PS/2 Controller**: Keyboard and mouse interface
- **PS/2 Mouse**: Mouse input handling

### Output Devices
- **VGA Text Mode**: 80x25 text display (built-in)
- **VBE**: VESA graphics mode support

### Storage Devices
- **ATA/IDE**: Hard disk and CD-ROM support
- Supports PIO mode 0-4
- Master/Slave configuration

### Timer Devices
- **PIT**: Legacy 8253/8254 timer (1.193182 MHz)
- **RTC**: CMOS real-time clock
- **HPET**: Modern high-precision timer
- **LAPIC**: Per-CPU timer for SMP

### Audio Devices
- **Sound Blaster 16**: Legacy audio card
- DMA-based audio playback

## Public Interfaces

### Serial Port (`serial.h`)
```c
void serial_init(void);
void serial_print(const char *str);
void serial_print_hex32(uint32_t value);
void serial_print_hex64(uint64_t value);
char serial_read_char(void);
```

### Timer (`clock_system.h`)
```c
void clock_system_init(void);
uint64_t get_system_ticks(void);
uint64_t get_uptime_ms(void);
void sleep_ms(uint64_t ms);
```

### Storage (`ata.h`)
```c
void ata_init(void);
int ata_read_sector(uint8_t drive, uint32_t lba, void *buffer);
int ata_write_sector(uint8_t drive, uint32_t lba, const void *buffer);
```

### PS/2 (`ps2.h`)
```c
void ps2_init(void);
void keyboard_init(void);
void ps2_mouse_init(void);
char keyboard_getchar(void);
```

## Driver Initialization Order

1. **Serial** - For early debug output
2. **PIT** - For basic timing
3. **PS/2** - For keyboard input
4. **ATA** - For disk access
5. **VBE** - For graphics (optional)
6. **Audio** - For sound (optional)

## Interrupt Handling

Drivers register interrupt handlers via the IDT:
- IRQ 0: PIT timer
- IRQ 1: Keyboard
- IRQ 12: PS/2 mouse
- IRQ 14: Primary ATA
- IRQ 15: Secondary ATA

## DMA Usage

Devices using DMA:
- Sound Blaster 16 (channel 1)
- Floppy disk (channel 2)

## Port I/O

All drivers use `inb()`, `outb()`, `inw()`, `outw()` for port access.
Port ranges are documented in each driver's header file.

## Future Work

- [ ] USB support (UHCI, OHCI, EHCI, xHCI)
- [ ] Network cards (RTL8139, E1000)
- [ ] SATA/AHCI support
- [ ] NVMe support
- [ ] GPU drivers (basic framebuffer)
- [ ] Audio: AC'97, HDA
