# IR0 Kernel Driver Subsystem

## Overview

The driver subsystem provides hardware abstraction and device management. IR0 implements a driver registry system that supports multi-language drivers (C, C++, Rust) and provides a unified interface for device initialization, control, and interaction.

## Architecture

The driver subsystem consists of:

1. **Driver Registry** - Centralized driver management and registration
2. **Device Drivers** - Hardware-specific implementations
3. **Multi-Language Support** - C, C++, and Rust driver support
4. **Device Access** - Integration with `/dev` filesystem

## Driver Registry

### Overview

The driver registry maintains a list of all loaded drivers with metadata (name, version, language).

### Location

- Header: `includes/ir0/driver.h`
- Implementation: `kernel/driver_registry.c`

### Driver Structure

```c
struct ir0_driver {
    const char *name;           /* Driver name */
    const char *version;        /* Driver version string */
    driver_language_t language; /* C, CXX, or RUST */
    void *driver_data;          /* Driver-specific data */
    struct ir0_driver *next;    /* Linked list pointer */
};
```

### Operations

```c
void ir0_driver_registry_init(void);
int ir0_driver_register(struct ir0_driver *driver);
struct ir0_driver *ir0_driver_find(const char *name);
```

**Registration:**
- Drivers register themselves during kernel initialization
- Registry maintains linked list of all drivers
- Drivers can be queried by name

## Driver Categories

### Input Devices

#### PS/2 Controller (`drivers/IO/ps2.c`)

Low-level PS/2 controller interface:
- Initializes PS/2 ports
- Handles communication protocol
- Supports keyboard and mouse

#### PS/2 Keyboard (`drivers/IO/ps2.c`, `interrupt/arch/keyboard.c`)

Keyboard input handling:
- Reads scan codes from PS/2 keyboard
- Converts scan codes to ASCII
- Maintains keyboard buffer
- IRQ 1 handler processes key presses
- Accessed via `/dev/console` read operations

#### PS/2 Mouse (`drivers/IO/ps2_mouse.c`)

Mouse input handling:
- Tracks mouse movement (x, y coordinates)
- Monitors button states
- Configurable sensitivity
- Accessed via `/dev/mouse` (read, IOCTL)

### Output Devices

#### VGA Display (`drivers/video/typewriter.c`)

Text mode display:
- 80x25 character display
- Color support (16 foreground, 16 background)
- Cursor management
- Used for kernel console output

#### VBE Graphics (`drivers/video/vbe.c`)

VESA BIOS Extensions:
- Graphics mode support
- Frame buffer access
- Mode switching

### Storage Devices

#### ATA/IDE Driver (`drivers/storage/ata.c`)

Disk storage controller:
- PIO mode 0-4 support
- Master/slave drive support
- Sector read/write operations
- Disk geometry detection
- Partition table reading (MBR/GPT)
- Accessed via `/dev/disk` (read, write, IOCTL)

**Operations:**
- `ata_init()` - Initialize ATA controller
- `ata_read_sectors()` - Read disk sectors
- `ata_write_sectors()` - Write disk sectors
- `ata_is_available()` - Check if disk is present
- `ata_drive_present()` - Check specific drive

### Audio Devices

#### Sound Blaster 16 (`drivers/audio/sound_blaster.c`)

Sound card support:
- DMA-based audio playback
- Volume control
- Speaker on/off
- Accessed via `/dev/audio` (write, IOCTL)

#### Adlib OPL2 (`drivers/audio/adlib.c`)

FM synthesis chip:
- MIDI-like music synthesis
- Register-based control

#### PC Speaker (`drivers/IO/pc_speaker.c`)

System speaker:
- Simple beep generation
- Frequency control

### Network Devices

#### RTL8139 (`drivers/net/rtl8139.c`)

Ethernet network card:
- 10/100 Mbps Ethernet
- Packet send/receive
- DMA-based transfers
- IRQ 11 handler
- Integration with network stack

#### E1000 (`drivers/net/e1000.c`)

Intel Gigabit Ethernet (placeholder):
- Structure defined but not fully implemented

### Timer Devices

#### PIT (Programmable Interval Timer) (`drivers/timer/pit/pit.c`)

Legacy timer:
- 1.193182 MHz base frequency
- IRQ 0 (system timer)
- Used for system uptime

#### RTC (Real-Time Clock) (`drivers/timer/rtc/rtc.c`)

CMOS real-time clock:
- Date/time reading
- Battery-backed time

#### HPET (High Precision Event Timer) (`drivers/timer/hpet/hpet.c`)

