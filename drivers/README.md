# Drivers Subsystem

## English

### Overview
The Drivers Subsystem provides hardware abstraction and device management for the IR0 kernel. It includes timer drivers (PIT, HPET, LAPIC), storage drivers, and I/O drivers with a modular architecture that supports both x86-32 and x86-64 platforms.

### Key Components

#### 1. Timer Drivers (`timer/`)
- **Purpose**: System timing and scheduling support
- **Features**:
  - **PIT (Programmable Interval Timer)**: Legacy timer for basic timing
  - **HPET (High Precision Event Timer)**: High-resolution timing
  - **LAPIC (Local Advanced Programmable Interrupt Controller)**: Modern timer for multi-core systems
  - **Clock System**: Automatic timer selection and management
  - **Best Clock Detection**: Intelligent timer selection based on hardware

#### 2. Storage Drivers (`storage/`)
- **Purpose**: Storage device management and I/O
- **Features**:
  - **ATA/IDE Support**: Hard disk and optical drive support
  - **SATA Support**: Modern storage interface
  - **Block Device Interface**: Unified storage access
  - **Partition Management**: Disk partitioning support
  - **File System Integration**: Direct filesystem access

#### 3. I/O Drivers (`IO/`)
- **Purpose**: Input/Output device management
- **Features**:
  - **Serial Port Support**: COM1/COM2 communication
  - **Parallel Port Support**: LPT1/LPT2 printing
  - **USB Support**: Universal Serial Bus devices
  - **PS/2 Support**: Keyboard and mouse
  - **VGA Support**: Video output and display

### Timer System

#### PIT (Programmable Interval Timer)
```c
// PIT Configuration
#define PIT_FREQ 1193180  // Base frequency
#define PIT_CHANNEL0 0x40 // Channel 0 port
#define PIT_COMMAND 0x43  // Command port

// Initialize PIT
void init_PIT(uint32_t frequency) {
    uint32_t divisor = PIT_FREQ / frequency;
    outb(PIT_COMMAND, 0x36);  // Mode 3, 16-bit
    outb(PIT_CHANNEL0, divisor & 0xFF);
    outb(PIT_CHANNEL0, (divisor >> 8) & 0xFF);
}
```

#### HPET (High Precision Event Timer)
```c
// HPET Configuration
struct hpet_config {
    uint64_t base_address;     // HPET base address
    uint32_t capabilities;     // HPET capabilities
    uint32_t period;           // Timer period in femtoseconds
    bool enabled;              // Timer enabled
};

// Initialize HPET
int init_HPET(void) {
    // Find HPET in ACPI tables
    struct acpi_hpet* hpet = find_hpet();
    if (!hpet) return -1;
    
    // Configure HPET
    configure_hpet(hpet);
    return 0;
}
```

#### LAPIC (Local APIC)
```c
// LAPIC Configuration
#define LAPIC_BASE 0xFEE00000
#define LAPIC_ID 0x20
#define LAPIC_VER 0x30
#define LAPIC_TPR 0x80
#define LAPIC_EOI 0x0B0
#define LAPIC_SIVR 0x0F0
#define LAPIC_ICR_LOW 0x300
#define LAPIC_ICR_HIGH 0x310

// Initialize LAPIC
void init_LAPIC(void) {
    // Enable LAPIC
    write_lapic_register(LAPIC_SIVR, 0x100 | 0xFF);
    
    // Set up timer
    setup_lapic_timer();
}
```

### Clock System

#### Automatic Timer Selection
```c
// Clock detection and selection
typedef enum {
    CLOCK_TIMER_PIT,    // Legacy PIT
    CLOCK_TIMER_HPET,   // High Precision Event Timer
    CLOCK_TIMER_LAPIC   // Local APIC Timer
} clock_timer_t;

// Detect best available timer
clock_timer_t detect_best_clock(void) {
    if (is_lapic_available()) {
        return CLOCK_TIMER_LAPIC;
    } else if (is_hpet_available()) {
        return CLOCK_TIMER_HPET;
    } else {
        return CLOCK_TIMER_PIT;
    }
}
```

