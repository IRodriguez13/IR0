# IR0 Kernel Configuration System
# Sistema de Configuración del Kernel IR0

## 🎯 Quick Start / Inicio Rápido

**One macro = Multiple subsystems / Una macro = Múltiples subsistemas**

```c
// In kernel_start.c, just use ONE of these:
// En kernel_start.c, solo usa UNA de estas:

#define IR0_DEVELOPMENT_MODE    // For testing / Para testing
#define IR0_DESKTOP            // For desktop / Para desktop
#define IR0_SERVER             // For server / Para server  
#define IR0_IOT                // For IoT / Para IoT
#define IR0_EMBEDDED           // For embedded / Para embedded
```

## 🚀 Examples / Ejemplos

### Testing Bump Allocator / Testing del Bump Allocator
```c
// In kernel_start.c:
#define IR0_DEVELOPMENT_MODE
#include <ir0/kernel_includes.h>

void main(void) 
{
    // Your tests here / Tus tests aquí
    void *ptr = kmalloc(16);
    if (ptr) 
    {
        print_colored("✓ Bump allocator working!\n", VGA_COLOR_GREEN, VGA_COLOR_BLACK);
    }
}
```

### Full Desktop Kernel / Kernel Desktop Completo
```c
// In kernel_start.c:
#define IR0_DESKTOP
#include <ir0/kernel_includes.h>

void main(void) 
{
    // Full system with scheduler, VFS, GUI / Sistema completo con scheduler, VFS, GUI
}
```

## 🔧 What Each Mode Enables / Qué Habilita Cada Modo

| Mode / Modo | Memory / Memoria | Process / Proceso | File System / Archivos | Drivers / Controladores | GUI |
|-------------|------------------|-------------------|------------------------|-------------------------|-----|
| **DEVELOPMENT** | ✅ Bump Allocator | ❌ | ❌ | ✅ All | ❌ |
| **DESKTOP** | ✅ All | ✅ All | ✅ All | ✅ All | ✅ |
| **SERVER** | ✅ All | ✅ All | ✅ All | ✅ All | ❌ |
| **IoT** | ✅ Basic | ❌ | ✅ Basic | ✅ Basic | ❌ |
| **EMBEDDED** | ✅ Bump Only | ❌ | ❌ | ✅ Basic | ❌ |

## ⚙️ Custom Configuration / Configuración Personalizada

```c
// Override specific settings / Sobrescribir configuraciones específicas
#define IR0_DEVELOPMENT_MODE

#undef ENABLE_MEMORY_TESTS
#define ENABLE_MEMORY_TESTS 0

#include <ir0/kernel_includes.h>
```

## 🛠️ Quick Start / Inicio Rápido

### Essential Commands / Comandos Esenciales
```bash
# Run kernel / Ejecutar kernel:
./scripts/quick_run.sh -64 -g              # 64-bit con GUI
./scripts/quick_run.sh -32 -n              # 32-bit sin GUI
./scripts/quick_run.sh -64 -d              # 64-bit con debugging
./scripts/quick_run.sh -64 -c -g           # Limpiar y ejecutar

# Alternative / Alternativo:
make run-64                                # 64-bit con GUI
make run-32                                # 32-bit con GUI
make run-64-debug                          # 64-bit con debugging
```

## 📁 Files / Archivos

- `setup/subsystem_config.h` - Main configuration / Configuración principal
- `includes/ir0/kernel_includes.h` - Conditional includes / Includes condicionales
- `examples/bump_allocator_testing.c` - Testing example / Ejemplo de testing
- `scripts/quick_run.sh` - Quick run script / Script de ejecución rápida
- `scripts/analyze_debug_logs.sh` - Debug log analyzer / Analizador de logs de debugging

## 🐛 Debugging / Depuración

### Debug Commands / Comandos de Debug
```bash
# Run with debugging / Ejecutar con debugging:
./scripts/quick_run.sh -64 -d              # 64-bit con debugging
make run-64-debug                          # 64-bit con debugging

# Analyze logs / Analizar logs:
./scripts/analyze_debug_logs.sh qemu_debug.log    # Analizar todo
./scripts/analyze_debug_logs.sh -p qemu_debug.log # Solo page faults
./scripts/analyze_debug_logs.sh -s qemu_debug.log # Solo resumen
```

### Debug Features / Características de Debug
- **Page Fault Detection** / Detección de Page Faults
- **Interrupt Logging** / Logging de Interrupciones  
- **Execution Tracing** / Trazado de Ejecución
- **Guest Error Detection** / Detección de Errores del Guest

## 🎉 That's It! / ¡Eso es Todo!

**Simple, automatic, and integrated with your existing strategy system.**

**Simple, automático e integrado con tu sistema de estrategias existente.**