Modern high-precision timer:
- Sub-microsecond resolution
- Multiple timer channels

#### LAPIC Timer (`drivers/timer/lapic/lapic.c`)

Local APIC timer:
- Per-CPU timer
- Used for SMP timing (future)

#### Clock System (`drivers/timer/clock_system.c`)

Unified clock interface:
- Detects available timers (PIT, HPET, LAPIC)
- Provides unified time functions
- System uptime calculation

### Serial Communication

#### Serial Port (`drivers/serial/serial.c`)

UART serial port:
- COM1 (0x3F8) support
- 38400 baud default
- Used for kernel debugging
- Serial logging for all errors

### DMA Controller

#### DMA (`drivers/dma/dma.c`)

Direct Memory Access:
- Channel allocation
- Memory-to-device transfers
- Used by audio and network drivers

## Multi-Language Driver Support

### Overview

IR0 supports drivers written in C, C++, and Rust through FFI (Foreign Function Interface).

### C Drivers

Standard kernel drivers written in C:
- Full access to kernel APIs
- Direct memory access
- No special requirements

### C++ Drivers

C++ drivers use compatibility layer:
- Location: `cpp/runtime/compat.cpp`
- Wraps C++ new/delete with kernel allocator
- Provides C++ runtime support

### Rust Drivers

Rust drivers use FFI bindings:
- Location: `rust/ffi/kernel.rs`
- Provides Rust bindings to kernel APIs
- Safe wrapper functions

### Driver Registration

All drivers (regardless of language) register via same interface:
```c
struct ir0_driver driver = {
    .name = "example_driver",
    .version = "1.0.0",
    .language = DRIVER_LANGUAGE_C,
    .driver_data = NULL
};
ir0_driver_register(&driver);
```

## Driver Initialization

### Boot Sequence

Drivers are initialized in `kernel/main.c` via `init_all_drivers()`:

1. PS/2 controller and keyboard
2. PS/2 mouse
3. PC Speaker
4. Audio drivers (Sound Blaster, Adlib)
5. Storage (ATA)
6. Network stack (includes RTL8139)

### Initialization Order

Critical drivers initialized first:
- Serial (for debugging)
- Timers (for system time)
- Input devices (keyboard/mouse)
- Storage (for filesystem)
- Network (after storage)

## Device Access

### /dev Filesystem

Devices are accessed via `/dev` filesystem:

- `/dev/console` - Keyboard input, VGA output
- `/dev/mouse` - Mouse state and control
- `/dev/audio` - Audio playback
- `/dev/net` - Network operations
- `/dev/disk` - Disk sector access
- `/dev/null`, `/dev/zero` - Special devices

### IOCTL Interface

Devices support IOCTL for control operations:

- **Audio:** Volume control, play/stop
- **Mouse:** Get state, set sensitivity
- **Network:** Send ping, configure IP
- **Disk:** Get geometry, read/write sector

## Error Handling

All driver operations:
- Return error codes on failure
- Log errors to serial console
- Provide detailed error information

**Common Error Codes:**
- `0` - Success
- `-1` - Generic error
- `-ENODEV` - Device not found
- `-EIO` - I/O error
- `-EINVAL` - Invalid parameter

## Driver Status

Drivers can be queried via:
- `/proc/drivers` - Lists all registered drivers
- Driver registry API - Programmatic query

## Implementation Notes

- **No Hot-Plugging:** All devices initialized at boot
- **Static Configuration:** No dynamic device discovery
- **Legacy Hardware:** Focus on older hardware (SB16, RTL8139, PS/2)
- **No Power Management:** Devices always powered
- **Single Device Type:** One driver per device type (no multi-device support)

---

# Subsistema de Drivers del Kernel IR0

## Resumen

El subsistema de drivers proporciona abstracción de hardware y gestión de dispositivos. IR0 implementa un sistema de registro de drivers que soporta drivers multi-lenguaje (C, C++, Rust) y proporciona una interfaz unificada para inicialización, control e interacción de dispositivos.

## Arquitectura

El subsistema de drivers consta de:

1. **Registro de Drivers** - Gestión y registro centralizado de drivers
2. **Drivers de Dispositivos** - Implementaciones específicas de hardware
3. **Soporte Multi-Lenguaje** - Soporte de drivers C, C++ y Rust
4. **Acceso a Dispositivos** - Integración con sistema de archivos `/dev`

## Registro de Drivers

### Resumen

El registro de drivers mantiene una lista de todos los drivers cargados con metadatos (nombre, versión, lenguaje).

### Ubicación

- Header: `includes/ir0/driver.h`
- Implementación: `kernel/driver_registry.c`