#### Clock System Management
```c
// Clock system state
struct clock_system {
    clock_timer_t active_timer;
    uint32_t frequency;
    uint64_t ticks;
    bool initialized;
    bool pit_enabled;
    bool hpet_enabled;
    bool lapic_enabled;
};

// Initialize clock system
int clock_system_init(void) {
    // Detect best timer
    clock_timer_t best = detect_best_clock();
    
    // Initialize selected timer
    switch (best) {
        case CLOCK_TIMER_LAPIC:
            return init_LAPIC();
        case CLOCK_TIMER_HPET:
            return init_HPET();
        case CLOCK_TIMER_PIT:
            return init_PIT(1000);  // 1kHz
        default:
            return -1;
    }
}
```

### Storage System

#### Block Device Interface
```c
// Block device structure
struct block_device {
    char name[32];
    uint64_t size;
    uint32_t block_size;
    uint32_t num_blocks;
    int (*read)(struct block_device*, uint64_t, void*, size_t);
    int (*write)(struct block_device*, uint64_t, const void*, size_t);
    void* private_data;
};

// Register block device
int register_block_device(struct block_device* device) {
    // Add to device list
    return add_block_device(device);
}

// Read from block device
int block_read(struct block_device* device, uint64_t offset, void* buffer, size_t size) {
    return device->read(device, offset, buffer, size);
}
```

#### ATA/IDE Support
```c
// ATA device structure
struct ata_device {
    uint16_t base_port;
    uint16_t control_port;
    bool is_master;
    bool is_present;
    uint64_t capacity;
    uint32_t block_size;
};

// Initialize ATA device
int init_ATA_device(struct ata_device* device) {
    // Detect device
    if (!ata_detect_device(device)) {
        return -1;
    }
    
    // Get device information
    ata_identify_device(device);
    
    return 0;
}
```

### I/O System

#### Serial Port Support
```c
// Serial port configuration
struct serial_port {
    uint16_t base_port;
    uint32_t baud_rate;
    uint8_t data_bits;
    uint8_t stop_bits;
    uint8_t parity;
    bool enabled;
};

// Initialize serial port
int init_serial_port(struct serial_port* port) {
    // Configure UART
    outb(port->base_port + 3, 0x80);  // Enable DLAB
    
    // Set baud rate
    uint16_t divisor = 115200 / port->baud_rate;
    outb(port->base_port, divisor & 0xFF);
    outb(port->base_port + 1, (divisor >> 8) & 0xFF);
    
    // Configure line control
    outb(port->base_port + 3, 0x03);  // 8N1
    
    return 0;
}
```

#### VGA Support
```c
// VGA configuration
struct vga_config {
    uint16_t width;
    uint16_t height;
    uint8_t depth;
    uint8_t* framebuffer;
    bool text_mode;
};

// Initialize VGA
int init_VGA(struct vga_config* config) {
    // Set video mode
    if (config->text_mode) {
        set_text_mode();
    } else {
        set_graphics_mode(config->width, config->height, config->depth);
    }
    
    // Initialize framebuffer
    config->framebuffer = (uint8_t*)0xB8000;
    
    return 0;
}
```

### Driver Architecture

#### Driver Registration
```c
// Driver structure
struct driver {
    char name[32];
    uint32_t version;
    int (*init)(void*);
    int (*cleanup)(void);
    void* private_data;
};

// Register driver
int register_driver(struct driver* driver) {
    // Add to driver list
    return add_driver(driver);
}

// Initialize all drivers
int init_all_drivers(void) {
    // Initialize timer drivers
    if (clock_system_init() != 0) {
        return -1;
    }
    
    // Initialize storage drivers
    if (init_storage_drivers() != 0) {
        return -1;
    }
    
    // Initialize I/O drivers
    if (init_io_drivers() != 0) {
        return -1;
    }
    
    return 0;
}
```

### Performance Characteristics

