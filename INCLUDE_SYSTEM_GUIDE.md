# IR0 Kernel Include System Guide

## 📋 **Resumen**

Este sistema permite organizar los includes del kernel usando la sintaxis `#include<>` y activar/desactivar subsistemas fácilmente.

## 🏗️ **Estructura del Sistema**

### **Archivos Principales:**

1. **`includes/ir0/kernel_includes.h`** - Sistema centralizado de includes
2. **`includes/ir0/kernel_config_advanced.h`** - Configuraciones predefinidas
3. **`kernel/kernel_start.c`** - Archivo principal del kernel
4. **`examples/kernel_with_scheduler.c`** - Ejemplo con scheduler

## 🚀 **Cómo Usar el Sistema**

### **1. Configuración Básica**

En `kernel/kernel_start.c`:

```c
#include "kernel_start.h"

// Elige tu configuración:
#define KERNEL_CONFIG_DEVELOPMENT  // Para testing
// #define KERNEL_CONFIG_BASIC      // Para funcionalidad básica
// #define KERNEL_CONFIG_FULL       // Para kernel completo
// #define KERNEL_CONFIG_MINIMAL    // Para kernel mínimo

#include <ir0/kernel_config_advanced.h>
#include <ir0/kernel_includes.h>
```

### **2. Configuraciones Disponibles**

#### **🔧 KERNEL_CONFIG_MINIMAL**
- Solo componentes esenciales
- Bump allocator + timer drivers
- Sin drivers de I/O
- Ideal para debugging básico

#### **⚡ KERNEL_CONFIG_BASIC**
- Funcionalidad básica con drivers
- Bump allocator + drivers de teclado/disco
- Sin scheduler ni file system
- Ideal para desarrollo inicial

#### **🛠️ KERNEL_CONFIG_DEVELOPMENT**
- Configuración para desarrollo
- Incluye tests de memoria
- Debugging habilitado
- Ideal para testing

#### **🚀 KERNEL_CONFIG_FULL**
- Kernel completo con todos los subsistemas
- Scheduler + file system + shell
- Solo cuando todo esté implementado

#### **🎯 KERNEL_CONFIG_CUSTOM**
- Configuración personalizada
- Define tus propios flags
- Máxima flexibilidad

### **3. Activando Subsistemas**

#### **Para Activar el Scheduler:**

```c
#define KERNEL_CONFIG_CUSTOM

// Configuración personalizada con scheduler
#define ENABLE_BUMP_ALLOCATOR     1
#define ENABLE_HEAP_ALLOCATOR     1      // Requerido para scheduler
#define ENABLE_PROCESS_MANAGEMENT 1      // Gestión de procesos
#define ENABLE_SCHEDULER          1      // Scheduler
#define ENABLE_SYSCALLS           1      // System calls
#define ENABLE_ELF_LOADER         1      // Cargador ELF

#include <ir0/kernel_config_advanced.h>
#include <ir0/kernel_includes.h>
```

#### **Para Activar File System:**

```c
#define KERNEL_CONFIG_CUSTOM

#define ENABLE_VFS                1      // Virtual File System
#define ENABLE_IR0FS              1      // IR0 File System
#define ENABLE_BUMP_ALLOCATOR     1
#define ENABLE_HEAP_ALLOCATOR     1      // Requerido para VFS

#include <ir0/kernel_config_advanced.h>
#include <ir0/kernel_includes.h>
```

#### **Para Activar Shell:**

```c
#define KERNEL_CONFIG_CUSTOM

#define ENABLE_SHELL              1      // Shell interactivo
#define ENABLE_KEYBOARD_DRIVER    1      // Requerido para shell
#define ENABLE_VFS                1      // Requerido para shell
#define ENABLE_PROCESS_MANAGEMENT 1      // Requerido para shell

#include <ir0/kernel_config_advanced.h>
#include <ir0/kernel_includes.h>
```

## 📁 **Organización de Includes**

### **Sintaxis `#include<>`**