### Estructura de Driver

```c
struct ir0_driver {
    const char *name;           /* Nombre del driver */
    const char *version;        /* Cadena de versión del driver */
    driver_language_t language; /* C, CXX, o RUST */
    void *driver_data;          /* Datos específicos del driver */
    struct ir0_driver *next;    /* Puntero de lista enlazada */
};
```

### Operaciones

```c
void ir0_driver_registry_init(void);
int ir0_driver_register(struct ir0_driver *driver);
struct ir0_driver *ir0_driver_find(const char *name);
```

**Registro:**
- Los drivers se registran durante la inicialización del kernel
- El registro mantiene lista enlazada de todos los drivers
- Los drivers pueden consultarse por nombre

## Categorías de Drivers

### Dispositivos de Entrada

#### Controlador PS/2 (`drivers/IO/ps2.c`)

Interfaz de bajo nivel del controlador PS/2:
- Inicializa puertos PS/2
- Maneja protocolo de comunicación
- Soporta teclado y mouse

#### Teclado PS/2 (`drivers/IO/ps2.c`, `interrupt/arch/keyboard.c`)

Manejo de entrada de teclado:
- Lee códigos de escaneo del teclado PS/2
- Convierte códigos de escaneo a ASCII
- Mantiene buffer de teclado
- Handler IRQ 1 procesa pulsaciones de tecla
- Accedido via operaciones read de `/dev/console`

#### Mouse PS/2 (`drivers/IO/ps2_mouse.c`)

Manejo de entrada de mouse:
- Rastrea movimiento del mouse (coordenadas x, y)
- Monitorea estados de botones
- Sensibilidad configurable
- Accedido via `/dev/mouse` (read, IOCTL)

### Dispositivos de Salida

#### Pantalla VGA (`drivers/video/typewriter.c`)

Pantalla en modo texto:
- Pantalla de caracteres 80x25
- Soporte de color (16 primer plano, 16 fondo)
- Gestión de cursor
- Usado para salida de consola del kernel

#### Gráficos VBE (`drivers/video/vbe.c`)

Extensiones VESA BIOS:
- Soporte de modo gráfico
- Acceso a frame buffer
- Cambio de modo

### Dispositivos de Almacenamiento

#### Driver ATA/IDE (`drivers/storage/ata.c`)

Controlador de almacenamiento en disco:
- Soporte de modo PIO 0-4
- Soporte de drive maestro/esclavo
- Operaciones de lectura/escritura de sectores
- Detección de geometría de disco
- Lectura de tabla de particiones (MBR/GPT)
- Accedido via `/dev/disk` (read, write, IOCTL)

**Operaciones:**
- `ata_init()` - Inicializar controlador ATA
- `ata_read_sectors()` - Leer sectores de disco
- `ata_write_sectors()` - Escribir sectores de disco
- `ata_is_available()` - Verificar si disco está presente
- `ata_drive_present()` - Verificar drive específico

### Dispositivos de Audio

#### Sound Blaster 16 (`drivers/audio/sound_blaster.c`)

Soporte de tarjeta de sonido:
- Reproducción de audio basada en DMA
- Control de volumen
- Encendido/apagado de altavoz
- Accedido via `/dev/audio` (write, IOCTL)

#### Adlib OPL2 (`drivers/audio/adlib.c`)

Chip de síntesis FM:
- Síntesis de música tipo MIDI
- Control basado en registros

#### PC Speaker (`drivers/IO/pc_speaker.c`)

Altavoz del sistema:
- Generación simple de pitidos
- Control de frecuencia

### Dispositivos de Red

#### RTL8139 (`drivers/net/rtl8139.c`)

Tarjeta de red Ethernet:
- Ethernet 10/100 Mbps
- Envío/recepción de paquetes
- Transferencias basadas en DMA
- Handler IRQ 11
- Integración con stack de red

#### E1000 (`drivers/net/e1000.c`)

Intel Gigabit Ethernet (placeholder):
- Estructura definida pero no completamente implementada

### Dispositivos de Timer

#### PIT (Programmable Interval Timer) (`drivers/timer/pit/pit.c`)

Timer legado:
- Frecuencia base 1.193182 MHz
- IRQ 0 (timer del sistema)
- Usado para tiempo de actividad del sistema

#### RTC (Real-Time Clock) (`drivers/timer/rtc/rtc.c`)

Reloj de tiempo real CMOS:
- Lectura de fecha/hora
- Tiempo respaldado por batería

#### HPET (High Precision Event Timer) (`drivers/timer/hpet/hpet.c`)

