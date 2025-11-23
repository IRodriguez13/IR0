# IR0 Kernel

IR0 is a monolithic operating system kernel for x86-64/ARM32 architecture.

## Dependencies

The following tools are required to build and run IR0:

### Essential Build Tools
- **GCC** (GNU Compiler Collection) - C compiler
- **NASM** (Netwide Assembler) - Assembly compiler
- **LD** (GNU Linker) - ELF format linker with x86-64 support
- **Make** - Build automation tool

### Runtime Tools
- **QEMU** (qemu-system-x86_64) - Virtual machine emulator
- **GRUB** (grub-mkrescue) - Bootloader tools for ISO creation

### Optional Tools
- **Python 3** - Required for kernel configuration system

### Platform-Specific Notes

**Linux:**
- Install build tools: `apt-get install build-essential nasm qemu-system-x86 grub-pc-bin`
- Or on Debian/Ubuntu: `apt-get install gcc nasm binutils make qemu-system-x86 grub-pc-bin`

**Windows:**
- Install MSYS2 or MinGW-w64 with binutils package
- Ensure ELF linker is available at `/usr/bin/ld.exe` or `/mingw64/bin/ld.exe`
- QEMU can be installed via MSYS2: `pacman -S qemu`

## First-Time Setup

Before building the kernel, verify that all dependencies are installed:

```bash
make deptest
```

This command checks for all required tools and displays installation instructions for any missing dependencies.

## Building the Kernel

### Full Build

To build the complete kernel with ISO image and userspace programs:

```bash
make ir0
```

This command:
1. Compiles all kernel source files
2. Links the kernel binary
3. Creates a bootable ISO image
4. Builds userspace programs

The virtual disk image (`disk.img`) is created automatically on first build.

### Windows Build

On Windows, use the Windows-specific Makefile:

```bash
make -f Makefile.win ir0
```

The Makefile automatically detects MSYS2/MinGW environments and uses the appropriate tools.

## Running the Kernel

After building, run the kernel in QEMU:

```bash
make run
```

This starts QEMU with the kernel ISO and virtual disk attached.

Additional run options:
- `make run-debug` - Run with serial debug output
- `make run-nodisk` - Run without virtual disk
- `make run-console` - Run in console mode (no GUI)

## Development

### Compiling Individual Files


When developing drivers or kernel modules, compile a single file in isolation:

```bash
make unibuild drivers/IO/ps2.c
```

Or using the traditional syntax:

```bash
make unibuild FILE=drivers/IO/ps2.c
```

**Cross-Platform Compilation:**

Compile for Windows using MinGW cross-compiler:

```bash
make unibuild -win drivers/IO/ps2.c
```

And support for other languages:

```bash
make unibuild -cpp kernel/module.cpp    # C++ support (planned)
make unibuild -rust kernel/module.rs    # Rust support (planned)
```

This compiles the specified file without building the entire kernel. Multiple files can be compiled at once:

```bash
make unibuild fs/vfs.c fs/ramfs.c
make unibuild -win drivers/serial/serial.c drivers/dma/dma.c
```

To clean a single compiled file:

```bash
make unibuild-clean FILE=drivers/IO/ps2.c
```

### Creating Virtual Disk

The virtual disk is created automatically during build. To create it manually:

```bash
make create-disk
```

To remove the virtual disk:

```bash
make delete-disk
```

### Cleaning Build Artifacts

To clean all compiled objects and binaries:

```bash
make clean
```

### Kernel Configuration Menu (Experimental)

IR0 includes an **experimental** graphical configuration tool (`menuconfig`) that provides an interactive interface for kernel subsystem selection and compilation. This tool is designed to simplify kernel development and allow fine-grained control over which components are built.

**Features:**
- **Visual subsystem selection**: Interactive interface showing kernel architecture layers
- **Selective compilation**: Build only the subsystems you need using the integrated `unibuild` system
- **Real-time architecture diagram**: Visual representation of kernel components and their dependencies
- **Profile support**: Pre-configured profiles for different use cases (x86-64 Generic, ARM32, etc.)
- **Cross-platform**: Supports Linux, Windows (MSYS2/MinGW), and macOS

**Usage:**
```bash
make menuconfig
```

Or run directly:
```bash
./menuconfig
```

**Requirements:**
- Python 3 with tkinter support (`python3-tk` package on Debian/Ubuntu)

**Note:** This feature is currently experimental. While it provides a convenient way to configure and build the kernel subsystems, the standard `make ir0` workflow remains the primary recommended build method. The configuration tool integrates with the kernel's `unibuild` system to compile selected subsystems incrementally.

## Kernel Features

### Memory Management
- Paging with identity mapping
- Heap allocator (kmalloc/kfree)
- Physical memory bitmap management
- Kernel and user space separation

### Process Management
- Process state management (NEW, READY, RUNNING, SLEEPING, STOPPED, ZOMBIE, DEAD)
- Multiple scheduler algorithms (CFS, Priority-based, Round-Robin)
- Context switching implementation
- Process tree hierarchy