El sistema usa includes con sintaxis `#include<>` que se resuelven automáticamente:

```c
// En lugar de:
#include "../drivers/timer/clock_system.h"
#include "../interrupt/arch/idt.h"

// Usar:
#include <drivers/timer/clock_system.h>
#include <interrupt/idt.h>
```

### **Rutas Configuradas en Makefile:**

```makefile
CFLAGS += -I$(KERNEL_ROOT)/includes \
          -I$(KERNEL_ROOT)/includes/ir0 \
          -I$(KERNEL_ROOT)/arch/common \
          -I$(KERNEL_ROOT)/interrupt \
          -I$(KERNEL_ROOT)/drivers \
          -I$(KERNEL_ROOT)/fs \
          -I$(KERNEL_ROOT)/kernel
```

## 🔧 **Flags de Configuración**

### **Memory Management:**
- `ENABLE_BUMP_ALLOCATOR` - Allocator simple
- `ENABLE_HEAP_ALLOCATOR` - Allocator dinámico
- `ENABLE_PHYSICAL_ALLOCATOR` - Gestión de páginas físicas
- `ENABLE_VIRTUAL_MEMORY` - Memoria virtual

### **Process Management:**
- `ENABLE_PROCESS_MANAGEMENT` - Gestión de procesos
- `ENABLE_ELF_LOADER` - Cargador de ejecutables
- `ENABLE_SCHEDULER` - Planificador de tareas
- `ENABLE_SYSCALLS` - Interfaz de system calls

### **File System:**
- `ENABLE_VFS` - Virtual File System
- `ENABLE_IR0FS` - IR0 File System

### **Drivers:**
- `ENABLE_KEYBOARD_DRIVER` - Driver de teclado
- `ENABLE_ATA_DRIVER` - Driver de disco
- `ENABLE_PS2_DRIVER` - Driver PS2
- `ENABLE_TIMER_DRIVERS` - Drivers de timer

### **Debugging:**
- `ENABLE_DEBUGGING` - Sistema de debugging
- `ENABLE_LOGGING` - Sistema de logging

## 🎯 **Ejemplos Prácticos**

### **Ejemplo 1: Kernel Mínimo para Testing**

```c
#define KERNEL_CONFIG_MINIMAL
#include <ir0/kernel_config_advanced.h>
#include <ir0/kernel_includes.h>
```

### **Ejemplo 2: Kernel con Scheduler**

```c
#define KERNEL_CONFIG_CUSTOM

#define ENABLE_BUMP_ALLOCATOR     1
#define ENABLE_HEAP_ALLOCATOR     1
#define ENABLE_PROCESS_MANAGEMENT 1
#define ENABLE_SCHEDULER          1
#define ENABLE_SYSCALLS           1
#define ENABLE_KEYBOARD_DRIVER    1
#define ENABLE_TIMER_DRIVERS      1

#include <ir0/kernel_config_advanced.h>
#include <ir0/kernel_includes.h>
```

### **Ejemplo 3: Kernel Completo**

```c
#define KERNEL_CONFIG_FULL
#include <ir0/kernel_config_advanced.h>
#include <ir0/kernel_includes.h>
```

## 🔍 **Información de Build**

El sistema muestra automáticamente la configuración:

```
╔══════════════════════════════════════════════════════════════╗
║                    IR0 Kernel v0.0.0                         ║
║                    Build: DEVELOPMENT                        ║
╚══════════════════════════════════════════════════════════════╝

[KERNEL] Kernel Configuration:
[KERNEL]   Build Type: DEVELOPMENT
[KERNEL]   Memory Management: ENABLED
[KERNEL]   Process Management: DISABLED
[KERNEL]   File System: DISABLED
[KERNEL]   Drivers: ENABLED
[KERNEL]   Debugging: ENABLED
```

## 🚀 **Compilación**

```bash
# Compilar con configuración actual
make clean
make

# Compilar para 64 bits
make ARCH=x86-64

# Ejecutar en QEMU
make run
```

## 📝 **Notas Importantes**