#### Timer Performance
- **PIT Latency**: ~1ms resolution
- **HPET Latency**: ~1μs resolution
- **LAPIC Latency**: ~100ns resolution
- **Timer Overhead**: < 1% of CPU time

#### Storage Performance
- **ATA Read Speed**: Up to 100MB/s
- **ATA Write Speed**: Up to 80MB/s
- **SATA Read Speed**: Up to 600MB/s
- **SATA Write Speed**: Up to 500MB/s

#### I/O Performance
- **Serial Speed**: Up to 115200 baud
- **VGA Refresh**: 60Hz
- **USB Speed**: Up to 480Mbps (USB 2.0)

### Configuration

#### Driver Configuration
```c
struct driver_config {
    bool enable_pit;           // Enable PIT timer
    bool enable_hpet;          // Enable HPET timer
    bool enable_lapic;         // Enable LAPIC timer
    bool enable_ata;           // Enable ATA storage
    bool enable_sata;          // Enable SATA storage
    bool enable_serial;        // Enable serial ports
    bool enable_vga;           // Enable VGA output
    uint32_t timer_frequency;  // Timer frequency
};
```

#### Hardware Detection
```c
// Detect available hardware
struct hardware_info {
    bool has_pit;
    bool has_hpet;
    bool has_lapic;
    bool has_ata;
    bool has_sata;
    bool has_serial;
    bool has_vga;
    uint32_t num_cpus;
    uint64_t total_memory;
};
```

---

## Español

### Descripción General
El Subsistema de Drivers proporciona abstracción de hardware y gestión de dispositivos para el kernel IR0. Incluye drivers de timer (PIT, HPET, LAPIC), drivers de almacenamiento y drivers de I/O con una arquitectura modular que soporta plataformas x86-32 y x86-64.

### Componentes Principales

#### 1. Drivers de Timer (`timer/`)
- **Propósito**: Soporte de temporización del sistema y planificación
- **Características**:
  - **PIT (Programmable Interval Timer)**: Timer legacy para temporización básica
  - **HPET (High Precision Event Timer)**: Temporización de alta resolución
  - **LAPIC (Local Advanced Programmable Interrupt Controller)**: Timer moderno para sistemas multi-core
  - **Sistema de Reloj**: Selección y gestión automática de timers
  - **Detección del Mejor Reloj**: Selección inteligente de timer basada en hardware

#### 2. Drivers de Almacenamiento (`storage/`)
- **Propósito**: Gestión de dispositivos de almacenamiento e I/O
- **Características**:
  - **Soporte ATA/IDE**: Soporte para disco duro y unidad óptica
  - **Soporte SATA**: Interfaz de almacenamiento moderna
  - **Interfaz de Dispositivo de Bloque**: Acceso unificado a almacenamiento
  - **Gestión de Particiones**: Soporte para particionado de disco
  - **Integración de Sistema de Archivos**: Acceso directo al filesystem

#### 3. Drivers de I/O (`IO/`)
- **Propósito**: Gestión de dispositivos de entrada/salida
- **Características**:
  - **Soporte de Puerto Serial**: Comunicación COM1/COM2
  - **Soporte de Puerto Paralelo**: Impresión LPT1/LPT2
  - **Soporte USB**: Dispositivos Universal Serial Bus
  - **Soporte PS/2**: Teclado y ratón
  - **Soporte VGA**: Salida de video y pantalla

### Sistema de Timer

#### PIT (Programmable Interval Timer)
```c
// Configuración PIT
#define PIT_FREQ 1193180  // Frecuencia base
#define PIT_CHANNEL0 0x40 // Puerto del canal 0
#define PIT_COMMAND 0x43  // Puerto de comando

// Inicializar PIT
void init_PIT(uint32_t frequency) {
    uint32_t divisor = PIT_FREQ / frequency;
    outb(PIT_COMMAND, 0x36);  // Modo 3, 16-bit
    outb(PIT_CHANNEL0, divisor & 0xFF);
    outb(PIT_CHANNEL0, (divisor >> 8) & 0xFF);
}
```

