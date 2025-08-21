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