1. **Dependencias:** Algunos subsistemas requieren otros:
   - Scheduler requiere heap allocator
   - Shell requiere keyboard driver + VFS
   - VFS requiere heap allocator

2. **Orden de Inicialización:** El sistema respeta las dependencias automáticamente

3. **Debugging:** Usa `KERNEL_CONFIG_DEVELOPMENT` para testing

4. **Performance:** Usa `KERNEL_CONFIG_MINIMAL` para máxima velocidad

## 🔮 **Futuras Mejoras**

- [ ] Configuración por línea de comandos
- [ ] Configuración dinámica en runtime
- [ ] Más opciones de debugging
- [ ] Configuración por target (desktop/server/embedded)
- [ ] Validación automática de dependencias

# IR0 Kernel Configuration System Guide
# Guía del Sistema de Configuración del Kernel IR0

## 📋 Overview / Resumen

The IR0 kernel uses a **simple macro-based configuration system** that works in tandem with the existing build strategy system.

El kernel IR0 usa un **sistema de configuración simple basado en macros** que funciona en tándem con el sistema de estrategias de compilación existente.

## 🎯 Quick Start / Inicio Rápido

### One Macro = Multiple Subsystems / Una Macro = Múltiples Subsistemas

```c
// In kernel_start.c, just use ONE of these:
// En kernel_start.c, solo usa UNA de estas:

#define IR0_DEVELOPMENT_MODE    // For testing / Para testing
#define IR0_DESKTOP            // For desktop / Para desktop
#define IR0_SERVER             // For server / Para server  
#define IR0_IOT                // For IoT / Para IoT
#define IR0_EMBEDDED           // For embedded / Para embedded
```

## 🔧 Configuration Options / Opciones de Configuración

### Development Mode / Modo Desarrollo
```c
#define IR0_DEVELOPMENT_MODE
// Enables / Habilita:
// - Bump allocator / Asignador bump
// - Interrupts / Interrupciones  
// - Drivers / Controladores
// - Memory tests / Tests de memoria
// - Stress tests / Tests de estrés
// - Debugging / Depuración
```

### Desktop Mode / Modo Desktop
```c
#define IR0_DESKTOP
// Enables / Habilita:
// - Everything from development / Todo del desarrollo
// - Process management / Gestión de procesos
// - Scheduler / Planificador
// - VFS / Sistema de archivos virtual
// - GUI / Interfaz gráfica
```

### Server Mode / Modo Servidor
```c
#define IR0_SERVER
// Enables / Habilita:
// - Process management / Gestión de procesos
// - Scheduler / Planificador
// - VFS / Sistema de archivos virtual
// - No GUI / Sin interfaz gráfica
```

### IoT Mode / Modo IoT
```c
#define IR0_IOT
// Enables / Habilita:
// - Minimal features / Características mínimas
// - Focus on efficiency / Enfoque en eficiencia
```

### Embedded Mode / Modo Embebido
```c
#define IR0_EMBEDDED
// Enables / Habilita:
// - Ultra minimal / Ultra mínimo
// - Basic functionality only / Solo funcionalidad básica
```

## 📁 File Structure / Estructura de Archivos

```
setup/
├── kernel_config.h          # Existing strategy system / Sistema de estrategias existente
├── subsystem_config.h       # New subsystem configuration / Nueva configuración de subsistemas
└── kernel_config.c          # Configuration functions / Funciones de configuración

includes/ir0/
└── kernel_includes.h        # Conditional includes / Includes condicionales

examples/
├── bump_allocator_testing.c # Bump allocator testing example / Ejemplo de testing del bump allocator
└── kernel_with_scheduler.c  # Scheduler example / Ejemplo con planificador
```

## 🚀 Usage Examples / Ejemplos de Uso

### Testing Bump Allocator / Testing del Bump Allocator
```c
// In kernel_start.c:
#define IR0_DEVELOPMENT_MODE
#include <ir0/kernel_includes.h>

void main(void) {
    // Banners and basic init / Banners e inicialización básica
    // ... your tests / ... tus tests
}
```