#### HPET (High Precision Event Timer)
```c
// Configuración HPET
struct hpet_config {
    uint64_t base_address;     // Dirección base HPET
    uint32_t capabilities;     // Capacidades HPET
    uint32_t period;           // Período del timer en femtosegundos
    bool enabled;              // Timer habilitado
};

// Inicializar HPET
int init_HPET(void) {
    // Buscar HPET en tablas ACPI
    struct acpi_hpet* hpet = find_hpet();
    if (!hpet) return -1;
    
    // Configurar HPET
    configure_hpet(hpet);
    return 0;
}
```

#### LAPIC (Local APIC)
```c
// Configuración LAPIC
#define LAPIC_BASE 0xFEE00000
#define LAPIC_ID 0x20
#define LAPIC_VER 0x30
#define LAPIC_TPR 0x80
#define LAPIC_EOI 0x0B0
#define LAPIC_SIVR 0x0F0
#define LAPIC_ICR_LOW 0x300
#define LAPIC_ICR_HIGH 0x310

// Inicializar LAPIC
void init_LAPIC(void) {
    // Habilitar LAPIC
    write_lapic_register(LAPIC_SIVR, 0x100 | 0xFF);
    
    // Configurar timer
    setup_lapic_timer();
}
```

### Sistema de Reloj

#### Selección Automática de Timer
```c
// Detección y selección de reloj
typedef enum {
    CLOCK_TIMER_PIT,    // PIT legacy
    CLOCK_TIMER_HPET,   // High Precision Event Timer
    CLOCK_TIMER_LAPIC   // Local APIC Timer
} clock_timer_t;

// Detectar mejor timer disponible
clock_timer_t detect_best_clock(void) {
    if (is_lapic_available()) {
        return CLOCK_TIMER_LAPIC;
    } else if (is_hpet_available()) {
        return CLOCK_TIMER_HPET;
    } else {
        return CLOCK_TIMER_PIT;
    }
}
```

#### Gestión del Sistema de Reloj
```c
// Estado del sistema de reloj
struct clock_system {
    clock_timer_t active_timer;
    uint32_t frequency;
    uint64_t ticks;
    bool initialized;
    bool pit_enabled;
    bool hpet_enabled;
    bool lapic_enabled;
};

// Inicializar sistema de reloj
int clock_system_init(void) {
    // Detectar mejor timer
    clock_timer_t best = detect_best_clock();
    
    // Inicializar timer seleccionado
    switch (best) {
        case CLOCK_TIMER_LAPIC:
            return init_LAPIC();
        case CLOCK_TIMER_HPET:
            return init_HPET();
        case CLOCK_TIMER_PIT:
            return init_PIT(1000);  // 1kHz
        default:
            return -1;
    }
}
```

### Sistema de Almacenamiento

#### Interfaz de Dispositivo de Bloque
```c
// Estructura de dispositivo de bloque
struct block_device {
    char name[32];
    uint64_t size;
    uint32_t block_size;
    uint32_t num_blocks;
    int (*read)(struct block_device*, uint64_t, void*, size_t);
    int (*write)(struct block_device*, uint64_t, const void*, size_t);
    void* private_data;
};

// Registrar dispositivo de bloque
int register_block_device(struct block_device* device) {
    // Añadir a lista de dispositivos
    return add_block_device(device);
}

// Leer de dispositivo de bloque
int block_read(struct block_device* device, uint64_t offset, void* buffer, size_t size) {
    return device->read(device, offset, buffer, size);
}
```

#### Soporte ATA/IDE
```c
// Estructura de dispositivo ATA
struct ata_device {
    uint16_t base_port;
    uint16_t control_port;
    bool is_master;
    bool is_present;
    uint64_t capacity;
    uint32_t block_size;
};

// Inicializar dispositivo ATA
int init_ATA_device(struct ata_device* device) {
    // Detectar dispositivo
    if (!ata_detect_device(device)) {
        return -1;
    }
    
    // Obtener información del dispositivo
    ata_identify_device(device);
    
    return 0;
}
```