Timer moderno de alta precisión:
- Resolución sub-microsegundo
- Múltiples canales de timer

#### Timer LAPIC (`drivers/timer/lapic/lapic.c`)

Timer Local APIC:
- Timer por CPU
- Usado para temporización SMP (futuro)

#### Sistema de Reloj (`drivers/timer/clock_system.c`)

Interfaz de reloj unificada:
- Detecta timers disponibles (PIT, HPET, LAPIC)
- Proporciona funciones de tiempo unificadas
- Cálculo de tiempo de actividad del sistema

### Comunicación Serial

#### Puerto Serial (`drivers/serial/serial.c`)

Puerto serial UART:
- Soporte COM1 (0x3F8)
- 38400 baud por defecto
- Usado para debugging del kernel
- Registro serial para todos los errores

### Controlador DMA

#### DMA (`drivers/dma/dma.c`)

Acceso Directo a Memoria:
- Asignación de canales
- Transferencias memoria-a-dispositivo
- Usado por drivers de audio y red

## Soporte de Drivers Multi-Lenguaje

### Resumen

IR0 soporta drivers escritos en C, C++ y Rust a través de FFI (Foreign Function Interface).

### Drivers C

Drivers estándar del kernel escritos en C:
- Acceso completo a APIs del kernel
- Acceso directo a memoria
- Sin requisitos especiales

### Drivers C++

Drivers C++ usan capa de compatibilidad:
- Ubicación: `cpp/runtime/compat.cpp`
- Envuelve new/delete C++ con asignador del kernel
- Proporciona soporte de runtime C++

### Drivers Rust

Drivers Rust usan bindings FFI:
- Ubicación: `rust/ffi/kernel.rs`
- Proporciona bindings Rust a APIs del kernel
- Funciones wrapper seguras

### Registro de Drivers

Todos los drivers (independientemente del lenguaje) se registran via la misma interfaz:
```c
struct ir0_driver driver = {
    .name = "example_driver",
    .version = "1.0.0",
    .language = DRIVER_LANGUAGE_C,
    .driver_data = NULL
};
ir0_driver_register(&driver);
```

## Inicialización de Drivers

### Secuencia de Arranque

Los drivers se inicializan en `kernel/main.c` via `init_all_drivers()`:

1. Controlador PS/2 y teclado
2. Mouse PS/2
3. PC Speaker
4. Drivers de audio (Sound Blaster, Adlib)
5. Almacenamiento (ATA)
6. Stack de red (incluye RTL8139)

### Orden de Inicialización

Drivers críticos inicializados primero:
- Serial (para debugging)
- Timers (para tiempo del sistema)
- Dispositivos de entrada (teclado/mouse)
- Almacenamiento (para sistema de archivos)
- Red (después de almacenamiento)

## Acceso a Dispositivos

### Sistema de Archivos /dev

Los dispositivos se acceden via sistema de archivos `/dev`:

- `/dev/console` - Entrada de teclado, salida VGA
- `/dev/mouse` - Estado y control de mouse
- `/dev/audio` - Reproducción de audio
- `/dev/net` - Operaciones de red
- `/dev/disk` - Acceso a sectores de disco
- `/dev/null`, `/dev/zero` - Dispositivos especiales

### Interfaz IOCTL

Los dispositivos soportan IOCTL para operaciones de control:

- **Audio:** Control de volumen, reproducir/detener
- **Mouse:** Obtener estado, establecer sensibilidad
- **Red:** Enviar ping, configurar IP
- **Disco:** Obtener geometría, leer/escribir sector

## Manejo de Errores

Todas las operaciones de driver:
- Retornan códigos de error en fallo
- Registran errores en consola serial
- Proporcionan información detallada de error

**Códigos de Error Comunes:**
- `0` - Éxito
- `-1` - Error genérico
- `-ENODEV` - Dispositivo no encontrado
- `-EIO` - Error de I/O
- `-EINVAL` - Parámetro inválido

## Estado de Drivers

Los drivers pueden consultarse via:
- `/proc/drivers` - Lista todos los drivers registrados
- API de registro de drivers - Consulta programática

## Notas de Implementación

- **Sin Hot-Plugging:** Todos los dispositivos inicializados al arrancar
- **Configuración Estática:** Sin descubrimiento dinámico de dispositivos
- **Hardware Legado:** Enfoque en hardware antiguo (SB16, RTL8139, PS/2)
- **Sin Gestión de Energía:** Dispositivos siempre encendidos
- **Tipo de Dispositivo Único:** Un driver por tipo de dispositivo (sin soporte multi-dispositivo)