### System Calls
- POSIX-compatible system call interface
- Process control (fork, exec, exit, wait, getpid)
- File operations (open, close, read, write, lseek)
- Memory management (brk, mmap, munmap, mprotect)
- Time operations (time, gettimeofday, sleep, usleep)

### Virtual File System
- VFS abstraction layer
- MINIX filesystem support
- RAM filesystem for boot files
- Mount point management

### Hardware Drivers
- PS/2 keyboard driver
- ATA storage driver
- VGA display driver
- Timer drivers (PIT, HPET, LAPIC)

### Interrupt System
- Complete IDT setup
- PIC configuration
- Exception handling
- TSS configuration

### Interactive Shell
- Command-line interface
- Built-in commands (help, info, version, ps, meminfo)
- System integration
- Basic authentication framework

## Architecture

IR0 targets x86-64 architecture. The kernel uses:
- ELF64 binary format
- Multiboot boot protocol
- Protected mode with paging
- Ring 0 (kernel) and Ring 3 (user) separation

## Build System

The build system supports:
- Native Linux compilation
- Native Windows compilation (with MSYS2/MinGW)
- Cross-compilation from Linux to Windows

Build targets:
- `make ir0` - Full kernel build (Linux)
- `make -f Makefile.win ir0` - Full kernel build (Windows)
- `make deptest` - Check dependencies
- `make unibuild FILE=<file>` - Compile single file
- `make clean` - Clean build artifacts


## License

This project is licensed under the GNU General Public License v3.0. See the LICENSE file for details.

---

# IR0 Kernel (Español)

IR0 es un kernel de sistema operativo monolítico para arquitectura x86-64/ARM32.

## Dependencias

Las siguientes herramientas son necesarias para compilar y ejecutar IR0:

### Herramientas de Compilación Esenciales
- **GCC** (GNU Compiler Collection) - Compilador C
- **NASM** (Netwide Assembler) - Compilador de ensamblador
- **LD** (GNU Linker) - Enlazador con soporte para formato ELF x86-64
- **Make** - Herramienta de automatización de compilación

### Herramientas de Ejecución
- **QEMU** (qemu-system-x86_64) - Emulador de máquina virtual
- **GRUB** (grub-mkrescue) - Herramientas de bootloader para creación de ISO

### Herramientas Opcionales
- **Python 3** - Requerido para el sistema de configuración del kernel

### Notas Específicas por Plataforma

**Linux:**
- Instalar herramientas de compilación: `apt-get install build-essential nasm qemu-system-x86 grub-pc-bin`
- O en Debian/Ubuntu: `apt-get install gcc nasm binutils make qemu-system-x86 grub-pc-bin`

**Windows:**
- Instalar MSYS2 o MinGW-w64 con el paquete binutils
- Asegurar que el enlazador ELF esté disponible en `/usr/bin/ld.exe` o `/mingw64/bin/ld.exe`
- QEMU se puede instalar vía MSYS2: `pacman -S qemu`

## Configuración Inicial

Antes de compilar el kernel, verificar que todas las dependencias estén instaladas:

```bash
make deptest
```

Este comando verifica todas las herramientas requeridas y muestra instrucciones de instalación para cualquier dependencia faltante.

## Compilación del Kernel

### Compilación Completa

Para compilar el kernel completo con imagen ISO y programas de espacio de usuario:

```bash
make ir0
```

Este comando:
1. Compila todos los archivos fuente del kernel
2. Enlaza el binario del kernel
3. Crea una imagen ISO booteable
4. Compila los programas de espacio de usuario

La imagen de disco virtual (`disk.img`) se crea automáticamente en la primera compilación.

### Compilación en Windows

En Windows, usar el Makefile específico de Windows:

```bash
make -f Makefile.win ir0
```

El Makefile detecta automáticamente los entornos MSYS2/MinGW y usa las herramientas apropiadas.

## Ejecución del Kernel

Después de compilar, ejecutar el kernel en QEMU:

```bash
make run
```

Esto inicia QEMU con la ISO del kernel y el disco virtual adjunto.

Opciones adicionales de ejecución:
- `make run-debug` - Ejecutar con salida de depuración serial
- `make run-nodisk` - Ejecutar sin disco virtual
- `make run-console` - Ejecutar en modo consola (sin GUI)

## Desarrollo

### Compilación de Archivos Individuales

Al desarrollar controladores o módulos del kernel, compilar un solo archivo de forma aislada:

```bash
make unibuild drivers/IO/ps2.c
```

O usando la sintaxis tradicional:

```bash
make unibuild FILE=drivers/IO/ps2.c
```

**Compilación Cruzada para Múltiples Plataformas:**

Compilar para Windows usando el compilador cruzado MinGW:

```bash
make unibuild -win drivers/IO/ps2.c
```

Soporte para otros lenguajes:

```bash
make unibuild -cpp kernel/module.cpp    # Soporte C++ (planificado)
make unibuild -rust kernel/module.rs    # Soporte Rust (planificado)
```

Esto compila el archivo especificado sin compilar todo el kernel. Se pueden compilar múltiples archivos a la vez:

