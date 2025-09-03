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

void main(void) 
{
    // Banners and basic init / Banners e inicialización básica
    // ... your tests / ... tus tests
}
```

### Full Desktop Kernel / Kernel Desktop Completo
```c
// In kernel_start.c:
#define IR0_DESKTOP
#include <ir0/kernel_includes.h>

void main(void) 
{
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