### Full Desktop Kernel / Kernel Desktop Completo
```c
// In kernel_start.c:
#define IR0_DESKTOP
#include <ir0/kernel_includes.h>

void main(void) {
    // Full system initialization / Inicialización completa del sistema
    // All subsystems available / Todos los subsistemas disponibles
}
```

### Custom Configuration / Configuración Personalizada
```c
// In kernel_start.c:
#define IR0_DEVELOPMENT_MODE

// Override specific settings / Sobrescribir configuraciones específicas
#undef ENABLE_MEMORY_TESTS
#define ENABLE_MEMORY_TESTS 0

#include <ir0/kernel_includes.h>
```

## ⚙️ Subsystem Flags / Flags de Subsistemas

### Memory Management / Gestión de Memoria
```c
#define ENABLE_BUMP_ALLOCATOR     1   // Simple bump allocator / Asignador bump simple
#define ENABLE_HEAP_ALLOCATOR     0   // Dynamic heap allocator / Asignador heap dinámico
#define ENABLE_PHYSICAL_ALLOCATOR 0   // Physical page allocator / Asignador de páginas físicas
#define ENABLE_VIRTUAL_MEMORY     0   // Virtual memory management / Gestión de memoria virtual
```

### Process Management / Gestión de Procesos
```c
#define ENABLE_PROCESS_MANAGEMENT 0   // Process creation and management / Creación y gestión de procesos
#define ENABLE_ELF_LOADER         0   // ELF executable loader / Cargador de ejecutables ELF
#define ENABLE_SCHEDULER          0   // Task scheduler / Planificador de tareas
#define ENABLE_SYSCALLS           0   // System call interface / Interfaz de llamadas al sistema
```

### File System / Sistema de Archivos
```c
#define ENABLE_VFS                0   // Virtual File System / Sistema de Archivos Virtual
#define ENABLE_IR0FS              0   // IR0 File System / Sistema de Archivos IR0
#define ENABLE_EXT2               0   // EXT2 file system support / Soporte para sistema de archivos EXT2
```

### Drivers / Controladores
```c
#define ENABLE_KEYBOARD_DRIVER    1   // Keyboard input driver / Controlador de entrada de teclado
#define ENABLE_ATA_DRIVER         1   // ATA disk driver / Controlador de disco ATA
#define ENABLE_PS2_DRIVER         1   // PS2 controller driver / Controlador PS2
#define ENABLE_TIMER_DRIVERS      1   // Timer drivers / Controladores de temporizador
#define ENABLE_VGA_DRIVER         1   // VGA display driver / Controlador de pantalla VGA
```

### Debugging and Development / Depuración y Desarrollo
```c
#define ENABLE_DEBUGGING          0   // Debugging system / Sistema de depuración
#define ENABLE_LOGGING            1   // Logging system / Sistema de logging
#define ENABLE_MEMORY_TESTS       0   // Memory allocation tests / Tests de asignación de memoria
#define ENABLE_STRESS_TESTS       0   // Stress testing / Tests de estrés
```

### Shell and User Interface / Shell e Interfaz de Usuario
```c
#define ENABLE_SHELL              0   // Interactive shell / Shell interactivo
#define ENABLE_GUI                0   // Graphical user interface / Interfaz gráfica de usuario
```

## 🔄 Automatic Dependencies / Dependencias Automáticas

The system automatically validates dependencies and includes only what's needed.

El sistema valida automáticamente las dependencias e incluye solo lo necesario.

```c
// Examples / Ejemplos:
#if ENABLE_SCHEDULER && !ENABLE_HEAP_ALLOCATOR
    #error "Scheduler requires heap allocator to be enabled"
#endif

#if ENABLE_SHELL && !ENABLE_KEYBOARD_DRIVER
    #error "Shell requires keyboard driver to be enabled"
#endif
```

## 📊 Feature Summary Macros / Macros de Resumen de Características