```bash
make unibuild fs/vfs.c fs/ramfs.c
make unibuild -win drivers/serial/serial.c drivers/dma/dma.c
```

Para limpiar un archivo compilado individual:

```bash
make unibuild-clean FILE=drivers/IO/ps2.c
```

### Creación de Disco Virtual

El disco virtual se crea automáticamente durante la compilación. Para crearlo manualmente:

```bash
make create-disk
```

Para eliminar el disco virtual:

```bash
make delete-disk
```

### Limpieza de Artefactos de Compilación

Para limpiar todos los objetos compilados y binarios:

```bash
make clean
```

### Menú de Configuración del Kernel (Experimental)

IR0 incluye una herramienta gráfica de configuración **experimental** (`menuconfig`) que proporciona una interfaz interactiva para la selección y compilación de subsistemas del kernel. Esta herramienta está diseñada para simplificar el desarrollo del kernel y permitir un control granular sobre qué componentes se compilan.

**Características:**
- **Selección visual de subsistemas**: Interfaz interactiva mostrando las capas de arquitectura del kernel
- **Compilación selectiva**: Compila solo los subsistemas que necesites usando el sistema integrado `unibuild`
- **Diagrama de arquitectura en tiempo real**: Representación visual de los componentes del kernel y sus dependencias
- **Soporte de perfiles**: Perfiles preconfigurados para diferentes casos de uso (x86-64 Genérico, ARM32, etc.)
- **Multiplataforma**: Soporta Linux, Windows (MSYS2/MinGW) y macOS

**Uso:**
```bash
make menuconfig
```

O ejecutar directamente:
```bash
./menuconfig
```

**Requisitos:**
- Python 3 con soporte para tkinter (paquete `python3-tk` en Debian/Ubuntu)

**Nota:** Esta característica es actualmente experimental. Aunque proporciona una manera conveniente de configurar y compilar los subsistemas del kernel, el flujo de trabajo estándar `make ir0` sigue siendo el método de compilación principal recomendado. La herramienta de configuración se integra con el sistema `unibuild` del kernel para compilar subsistemas seleccionados de forma incremental.

## Características del Kernel

### Gestión de Memoria
- Paginación con mapeo de identidad
- Asignador de heap (kmalloc/kfree)
- Gestión de mapa de bits de memoria física
- Separación de espacio de kernel y usuario

### Gestión de Procesos
- Gestión de estados de proceso (NEW, READY, RUNNING, SLEEPING, STOPPED, ZOMBIE, DEAD)
- Múltiples algoritmos de planificación (CFS, basado en prioridad, Round-Robin)
- Implementación de cambio de contexto
- Jerarquía de árbol de procesos

### Llamadas al Sistema
- Interfaz de llamadas al sistema compatible con POSIX
- Control de procesos (fork, exec, exit, wait, getpid)
- Operaciones de archivo (open, close, read, write, lseek)
- Gestión de memoria (brk, mmap, munmap, mprotect)
- Operaciones de tiempo (time, gettimeofday, sleep, usleep)

### Sistema de Archivos Virtual
- Capa de abstracción VFS
- Soporte para sistema de archivos MINIX
- Sistema de archivos RAM para archivos de arranque
- Gestión de puntos de montaje

### Controladores de Hardware
- Controlador de teclado PS/2
- Controlador de almacenamiento ATA
- Controlador de pantalla VGA
- Controladores de temporizador (PIT, HPET, LAPIC)

### Sistema de Interrupciones
- Configuración completa de IDT
- Configuración de PIC
- Manejo de excepciones
- Configuración de TSS

### Shell Interactivo
- Interfaz de línea de comandos
- Comandos integrados (help, info, version, ps, meminfo)
- Integración con el sistema
- Marco básico de autenticación

## Arquitectura

IR0 está dirigido a la arquitectura x86-64. El kernel utiliza:
- Formato binario ELF64
- Protocolo de arranque Multiboot
- Modo protegido con paginación
- Separación de Ring 0 (kernel) y Ring 3 (usuario)

## Sistema de Compilación

El sistema de compilación soporta:
- Compilación nativa en Linux
- Compilación nativa en Windows (con MSYS2/MinGW)
- Compilación cruzada desde Linux a Windows

Objetivos de compilación:
- `make ir0` - Compilación completa del kernel (Linux)
- `make -f Makefile.win ir0` - Compilación completa del kernel (Windows)
- `make deptest` - Verificar dependencias
- `make unibuild FILE=<archivo>` - Compilar archivo individual
- `make clean` - Limpiar artefactos de compilación

## Documentación

Documentación adicional disponible en el repositorio:
- Sistema de Compilación: `setup/docs/BUILD_SYSTEM.md`
- Instalación: `setup/docs/INSTALL.md`
- Configuración: `setup/docs/CONFIGURATION_SYSTEM_README.md`
- Sistema de Inclusión: `setup/docs/INCLUDE_SYSTEM_GUIDE.md`

## Licencia

Este proyecto está licenciado bajo la Licencia Pública General de GNU v3.0. Ver el archivo LICENSE para más detalles.
