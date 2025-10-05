# IR0 Hardware Drivers

This directory contains all hardware drivers for the IR0 kernel, organized by device category.

## Driver Categories

### Input/Output (`IO/`)
- **PS/2 Keyboard** (`ps2.c/h`) - Complete PS/2 keyboard driver with circular buffer
- **PS/2 Mouse** (`ps2_mouse.c/h`) - Advanced mouse driver with 3/5 button + scroll wheel support

### Audio (`audio/`)
- **Sound Blaster 16** (`sound_blaster.c/h`) - Complete SB16 driver with DMA support
  - 8-bit/16-bit audio playback
  - Mono/Stereo support
  - Volume control (master and PCM)
  - Sample rate configuration
  - DMA channels 1 (8-bit) and 5 (16-bit)
  - IRQ 5 interrupt handling

### DMA Controller (`dma/`)
- **DMA Controller** (`dma.c/h`) - 8-channel DMA controller
  - Channels 0-7 support
  - 8-bit and 16-bit transfer modes
  - Audio integration for Sound Blaster

### Storage (`storage/`)
- **ATA/IDE** (`ata.c/h`) - Complete ATA/IDE hard disk and CD-ROM driver
  - Primary and secondary IDE channels
  - Master/slave device support
  - Sector-based read/write operations
  - CD-ROM support

### Video (`video/`)
- **VBE Graphics** (`vbe.c/h`) - VESA BIOS Extensions graphics driver
  - Framebuffer access
  - Multiple video modes
  - Graphics primitives support

### Serial Communication (`serial/`)
- **Serial Ports** (`serial.c/h`) - COM1/COM2 serial driver
  - Debug output support
  - Configurable baud rates
  - Interrupt-driven I/O
  - Kernel debugging integration

### Timer Systems (`timer/`)
- **PIT** (`pit/pit.c`) - Programmable Interval Timer
- **RTC** (`rtc/rtc.c`) - Real Time Clock
- **HPET** (`hpet/hpet.c`) - High Precision Event Timer
- **LAPIC** (`lapic/lapic.c`) - Local APIC Timer
- **Clock System** (`clock_system.c`) - Unified timer abstraction
- **Best Clock** (`best_clock.c`) - Automatic timer selection

## Features by Driver

### PS/2 Keyboard
- ✅ Complete scancode translation
- ✅ Circular buffer for key events
- ✅ Interrupt-driven input (IRQ 1)
- ✅ Modifier key support (Shift, Ctrl, Alt)
- ✅ Special key handling (F-keys, arrows)

### PS/2 Mouse
- ✅ Mouse type detection (standard/wheel/5-button)
- ✅ 3-button support (left, right, middle)
- ✅ 5-button support (button4, button5)
- ✅ Scroll wheel support
- ✅ Configurable sample rate and resolution
- ✅ Movement tracking with overflow detection
- ✅ Interrupt handling (IRQ 12)

### Sound Blaster 16
- ✅ DSP version detection and validation
- ✅ 8-bit and 16-bit audio formats
- ✅ Mono and stereo playback
- ✅ Sample rate configuration (8kHz - 44.1kHz)
- ✅ DMA-based audio transfer
- ✅ Volume control (master and PCM)
- ✅ Audio sample management
- ✅ Playback control (play, stop, pause, resume)

### ATA/IDE Storage
- ✅ Primary and secondary IDE channels
- ✅ Master and slave device detection
- ✅ Hard disk and CD-ROM support
- ✅ 28-bit LBA addressing
- ✅ Sector-based I/O operations
- ✅ Device identification and capabilities

### VBE Graphics
- ✅ VESA BIOS Extensions interface
- ✅ Multiple video mode support
- ✅ Linear framebuffer access
- ✅ Pixel format detection
- ✅ Graphics mode switching

### Serial Communication
- ✅ COM1 and COM2 port support
- ✅ Configurable baud rates (9600-115200)
- ✅ 8N1 configuration (8 data, no parity, 1 stop)
- ✅ Interrupt-driven transmission
- ✅ Debug output integration
- ✅ Kernel logging support

### Timer Systems
- ✅ Multiple timer source support
- ✅ Unified clock abstraction
- ✅ Automatic best timer selection
- ✅ Scheduler integration
- ✅ High-precision timing
- ✅ System uptime tracking

## Driver Architecture

### Initialization
All drivers are initialized during kernel boot in `kernel/kernel.c`:
```c
// Driver initialization sequence
ps2_init();           // PS/2 controller
keyboard_init();      // PS/2 keyboard
ps2_mouse_init();     // PS/2 mouse
sb16_init();          // Sound Blaster 16
ata_init();           // ATA/IDE storage
clock_system_init();  // Timer systems
```

### Interrupt Integration
- IRQ 1: PS/2 Keyboard
- IRQ 5: Sound Blaster 16
- IRQ 12: PS/2 Mouse
- IRQ 14: Primary IDE channel
- IRQ 15: Secondary IDE channel
- Timer IRQ: System timer (PIT/HPET)

### Memory Management
- DMA buffers for audio playback
- Circular buffers for input devices
- Memory-mapped I/O for some devices
- Proper cleanup and resource management

## Current Status

### ✅ Fully Functional
- PS/2 Keyboard and Mouse
- Serial communication
- ATA/IDE storage
- Timer systems
- Basic VBE graphics

### ✅ Functional with Limitations
- Sound Blaster 16 (driver complete, needs testing)
- DMA controller (basic functionality)

### 🔄 In Development
- USB support framework
- Advanced graphics features
- Network interface drivers
- Additional audio device support

### ❌ Not Implemented
- USB drivers
- Network drivers (Ethernet, WiFi)
- Advanced graphics (3D acceleration)
- SATA/AHCI storage
- Modern audio (HD Audio)

## Build Integration

Drivers are automatically built as part of the kernel:
- Compiled with kernel-specific flags
- Integrated into main kernel binary
- No dynamic loading (monolithic kernel)
- Architecture-specific optimizations

## Testing

Driver functionality can be tested through:
- Shell commands (keyboard input, file operations)
- Audio playback commands (when implemented)
- Mouse movement detection
- Serial debug output
- Timer-based operations