### Sistema de I/O

#### Soporte de Puerto Serial
```c
// Configuración de puerto serial
struct serial_port {
    uint16_t base_port;
    uint32_t baud_rate;
    uint8_t data_bits;
    uint8_t stop_bits;
    uint8_t parity;
    bool enabled;
};

// Inicializar puerto serial
int init_serial_port(struct serial_port* port) {
    // Configurar UART
    outb(port->base_port + 3, 0x80);  // Habilitar DLAB
    
    // Establecer velocidad de baudios
    uint16_t divisor = 115200 / port->baud_rate;
    outb(port->base_port, divisor & 0xFF);
    outb(port->base_port + 1, (divisor >> 8) & 0xFF);
    
    // Configurar control de línea
    outb(port->base_port + 3, 0x03);  // 8N1
    
    return 0;
}
```

#### Soporte VGA
```c
// Configuración VGA
struct vga_config {
    uint16_t width;
    uint16_t height;
    uint8_t depth;
    uint8_t* framebuffer;
    bool text_mode;
};

// Inicializar VGA
int init_VGA(struct vga_config* config) {
    // Establecer modo de video
    if (config->text_mode) {
        set_text_mode();
    } else {
        set_graphics_mode(config->width, config->height, config->depth);
    }
    
    // Inicializar framebuffer
    config->framebuffer = (uint8_t*)0xB8000;
    
    return 0;
}
```

### Arquitectura de Drivers

#### Registro de Drivers
```c
// Estructura de driver
struct driver {
    char name[32];
    uint32_t version;
    int (*init)(void*);
    int (*cleanup)(void);
    void* private_data;
};

// Registrar driver
int register_driver(struct driver* driver) {
    // Añadir a lista de drivers
    return add_driver(driver);
}

// Inicializar todos los drivers
int init_all_drivers(void) {
    // Inicializar drivers de timer
    if (clock_system_init() != 0) {
        return -1;
    }
    
    // Inicializar drivers de almacenamiento
    if (init_storage_drivers() != 0) {
        return -1;
    }
    
    // Inicializar drivers de I/O
    if (init_io_drivers() != 0) {
        return -1;
    }
    
    return 0;
}
```

### Características de Rendimiento

#### Rendimiento de Timer
- **Latencia PIT**: ~1ms resolución
- **Latencia HPET**: ~1μs resolución
- **Latencia LAPIC**: ~100ns resolución
- **Overhead de Timer**: < 1% del tiempo CPU

#### Rendimiento de Almacenamiento
- **Velocidad de Lectura ATA**: Hasta 100MB/s
- **Velocidad de Escritura ATA**: Hasta 80MB/s
- **Velocidad de Lectura SATA**: Hasta 600MB/s
- **Velocidad de Escritura SATA**: Hasta 500MB/s

#### Rendimiento de I/O
- **Velocidad Serial**: Hasta 115200 baudios
- **Refresco VGA**: 60Hz
- **Velocidad USB**: Hasta 480Mbps (USB 2.0)

### Configuración

#### Configuración de Drivers
```c
struct driver_config {
    bool enable_pit;           // Habilitar timer PIT
    bool enable_hpet;          // Habilitar timer HPET
    bool enable_lapic;         // Habilitar timer LAPIC
    bool enable_ata;           // Habilitar almacenamiento ATA
    bool enable_sata;          // Habilitar almacenamiento SATA
    bool enable_serial;        // Habilitar puertos seriales
    bool enable_vga;           // Habilitar salida VGA
    uint32_t timer_frequency;  // Frecuencia del timer
};
```

#### Detección de Hardware
```c
// Detectar hardware disponible
struct hardware_info {
    bool has_pit;
    bool has_hpet;
    bool has_lapic;
    bool has_ata;
    bool has_sata;
    bool has_serial;
    bool has_vga;
    uint32_t num_cpus;
    uint64_t total_memory;
};
```