```c
// Check what's enabled / Verificar qué está habilitado:
HAS_MEMORY_MANAGEMENT()     // Returns true if any memory system enabled / Retorna true si algún sistema de memoria está habilitado
HAS_PROCESS_MANAGEMENT()    // Returns true if process management enabled / Retorna true si gestión de procesos está habilitada
HAS_FILE_SYSTEM()          // Returns true if any file system enabled / Retorna true si algún sistema de archivos está habilitado
HAS_DRIVERS()              // Returns true if any drivers enabled / Retorna true si algún controlador está habilitado
HAS_DEBUGGING()            // Returns true if debugging enabled / Retorna true si depuración está habilitada
HAS_USER_INTERFACE()       // Returns true if UI enabled / Retorna true si interfaz de usuario está habilitada
```

## 🛠️ Compilation / Compilación

### Build Commands / Comandos de Compilación
```bash
# For x86-64 / Para x86-64:
make clean
make x86-64

# For x86-32 / Para x86-32:
make clean  
make x86-32

# Run in QEMU / Ejecutar en QEMU:
make run-x86-64
make run-x86-32
```

### Include Paths / Rutas de Include
The Makefile automatically includes all necessary paths for the `#include<>` syntax.

El Makefile incluye automáticamente todas las rutas necesarias para la sintaxis `#include<>`.

## 🎯 Best Practices / Mejores Prácticas

### 1. Start Simple / Comenzar Simple
```c
// Start with development mode / Comenzar con modo desarrollo
#define IR0_DEVELOPMENT_MODE
```

### 2. Add Features Gradually / Agregar Características Gradualmente
```c
// Enable specific features as needed / Habilitar características específicas según se necesiten
#undef ENABLE_MEMORY_TESTS
#define ENABLE_MEMORY_TESTS 1
```

### 3. Use Conditional Initialization / Usar Inicialización Condicional
```c
#if ENABLE_SCHEDULER
    log_info("KERNEL", "Initializing scheduler");
    scheduler_init();
#endif
```

### 4. Keep Banners / Mantener Banners
```c
// Always keep the nice banners / Siempre mantener los banners bonitos
print_colored("╔══════════════════════════════════════════════════════════════╗\n", VGA_COLOR_GREEN, VGA_COLOR_BLACK);
```

## 🔍 Troubleshooting / Solución de Problemas

### Common Issues / Problemas Comunes

1. **Undefined reference errors / Errores de referencia indefinida**
   - Check if subsystem is enabled / Verificar si el subsistema está habilitado
   - Ensure object files are compiled / Asegurar que los archivos objeto estén compilados

2. **Missing includes / Includes faltantes**
   - Verify include paths in Makefile / Verificar rutas de include en Makefile
   - Check if subsystem flag is set / Verificar si el flag del subsistema está configurado

3. **Dependency errors / Errores de dependencia**
   - Enable required subsystems first / Habilitar subsistemas requeridos primero
   - Check dependency validation / Verificar validación de dependencias

## 📚 Examples Directory / Directorio de Ejemplos

Check the `examples/` directory for complete working examples.

Revisa el directorio `examples/` para ejemplos completos que funcionan.

- `bump_allocator_testing.c` - Complete bump allocator testing / Testing completo del bump allocator
- `kernel_with_scheduler.c` - Kernel with scheduler enabled / Kernel con planificador habilitado

## 🎉 Summary / Resumen

**One macro = Complete system configuration**

**Una macro = Configuración completa del sistema**

The IR0 kernel configuration system is designed to be:
- **Simple**: One macro enables multiple subsystems
- **Automatic**: Dependencies are validated automatically  
- **Flexible**: Easy to override specific settings
- **Integrated**: Works with existing strategy system

El sistema de configuración del kernel IR0 está diseñado para ser:
- **Simple**: Una macro habilita múltiples subsistemas
- **Automático**: Las dependencias se validan automáticamente
- **Flexible**: Fácil de sobrescribir configuraciones específicas  
- **Integrado**: Funciona con el sistema de estrategias existente
