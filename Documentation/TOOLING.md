# IR0 Kernel Tooling and Build System

## Overview

The IR0 kernel build system provides comprehensive tooling for building, configuring, testing, and deploying the kernel. This includes Makefile targets, configuration management via menuconfig, disk image creation, init process loading, and multi-language driver compilation support.

## Build System Architecture

The build system is based on GNU Make with support for:
- **Multi-architecture builds** (x86-64, x86-32, ARM64, ARM32)
- **Multi-language compilation** (C, C++, Rust)
- **Configuration management** (menuconfig system)
- **Disk image management** (create, format, load init)
- **Unibuild system** (isolated file compilation)
- **Cross-compilation** (Windows via MinGW)

## Makefile Targets

### Primary Build Targets

#### `make ir0` or `make all`
Builds the complete kernel:
1. Compiles all kernel source files (C, C++, Rust, Assembly)
2. Links kernel binary (`kernel-x64.bin`)
3. Creates bootable ISO image (`kernel-x64.iso`)
4. Creates virtual disk image (`disk.img`) if it doesn't exist

**Usage:**
```bash
make ir0
```

#### `make ir0-auto` or `make auto`
Builds kernel using all available CPU cores for parallel compilation.

**Usage:**
```bash
make ir0-auto
```

#### `make kernel-x64.bin`
Compiles and links only the kernel binary (without ISO creation).

#### `make kernel-x64.iso`
Creates bootable ISO image from kernel binary.

**Requirements:**
- `kernel-x64.bin` must exist
- `grub-mkrescue` must be installed
- GRUB configuration at `arch/x86-64/grub.cfg`

### Configuration Targets

#### `make menuconfig`
Launches interactive kernel configuration menu.

**Features:**
- GUI-based configuration interface
- Subsystem selection (enable/disable kernel subsystems)
- Configuration saved to `.config` file
- Dynamic Makefile generation based on configuration

**Usage:**
```bash
make menuconfig
```

**Configuration Files:**
- `.config` - User configuration (subsystem selection)
- `setup/subsystem_config.h` - Generated configuration header
- `setup/subsystems.json` - Subsystem definitions

**Process:**
1. Builds kconfig library if needed
2. Launches Tkinter-based GUI
3. User selects subsystems
4. Configuration saved to `.config`
5. Makefile regenerated dynamically

### Disk Image Management

#### `make create-disk [filesystem] [size]`
Creates a virtual disk image for filesystem storage.

**Supported Filesystems:**
- `minix` - MINIX filesystem (default, 200MB default size)
- `fat32` - FAT32 filesystem (200MB default size)
- `ext4` - ext4 filesystem (500MB default size)

**Usage Examples:**
```bash
make create-disk                    # Create 200MB MINIX disk (disk.img)
make create-disk minix 500          # Create 500MB MINIX disk
make create-disk fat32              # Create FAT32 disk (fat32.img, 200MB)
make create-disk fat32 500          # Create 500MB FAT32 disk
make create-disk ext4 1000          # Create 1GB ext4 disk
```

**Parameters:**
- `filesystem` - Filesystem type (minix, fat32, ext4)
- `size` - Disk size in MB (default: 200 for minix/fat32, 500 for ext4)

**Output Files:**
- `disk.img` - Default for MINIX
- `fat32.img` - Default for FAT32
- `ext4.img` - Default for ext4
- Custom filename via `-o` flag in script

**Behind the scenes:**
- Calls `scripts/create_disk.sh`
- Creates raw disk image using `dd`
- Kernel auto-formats disk on first boot if unformatted

#### `make delete-disk`
Deletes virtual disk image.

**Usage:**
```bash
make delete-disk          # Delete disk.img
make delete-disk fat32    # Delete fat32.img
```

**Warning:** This permanently deletes the disk image and all its contents.

### Init Process Management

#### `make load-init [filesystem] [disk_image]`
Loads init binary into virtual disk image.

**Init Process Policy:**
- Init binary must be compiled separately and placed at `setup/pid1/init`
- Script mounts disk image and copies init to `/sbin/init`
- Kernel attempts to execute `/sbin/init` at boot
- Falls back to debug shell if init is not found

**Usage Examples:**
```bash
make load-init                    # Auto-detect FS, use disk.img
make load-init fat32              # Use fat32.img
make load-init disk.img           # Explicit disk image
make load-init fat32 fat32.img    # Explicit filesystem and disk
```

**Requirements:**
- Root privileges (sudo) required for mounting
- Disk image must be formatted (via `create-disk`)
- Init binary must exist at `setup/pid1/init`

**Behind the scenes:**
- Calls `scripts/load_init.sh`
- Mounts disk image as loop device
- Creates `/sbin` directory if needed
- Copies init binary and sets executable permissions
- Unmounts disk image

#### `make remove-init`
Removes init binary from disk image.

**Usage:**
```bash
make remove-init
```

### Running and Debugging

#### `make run`
Runs kernel in QEMU with GUI and all supported hardware.

**Hardware Emulated:**
- RTL8139 network card
- Sound Blaster 16 audio
- Adlib OPL2
- ATA/IDE disk
- Serial port (COM1)
- PS/2 keyboard/mouse
- VGA display

**Usage:**
```bash
make run
```

**QEMU Configuration:**
- Memory: 512MB
- Display: GTK window
- Serial: stdio (for kernel messages)
- Network: User-mode networking
- Disk: `disk.img` attached as IDE drive

#### `make run-debug`
Runs kernel with serial debug output in terminal.

**Features:**
- QEMU GUI in separate window
- Serial output in current terminal
- Detailed interrupt logging
- QEMU monitor on telnet port 1234

**Usage:**
```bash
make run-debug
```

#### `make run-nodisk`
Runs kernel without disk image (no filesystem).

**Usage:**
```bash
make run-nodisk
```

#### `make run-console`
Runs kernel in console mode (no GUI).

**Usage:**
```bash
make run-console
```

**Features:**
- All output to terminal
- Serial console interface
- All hardware still emulated

#### `make debug`
Runs kernel with detailed QEMU debugging.

**Features:**
- Interrupt logging
- CPU reset logging
- Guest error logging
- Debug log written to `qemu_debug.log`

**Usage:**
```bash
make debug
```

### Cleanup Targets

#### `make clean`
Removes all build artifacts.

**Cleaned Files:**
- Object files (`.o`)
- Kernel binary (`kernel-x64.bin`)
- ISO image (`kernel-x64.iso`)
- Build directory contents

**Usage:**
```bash
make clean
```

#### `make unibuild-clean`
Cleans unibuild artifacts (isolated compilation outputs).

**Usage:**
```bash
make unibuild-clean
```

### Testing and Validation

#### `make deptest`
Tests for required dependencies and displays installation instructions.

**Checks:**
- GCC compiler
- NASM assembler
- GNU linker (ld)
- Make
- QEMU
- GRUB tools
- Python 3 (for menuconfig)

**Usage:**
```bash
make deptest
```

#### `make help`
Displays help message with available targets.

**Usage:**
```bash
make help
```

### Windows Build Targets

#### `make windows` or `make win`
Builds kernel for Windows using MinGW cross-compilation.

**Requirements:**
- MSYS2 or MinGW-w64
- Windows-specific Makefile (`Makefile.win`)

**Usage:**
```bash
make windows
```

#### `make windows-clean` or `make win-clean`
Cleans Windows build artifacts.

**Usage:**
```bash
make windows-clean
```

### Unibuild System

The unibuild system allows isolated compilation of individual files without rebuilding the entire kernel.

#### `make unibuild <source_file>`
Compiles a single C file in isolation.

**Usage:**
```bash
make unibuild drivers/IO/ps2.c
make unibuild kernel/process.c
```

**Features:**
- Uses kernel headers and flags
- Outputs `.o` object file
- Useful for quick syntax checking

#### `make unibuild-cpp <source_file>`
Compiles a single C++ file in isolation.

**Usage:**
```bash
make unibuild-cpp cpp/examples/cpp_example.cpp
```

#### `make unibuild-rust <source_file>`
Compiles a single Rust file in isolation.

**Usage:**
```bash
make unibuild-rust rust/drivers/rust_simple_driver.rs
```

**Requirements:**
- Rust toolchain with `x86_64-ir0-kernel` target
- Target specification at `rust/x86_64-ir0-kernel.json`

**Windows Unibuild:**
- `make unibuild-win` - Windows cross-compile C files
- `make unibuild-cpp-win` - Windows cross-compile C++ files
- `make unibuild-rust-win` - Windows cross-compile Rust files

### External Driver Support

#### `make en-ext-drv`
Enables example external drivers (Rust and C++).

**Effect:**
- Enables `KERNEL_ENABLE_EXAMPLE_DRIVERS` flag
- Includes example Rust driver (`rust/drivers/rust_simple_driver.rs`)
- Includes example C++ driver (`cpp/examples/cpp_example.cpp`)
- Registers multi-language driver support

**Usage:**
```bash
make en-ext-drv
make ir0
```

#### `make dis-ext-drv`
Disables example external drivers.

**Usage:**
```bash
make dis-ext-drv
```

#### `make test-drivers`
Compiles test drivers (Rust and C++).

**Usage:**
```bash
make test-drivers
```

## Scripts

### Disk Management Scripts

#### `scripts/create_disk.sh`
Creates virtual disk images for IR0 filesystems.

**Usage:**
```bash
./scripts/create_disk.sh [filesystem] [size] [-o output_file]
```

**Examples:**
```bash
./scripts/create_disk.sh              # 200MB MINIX disk
./scripts/create_disk.sh minix 500    # 500MB MINIX disk
./scripts/create_disk.sh fat32        # FAT32 disk
./scripts/create_disk.sh -f fat32 -s 500 -o custom.img
```

**Supported Filesystems:**
- `minix` - MINIX filesystem (default)
- `fat32` - FAT32 filesystem
- `ext4` - ext4 filesystem

**Features:**
- Auto-detects default disk filename from filesystem type
- Backs up existing disk if present
- Shows progress bar with `pv` if available

#### `scripts/load_init.sh`
Loads init binary into virtual disk image.

**Usage:**
```bash
sudo ./scripts/load_init.sh [filesystem] [disk_image] [init_binary]
```

**Examples:**
```bash
sudo ./scripts/load_init.sh                    # Auto-detect, use disk.img
sudo ./scripts/load_init.sh fat32              # Use fat32.img
sudo ./scripts/load_init.sh disk.img           # Explicit disk
sudo ./scripts/load_init.sh fat32 fat32.img /path/to/init
```

**Features:**
- Auto-detects filesystem from disk filename
- Creates `/sbin` directory if needed
- Sets executable permissions
- Verifies file was copied successfully

**Requirements:**
- Root privileges (sudo)
- Formatted disk image
- Init binary must exist

#### `scripts/delete_disk.sh`
Deletes virtual disk images.

**Usage:**
```bash
./scripts/delete_disk.sh [disk_image]
```

### Build Scripts

#### `scripts/unibuild.sh`
Unified build script for isolated file compilation.

**Usage:**
```bash
./scripts/unibuild.sh [-win] [-rust|-cpp] <source_file> [source_file2] ...
```

**Examples:**
```bash
./scripts/unibuild.sh kernel/process.c
./scripts/unibuild.sh -cpp cpp/examples/cpp_example.cpp
./scripts/unibuild.sh -rust rust/drivers/rust_simple_driver.rs
./scripts/unibuild.sh -win drivers/IO/ps2.c  # Windows cross-compile
```

**Features:**
- Supports C, C++, and Rust
- Native and Windows cross-compilation
- Multiple file compilation
- Automatic dependency detection

#### `scripts/unibuild-clean.sh`
Cleans unibuild artifacts.

**Usage:**
```bash
./scripts/unibuild-clean.sh
```

### Testing Scripts

#### `scripts/test.sh`
Runs kernel tests.

**Usage:**
```bash
./scripts/test.sh
```

#### `scripts/test_userspace.sh`
Tests userspace programs.

**Usage:**
```bash
./scripts/test_userspace.sh
```

#### `scripts/quick_run.sh`
Quick kernel run script.

**Usage:**
```bash
./scripts/quick_run.sh
```

### Utility Scripts

#### `scripts/cloc.sh`
Counts lines of code.

**Usage:**
```bash
./scripts/cloc.sh
```

**Requirements:**
- `cloc` tool installed

#### `scripts/deptest.sh`
Tests for build dependencies.

**Usage:**
```bash
./scripts/deptest.sh
```

**Called by:** `make deptest`

#### `scripts/build_simple.sh`
Simple build script (alternative to Makefile).

**Usage:**
```bash
./scripts/build_simple.sh
```

## Configuration System (menuconfig)

### Overview

IR0 uses a Python-based configuration system for kernel subsystem selection. Configuration is managed through an interactive GUI or command-line interface.

### Configuration Interface

#### GUI Mode
```bash
make menuconfig
```

**Features:**
- Tkinter-based GUI (if available)
- Visual subsystem selection
- Real-time configuration preview
- Save/load configuration

#### Configuration Files

**`.config`**
- User configuration file
- Plain text format
- Lists enabled subsystems
- Format: `SUBSYSTEM_ID=1` (enabled) or `SUBSYSTEM_ID=0` (disabled)

**`setup/subsystems.json`**
- Subsystem definitions
- Maps subsystem IDs to source files
- Defines dependencies
- Architecture-specific file lists

**`setup/subsystem_config.h`**
- Generated configuration header
- Included in kernel build
- Defines `ENABLE_*` flags for conditional compilation

**`setup/kernel_config.h`**
- Kernel configuration structure
- Runtime configuration data
- Used by kernel initialization

### Subsystem Configuration

Subsystems are organized by category:

**Core Subsystems (Always Enabled):**
- KERNEL - Core kernel functionality
- MEMORY - Memory management
- INTERRUPT - Interrupt system

**Optional Subsystems:**
- FILESYSTEM - File system support
- NETWORKING - Network stack
- DRIVER_* - Individual driver categories

**Configuration Example:**
```
KERNEL=1
MEMORY=1
INTERRUPT=1
FILESYSTEM=1
NETWORKING=1
DRIVER_STORAGE=1
DRIVER_NET=1
```

### Build Strategy System

Build strategies define different kernel configurations for different deployment scenarios:

**Desktop Strategy (`BUILD_TARGET=desktop`):**
- Full hardware support
- Graphics and audio enabled
- User interface features
- Performance optimizations

**Server Strategy (`BUILD_TARGET=server`):**
- Network focus
- Reliability features
- Minimal GUI
- Resource efficiency

**Embedded Strategy (`BUILD_TARGET=embedded`):**
- Minimal configuration
- Real-time features
- Power management
- Hardware abstraction

**Strategy Selection:**
- Set in Makefile: `BUILD_TARGET := desktop`
- Affects conditional compilation via `CFLAGS_TARGET`

## Disk Image Policy

### Disk Image Creation

**Default Behavior:**
- `make ir0` automatically creates `disk.img` if it doesn't exist
- Default: 200MB MINIX filesystem
- Disk is raw image (not pre-formatted)

**Filesystem Selection:**
- MINIX: Default for `disk.img`
- FAT32: Creates `fat32.img`
- ext4: Creates `ext4.img`

**Auto-Formatting:**
- Kernel automatically formats unformatted disks on first boot
- Format detection: Checks superblock validity
- Format type: Determined by filesystem driver availability

### Init Process Loading

**Init Location:**
- Path: `/sbin/init` on disk image
- Binary: `setup/pid1/init` (must be compiled separately)

**Loading Process:**
1. Compile init binary in `setup/pid1/` directory
2. Copy to `setup/pid1/init`
3. Run `make load-init` (or `sudo ./scripts/load_init.sh`)
4. Script mounts disk and copies init to `/sbin/init`

**Boot Behavior:**
1. Kernel boots and initializes filesystem
2. Attempts to execute `/sbin/init` via `execve()` syscall
3. If init exists and is executable: runs init
4. If init not found: falls back to debug shell

**Multiple Disks:**
- Each filesystem type can have its own disk image
- `load-init` script supports multiple disks:
  ```bash
  sudo ./scripts/load_init.sh fat32 fat32.img
  sudo ./scripts/load_init.sh minix disk.img
  ```

## Multi-Language Driver Support

### Overview

IR0 supports drivers written in C, C++, and Rust through FFI (Foreign Function Interface) bindings.

### C Drivers

**Standard drivers** written in C:
- Full access to kernel APIs
- Direct memory access
- No special requirements

**Compilation:**
- Compiled via standard Makefile rules
- Uses kernel headers and flags
- Linked with kernel binary

### C++ Drivers

**C++ Runtime Support:**
- Location: `cpp/runtime/compat.cpp`
- Wraps C++ `new`/`delete` with kernel allocator
- Provides minimal C++ runtime

**Compilation:**
```bash
make unibuild-cpp cpp/examples/cpp_example.cpp
```

**Features:**
- Exception support disabled (`-fno-exceptions`)
- RTTI disabled (`-fno-rtti`)
- Kernel-compatible allocator

**Limitations:**
- No standard library
- No exceptions
- Manual memory management

### Rust Drivers

**FFI Bindings:**
- Location: `rust/ffi/kernel.rs`
- Provides Rust bindings to kernel APIs
- Safe wrapper functions

**Target Configuration:**
- Location: `rust/x86_64-ir0-kernel.json`
- Custom Rust target for IR0 kernel
- No standard library (`#![no_std]`)

**Compilation:**
```bash
make unibuild-rust rust/drivers/rust_simple_driver.rs
```

**Requirements:**
- Rust toolchain installed
- Custom target configured
- Kernel bindings available

**Features:**
- Memory safety guarantees
- Zero-cost abstractions
- Integration with kernel allocator

### Driver Registration

**Unified Interface:**
All drivers (regardless of language) register via same interface:

```c
struct ir0_driver driver = {
    .name = "example_driver",
    .version = "1.0.0",
    .language = DRIVER_LANGUAGE_C,  // or CXX, or RUST
    .driver_data = NULL
};
ir0_driver_register(&driver);
```

**Example Drivers:**
- `rust/drivers/rust_simple_driver.rs` - Rust example
- `cpp/examples/cpp_example.cpp` - C++ example

**Enabling Example Drivers:**
```bash
make en-ext-drv    # Enable example drivers
make ir0           # Build kernel with example drivers
```

## Build Configuration Variables

### Architecture Selection

**Supported Architectures:**
- `x86-64` (default)
- `x86-32`
- `arm64`
- `arm32`

**Selection:**
```bash
make ir0 arch=x86-64
make ir0 arch=arm64
```

**Effect:**
- Sets `ARCH` variable in Makefile
- Adds `-DARCH_X86_64` (or equivalent) to CFLAGS
- Selects architecture-specific source files

### Build Target

**Build Targets:**
- `desktop` (default)
- `server`
- `embedded`

**Selection:**
- Edit Makefile: `BUILD_TARGET := desktop`
- Affects conditional compilation

### Parallel Builds

**Auto-detection:**
- `make ir0-auto` uses all CPU cores
- Automatically detects `nproc` for job count

**Manual:**
- Not currently configurable via parameter
- Always uses all cores for `ir0-auto`

## Build Information

### Version Information

**Version String:**
- Format: `MAJOR.MINOR.PATCH-SUFFIX`
- Current: `0.0.1-pre-rc3`
- Defined in Makefile: `IR0_VERSION_*` variables

### Build Metadata

**Included in Kernel:**
- Build date (`IR0_BUILD_DATE`)
- Build time (`IR0_BUILD_TIME`)
- Build user (`IR0_BUILD_USER`)
- Build host (`IR0_BUILD_HOST`)
- Compiler version (`IR0_BUILD_CC`)
- Build number (`IR0_BUILD_NUMBER`)

**Access:**
- Via `/proc/version` at runtime
- Auto-incremented on each kernel build

## QEMU Configuration

### Hardware Emulation

**Network:**
- RTL8139 Ethernet card
- User-mode networking (NAT)
- IRQ 11

**Audio:**
- Sound Blaster 16 (`-device sb16`)
- Adlib OPL2 (`-device adlib`)

**Storage:**
- ATA/IDE disk (`-drive file=disk.img,format=raw,if=ide`)
- CD-ROM (ISO boot)

**Serial:**
- COM1 serial port (`-serial stdio`)
- Used for kernel logging

**Input:**
- PS/2 keyboard
- PS/2 mouse

**Display:**
- VGA text mode (default)
- VBE graphics (if enabled)

### QEMU Flags

**Memory:**
- Default: 512MB (`-m 512M`)

**Display Modes:**
- GTK window (default)
- SDL2 (`-display sdl2`)
- None (`-display none`)
- Console (`-nographic`)

**Debug Options:**
- Interrupt logging (`-d int`)
- CPU reset logging (`-d cpu_reset`)
- Guest errors (`-d guest_errors`)
- Execution tracing (`-d exec`)
- Page faults (`-d page`)
- All debug (`-d int,cpu_reset,exec,guest_errors,page`)

**Log File:**
- `-D qemu_debug.log` - Write debug output to file

## Development Workflow

### Typical Development Cycle

1. **Edit Source:**
   ```bash
   vim kernel/process.c
   ```

2. **Quick Compilation Check:**
   ```bash
   make unibuild kernel/process.c
   ```

3. **Full Build:**
   ```bash
   make ir0
   ```

4. **Load Init (if changed):**
   ```bash
   make load-init
   ```

5. **Run Kernel:**
   ```bash
   make run-debug
   ```

6. **Debug Issues:**
   - Check serial console output
   - Review `qemu_debug.log`
   - Use QEMU monitor (telnet port 1234)

### Disk Management Workflow

1. **Create Disk:**
   ```bash
   make create-disk minix 500
   ```

2. **Load Init:**
   ```bash
   sudo make load-init
   ```

3. **Run Kernel:**
   ```bash
   make run
   ```

4. **Modify Init:**
   ```bash
   # Recompile init in setup/pid1/
   sudo make load-init
   make run
   ```

### Multi-Language Driver Development

1. **C++ Driver:**
   ```bash
   make unibuild-cpp cpp/examples/cpp_example.cpp
   make en-ext-drv
   make ir0
   ```

2. **Rust Driver:**
   ```bash
   make unibuild-rust rust/drivers/rust_simple_driver.rs
   make en-ext-drv
   make ir0
   ```

## Troubleshooting

### Common Issues

**"grub-mkrescue: command not found":**
- Install GRUB tools: `apt-get install grub-pc-bin`

**"Permission denied" on load-init:**
- Requires root: `sudo make load-init`

**"Disk image not found":**
- Create disk first: `make create-disk`

**"Init binary not found":**
- Compile init in `setup/pid1/` directory
- Ensure binary is at `setup/pid1/init`

**Rust compilation fails:**
- Install Rust toolchain
- Configure custom target: `rustup target add x86_64-ir0-kernel`

**QEMU errors:**
- Check QEMU installation: `qemu-system-x86_64 --version`
- Verify hardware support: `qemu-system-x86_64 -device help`

---

# Tooling y Sistema de Build del Kernel IR0

## Resumen

El sistema de build del kernel IR0 proporciona herramientas completas para compilar, configurar, probar y desplegar el kernel. Esto incluye targets del Makefile, gestión de configuración via menuconfig, creación de imágenes de disco, carga del proceso init, y soporte de compilación de drivers multi-lenguaje.

## Arquitectura del Sistema de Build

El sistema de build está basado en GNU Make con soporte para:
- **Builds multi-arquitectura** (x86-64, x86-32, ARM64, ARM32)
- **Compilación multi-lenguaje** (C, C++, Rust)
- **Gestión de configuración** (sistema menuconfig)
- **Gestión de imágenes de disco** (crear, formatear, cargar init)
- **Sistema unibuild** (compilación aislada de archivos)
- **Cross-compilación** (Windows via MinGW)

## Targets del Makefile

### Targets de Build Principales

#### `make ir0` o `make all`
Construye el kernel completo:
1. Compila todos los archivos fuente del kernel (C, C++, Rust, Assembly)
2. Enlaza binario del kernel (`kernel-x64.bin`)
3. Crea imagen ISO booteable (`kernel-x64.iso`)
4. Crea imagen de disco virtual (`disk.img`) si no existe

**Uso:**
```bash
make ir0
```

#### `make ir0-auto` o `make auto`
Construye kernel usando todos los núcleos de CPU disponibles para compilación paralela.

**Uso:**
```bash
make ir0-auto
```

#### `make kernel-x64.bin`
Compila y enlaza solo el binario del kernel (sin creación de ISO).

#### `make kernel-x64.iso`
Crea imagen ISO booteable desde binario del kernel.

**Requisitos:**
- `kernel-x64.bin` debe existir
- `grub-mkrescue` debe estar instalado
- Configuración GRUB en `arch/x86-64/grub.cfg`

### Targets de Configuración

#### `make menuconfig`
Lanza menú interactivo de configuración del kernel.

**Características:**
- Interfaz de configuración basada en GUI
- Selección de subsistemas (habilitar/deshabilitar subsistemas del kernel)
- Configuración guardada en archivo `.config`
- Generación dinámica de Makefile basada en configuración

**Uso:**
```bash
make menuconfig
```

**Archivos de Configuración:**
- `.config` - Configuración del usuario (selección de subsistemas)
- `setup/subsystem_config.h` - Header de configuración generado
- `setup/subsystems.json` - Definiciones de subsistemas

**Proceso:**
1. Construye librería kconfig si es necesario
2. Lanza GUI basada en Tkinter
3. Usuario selecciona subsistemas
4. Configuración guardada en `.config`
5. Makefile regenerado dinámicamente

### Gestión de Imágenes de Disco

#### `make create-disk [filesystem] [size]`
Crea una imagen de disco virtual para almacenamiento de filesystem.

**Filesystems Soportados:**
- `minix` - Filesystem MINIX (por defecto, tamaño por defecto 200MB)
- `fat32` - Filesystem FAT32 (tamaño por defecto 200MB)
- `ext4` - Filesystem ext4 (tamaño por defecto 500MB)

**Ejemplos de Uso:**
```bash
make create-disk                    # Crea disco MINIX de 200MB (disk.img)
make create-disk minix 500          # Crea disco MINIX de 500MB
make create-disk fat32              # Crea disco FAT32 (fat32.img, 200MB)
make create-disk fat32 500          # Crea disco FAT32 de 500MB
make create-disk ext4 1000          # Crea disco ext4 de 1GB
```

**Parámetros:**
- `filesystem` - Tipo de filesystem (minix, fat32, ext4)
- `size` - Tamaño del disco en MB (por defecto: 200 para minix/fat32, 500 para ext4)

**Archivos de Salida:**
- `disk.img` - Por defecto para MINIX
- `fat32.img` - Por defecto para FAT32
- `ext4.img` - Por defecto para ext4
- Nombre de archivo personalizado via flag `-o` en script

**Detrás de escena:**
- Llama `scripts/create_disk.sh`
- Crea imagen de disco raw usando `dd`
- Kernel auto-formatea disco en primer arranque si no está formateado

#### `make delete-disk`
Elimina imagen de disco virtual.

**Uso:**
```bash
make delete-disk          # Elimina disk.img
make delete-disk fat32    # Elimina fat32.img
```

**Advertencia:** Esto elimina permanentemente la imagen de disco y todo su contenido.

### Gestión del Proceso Init

#### `make load-init [filesystem] [disk_image]`
Carga binario init en imagen de disco virtual.

**Política del Proceso Init:**
- Binario init debe compilarse por separado y colocarse en `setup/pid1/init`
- Script monta imagen de disco y copia init a `/sbin/init`
- Kernel intenta ejecutar `/sbin/init` al arrancar
- Recurre a shell de debug si init no se encuentra

**Ejemplos de Uso:**
```bash
make load-init                    # Auto-detecta FS, usa disk.img
make load-init fat32              # Usa fat32.img
make load-init disk.img           # Imagen de disco explícita
make load-init fat32 fat32.img    # Filesystem y disco explícitos
```

**Requisitos:**
- Privilegios de root (sudo) requeridos para montar
- Imagen de disco debe estar formateada (via `create-disk`)
- Binario init debe existir en `setup/pid1/init`

**Detrás de escena:**
- Llama `scripts/load_init.sh`
- Monta imagen de disco como dispositivo loop
- Crea directorio `/sbin` si es necesario
- Copia binario init y establece permisos ejecutables
- Desmonta imagen de disco

#### `make remove-init`
Elimina binario init de imagen de disco.

**Uso:**
```bash
make remove-init
```

### Ejecución y Debugging

#### `make run`
Ejecuta kernel en QEMU con GUI y todo el hardware soportado.

**Hardware Emulado:**
- Tarjeta de red RTL8139
- Audio Sound Blaster 16
- Adlib OPL2
- Disco ATA/IDE
- Puerto serial (COM1)
- Teclado/ratón PS/2
- Pantalla VGA

**Uso:**
```bash
make run
```

**Configuración QEMU:**
- Memoria: 512MB
- Display: Ventana GTK
- Serial: stdio (para mensajes del kernel)
- Red: Red en modo usuario (NAT)
- Disco: `disk.img` adjunto como drive IDE

#### `make run-debug`
Ejecuta kernel con salida de debug serial en terminal.

**Características:**
- GUI de QEMU en ventana separada
- Salida serial en terminal actual
- Registro detallado de interrupciones
- Monitor QEMU en puerto telnet 1234

**Uso:**
```bash
make run-debug
```

#### `make run-nodisk`
Ejecuta kernel sin imagen de disco (sin filesystem).

**Uso:**
```bash
make run-nodisk
```

#### `make run-console`
Ejecuta kernel en modo consola (sin GUI).

**Uso:**
```bash
make run-console
```

**Características:**
- Toda la salida al terminal
- Interfaz de consola serial
- Todo el hardware aún emulado

#### `make debug`
Ejecuta kernel con debugging detallado de QEMU.

**Características:**
- Registro de interrupciones
- Registro de reset CPU
- Registro de errores del huésped
- Log de debug escrito en `qemu_debug.log`

**Uso:**
```bash
make debug
```

### Targets de Limpieza

#### `make clean`
Elimina todos los artefactos de build.

**Archivos Limpiados:**
- Archivos objeto (`.o`)
- Binario del kernel (`kernel-x64.bin`)
- Imagen ISO (`kernel-x64.iso`)
- Contenidos del directorio build

**Uso:**
```bash
make clean
```

#### `make unibuild-clean`
Limpia artefactos de unibuild (salidas de compilación aisladas).

**Uso:**
```bash
make unibuild-clean
```

### Testing y Validación

#### `make deptest`
Prueba dependencias requeridas y muestra instrucciones de instalación.

**Verifica:**
- Compilador GCC
- Ensamblador NASM
- Linker GNU (ld)
- Make
- QEMU
- Herramientas GRUB
- Python 3 (para menuconfig)

**Uso:**
```bash
make deptest
```

#### `make help`
Muestra mensaje de ayuda con targets disponibles.

**Uso:**
```bash
make help
```

### Targets de Build para Windows

#### `make windows` o `make win`
Construye kernel para Windows usando cross-compilación MinGW.

**Requisitos:**
- MSYS2 o MinGW-w64
- Makefile específico de Windows (`Makefile.win`)

**Uso:**
```bash
make windows
```

#### `make windows-clean` o `make win-clean`
Limpia artefactos de build de Windows.

**Uso:**
```bash
make windows-clean
```

### Sistema Unibuild

El sistema unibuild permite compilación aislada de archivos individuales sin recompilar todo el kernel.

#### `make unibuild <source_file>`
Compila un único archivo C en aislamiento.

**Uso:**
```bash
make unibuild drivers/IO/ps2.c
make unibuild kernel/process.c
```

**Características:**
- Usa headers y flags del kernel
- Genera archivo objeto `.o`
- Útil para verificación rápida de sintaxis

#### `make unibuild-cpp <source_file>`
Compila un único archivo C++ en aislamiento.

**Uso:**
```bash
make unibuild-cpp cpp/examples/cpp_example.cpp
```

#### `make unibuild-rust <source_file>`
Compila un único archivo Rust en aislamiento.

**Uso:**
```bash
make unibuild-rust rust/drivers/rust_simple_driver.rs
```

**Requisitos:**
- Toolchain Rust con target `x86_64-ir0-kernel`
- Especificación de target en `rust/x86_64-ir0-kernel.json`

**Unibuild para Windows:**
- `make unibuild-win` - Cross-compila archivos C para Windows
- `make unibuild-cpp-win` - Cross-compila archivos C++ para Windows
- `make unibuild-rust-win` - Cross-compila archivos Rust para Windows

### Soporte de Drivers Externos

#### `make en-ext-drv`
Habilita drivers externos de ejemplo (Rust y C++).

**Efecto:**
- Habilita flag `KERNEL_ENABLE_EXAMPLE_DRIVERS`
- Incluye driver Rust de ejemplo (`rust/drivers/rust_simple_driver.rs`)
- Incluye driver C++ de ejemplo (`cpp/examples/cpp_example.cpp`)
- Registra soporte de drivers multi-lenguaje

**Uso:**
```bash
make en-ext-drv
make ir0
```

#### `make dis-ext-drv`
Deshabilita drivers externos de ejemplo.

**Uso:**
```bash
make dis-ext-drv
```

#### `make test-drivers`
Compila drivers de prueba (Rust y C++).

**Uso:**
```bash
make test-drivers
```

## Scripts

### Scripts de Gestión de Disco

#### `scripts/create_disk.sh`
Crea imágenes de disco virtual para filesystems IR0.

**Uso:**
```bash
./scripts/create_disk.sh [filesystem] [size] [-o output_file]
```

**Ejemplos:**
```bash
./scripts/create_disk.sh              # Disco MINIX de 200MB
./scripts/create_disk.sh minix 500    # Disco MINIX de 500MB
./scripts/create_disk.sh fat32        # Disco FAT32
./scripts/create_disk.sh -f fat32 -s 500 -o custom.img
```

**Filesystems Soportados:**
- `minix` - Filesystem MINIX (por defecto)
- `fat32` - Filesystem FAT32
- `ext4` - Filesystem ext4

**Características:**
- Auto-detecta nombre de archivo de disco por defecto desde tipo de filesystem
- Hace backup de disco existente si está presente
- Muestra barra de progreso con `pv` si está disponible

#### `scripts/load_init.sh`
Carga binario init en imagen de disco virtual.

**Uso:**
```bash
sudo ./scripts/load_init.sh [filesystem] [disk_image] [init_binary]
```

**Ejemplos:**
```bash
sudo ./scripts/load_init.sh                    # Auto-detecta FS, usa disk.img
sudo ./scripts/load_init.sh fat32              # Usa fat32.img
sudo ./scripts/load_init.sh disk.img           # Disco explícito
sudo ./scripts/load_init.sh fat32 fat32.img /path/to/init
```

**Características:**
- Auto-detecta filesystem desde nombre de archivo de disco
- Crea directorio `/sbin` si es necesario
- Establece permisos ejecutables
- Verifica que archivo fue copiado exitosamente

**Requisitos:**
- Privilegios de root (sudo)
- Imagen de disco formateada
- Binario init debe existir

#### `scripts/delete_disk.sh`
Elimina imágenes de disco virtual.

**Uso:**
```bash
./scripts/delete_disk.sh [disk_image]
```

### Scripts de Build

#### `scripts/unibuild.sh`
Script de build unificado para compilación aislada de archivos.

**Uso:**
```bash
./scripts/unibuild.sh [-win] [-rust|-cpp] <source_file> [source_file2] ...
```

**Ejemplos:**
```bash
./scripts/unibuild.sh kernel/process.c
./scripts/unibuild.sh -cpp cpp/examples/cpp_example.cpp
./scripts/unibuild.sh -rust rust/drivers/rust_simple_driver.rs
./scripts/unibuild.sh -win drivers/IO/ps2.c  # Cross-compila para Windows
```

**Características:**
- Soporta C, C++, y Rust
- Compilación nativa y cross-compilación para Windows
- Compilación de múltiples archivos
- Detección automática de dependencias

#### `scripts/unibuild-clean.sh`
Limpia artefactos de unibuild.

**Uso:**
```bash
./scripts/unibuild-clean.sh
```

### Scripts de Testing

#### `scripts/test.sh`
Ejecuta tests del kernel.

**Uso:**
```bash
./scripts/test.sh
```

#### `scripts/test_userspace.sh`
Prueba programas de userspace.

**Uso:**
```bash
./scripts/test_userspace.sh
```

#### `scripts/quick_run.sh`
Script de ejecución rápida del kernel.

**Uso:**
```bash
./scripts/quick_run.sh
```

### Scripts de Utilidad

#### `scripts/cloc.sh`
Cuenta líneas de código.

**Uso:**
```bash
./scripts/cloc.sh
```

**Requisitos:**
- Herramienta `cloc` instalada

#### `scripts/deptest.sh`
Prueba dependencias de build.

**Uso:**
```bash
./scripts/deptest.sh
```

**Llamado por:** `make deptest`

#### `scripts/build_simple.sh`
Script de build simple (alternativa al Makefile).

**Uso:**
```bash
./scripts/build_simple.sh
```

## Sistema de Configuración (menuconfig)

### Resumen

IR0 usa un sistema de configuración basado en Python para selección de subsistemas del kernel. La configuración se gestiona a través de una GUI interactiva o interfaz de línea de comandos.

### Interfaz de Configuración

#### Modo GUI
```bash
make menuconfig
```

**Características:**
- GUI basada en Tkinter (si está disponible)
- Selección visual de subsistemas
- Vista previa de configuración en tiempo real
- Guardar/cargar configuración

#### Archivos de Configuración

**`.config`**
- Archivo de configuración del usuario
- Formato texto plano
- Lista subsistemas habilitados
- Formato: `SUBSYSTEM_ID=1` (habilitado) o `SUBSYSTEM_ID=0` (deshabilitado)

**`setup/subsystems.json`**
- Definiciones de subsistemas
- Mapea IDs de subsistemas a archivos fuente
- Define dependencias
- Listas de archivos específicas de arquitectura

**`setup/subsystem_config.h`**
- Header de configuración generado
- Incluido en build del kernel
- Define flags `ENABLE_*` para compilación condicional

**`setup/kernel_config.h`**
- Estructura de configuración del kernel
- Datos de configuración en runtime
- Usado por inicialización del kernel

### Configuración de Subsistemas

Los subsistemas están organizados por categoría:

**Subsistemas Core (Siempre Habilitados):**
- KERNEL - Funcionalidad core del kernel
- MEMORY - Gestión de memoria
- INTERRUPT - Sistema de interrupciones

**Subsistemas Opcionales:**
- FILESYSTEM - Soporte de sistema de archivos
- NETWORKING - Stack de red
- DRIVER_* - Categorías individuales de drivers

**Ejemplo de Configuración:**
```
KERNEL=1
MEMORY=1
INTERRUPT=1
FILESYSTEM=1
NETWORKING=1
DRIVER_STORAGE=1
DRIVER_NET=1
```

### Sistema de Estrategias de Build

Las estrategias de build definen diferentes configuraciones del kernel para diferentes escenarios de despliegue:

**Estrategia Desktop (`BUILD_TARGET=desktop`):**
- Soporte completo de hardware
- Gráficos y audio habilitados
- Características de interfaz de usuario
- Optimizaciones de rendimiento

**Estrategia Server (`BUILD_TARGET=server`):**
- Enfoque en red
- Características de confiabilidad
- GUI mínima
- Eficiencia de recursos

**Estrategia Embedded (`BUILD_TARGET=embedded`):**
- Configuración mínima
- Características de tiempo real
- Gestión de energía
- Abstracción de hardware

**Selección de Estrategia:**
- Establecido en Makefile: `BUILD_TARGET := desktop`
- Afecta compilación condicional via `CFLAGS_TARGET`

## Política de Imágenes de Disco

### Creación de Imágenes de Disco

**Comportamiento por Defecto:**
- `make ir0` automáticamente crea `disk.img` si no existe
- Por defecto: Filesystem MINIX de 200MB
- Disco es imagen raw (no pre-formateada)

**Selección de Filesystem:**
- MINIX: Por defecto para `disk.img`
- FAT32: Crea `fat32.img`
- ext4: Crea `ext4.img`

**Auto-formateo:**
- Kernel automáticamente formatea discos no formateados en primer arranque
- Detección de formato: Verifica validez del superbloque
- Tipo de formato: Determinado por disponibilidad de driver de filesystem

### Carga del Proceso Init

**Ubicación de Init:**
- Ruta: `/sbin/init` en imagen de disco
- Binario: `setup/pid1/init` (debe compilarse por separado)

**Proceso de Carga:**
1. Compilar binario init en directorio `setup/pid1/`
2. Copiar a `setup/pid1/init`
3. Ejecutar `make load-init` (o `sudo ./scripts/load_init.sh`)
4. Script monta disco y copia init a `/sbin/init`

**Comportamiento de Arranque:**
1. Kernel arranca e inicializa filesystem
2. Intenta ejecutar `/sbin/init` via syscall `execve()`
3. Si init existe y es ejecutable: ejecuta init
4. Si init no se encuentra: recurre a shell de debug

**Múltiples Discos:**
- Cada tipo de filesystem puede tener su propia imagen de disco
- Script `load-init` soporta múltiples discos:
  ```bash
  sudo ./scripts/load_init.sh fat32 fat32.img
  sudo ./scripts/load_init.sh minix disk.img
  ```

## Soporte de Drivers Multi-Lenguaje

### Resumen

IR0 soporta drivers escritos en C, C++, y Rust a través de bindings FFI (Foreign Function Interface).

### Drivers C

**Drivers estándar** escritos en C:
- Acceso completo a APIs del kernel
- Acceso directo a memoria
- Sin requisitos especiales

**Compilación:**
- Compilado via reglas estándar del Makefile
- Usa headers y flags del kernel
- Enlazado con binario del kernel

### Drivers C++

**Soporte de Runtime C++:**
- Ubicación: `cpp/runtime/compat.cpp`
- Envuelve `new`/`delete` C++ con asignador del kernel
- Proporciona runtime C++ mínimo

**Compilación:**
```bash
make unibuild-cpp cpp/examples/cpp_example.cpp
```

**Características:**
- Soporte de excepciones deshabilitado (`-fno-exceptions`)
- RTTI deshabilitado (`-fno-rtti`)
- Asignador compatible con kernel

**Limitaciones:**
- Sin librería estándar
- Sin excepciones
- Gestión manual de memoria

### Drivers Rust

**Bindings FFI:**
- Ubicación: `rust/ffi/kernel.rs`
- Proporciona bindings Rust a APIs del kernel
- Funciones wrapper seguras

**Configuración de Target:**
- Ubicación: `rust/x86_64-ir0-kernel.json`
- Target Rust personalizado para kernel IR0
- Sin librería estándar (`#![no_std]`)

**Compilación:**
```bash
make unibuild-rust rust/drivers/rust_simple_driver.rs
```

**Requisitos:**
- Toolchain Rust instalado
- Target personalizado configurado
- Bindings del kernel disponibles

**Características:**
- Garantías de seguridad de memoria
- Abstracciones de costo cero
- Integración con asignador del kernel

### Registro de Drivers

**Interfaz Unificada:**
Todos los drivers (independientemente del lenguaje) se registran via la misma interfaz:

```c
struct ir0_driver driver = {
    .name = "example_driver",
    .version = "1.0.0",
    .language = DRIVER_LANGUAGE_C,  // o CXX, o RUST
    .driver_data = NULL
};
ir0_driver_register(&driver);
```

**Drivers de Ejemplo:**
- `rust/drivers/rust_simple_driver.rs` - Ejemplo Rust
- `cpp/examples/cpp_example.cpp` - Ejemplo C++

**Habilitando Drivers de Ejemplo:**
```bash
make en-ext-drv    # Habilita drivers de ejemplo
make ir0           # Construye kernel con drivers de ejemplo
```

## Variables de Configuración de Build

### Selección de Arquitectura

**Arquitecturas Soportadas:**
- `x86-64` (por defecto)
- `x86-32`
- `arm64`
- `arm32`

**Selección:**
```bash
make ir0 arch=x86-64
make ir0 arch=arm64
```

**Efecto:**
- Establece variable `ARCH` en Makefile
- Añade `-DARCH_X86_64` (o equivalente) a CFLAGS
- Selecciona archivos fuente específicos de arquitectura

### Build Target

**Build Targets:**
- `desktop` (por defecto)
- `server`
- `embedded`

**Selección:**
- Editar Makefile: `BUILD_TARGET := desktop`
- Afecta compilación condicional

### Builds Paralelos

**Auto-detección:**
- `make ir0-auto` usa todos los núcleos de CPU
- Automáticamente detecta `nproc` para conteo de jobs

**Manual:**
- Actualmente no configurable via parámetro
- Siempre usa todos los núcleos para `ir0-auto`

## Información de Build

### Información de Versión

**Cadena de Versión:**
- Formato: `MAJOR.MINOR.PATCH-SUFFIX`
- Actual: `0.0.1-pre-rc3`
- Definido en Makefile: variables `IR0_VERSION_*`

### Metadatos de Build

**Incluidos en Kernel:**
- Fecha de build (`IR0_BUILD_DATE`)
- Hora de build (`IR0_BUILD_TIME`)
- Usuario de build (`IR0_BUILD_USER`)
- Host de build (`IR0_BUILD_HOST`)
- Versión del compilador (`IR0_BUILD_CC`)
- Número de build (`IR0_BUILD_NUMBER`)

**Acceso:**
- Via `/proc/version` en runtime
- Auto-incrementado en cada build del kernel

## Configuración QEMU

### Emulación de Hardware

**Red:**
- Tarjeta Ethernet RTL8139
- Red en modo usuario (NAT)
- IRQ 11

**Audio:**
- Sound Blaster 16 (`-device sb16`)
- Adlib OPL2 (`-device adlib`)

**Almacenamiento:**
- Disco ATA/IDE (`-drive file=disk.img,format=raw,if=ide`)
- CD-ROM (arranque ISO)

**Serial:**
- Puerto serial COM1 (`-serial stdio`)
- Usado para logging del kernel

**Entrada:**
- Teclado PS/2
- Ratón PS/2

**Display:**
- Modo texto VGA (por defecto)
- Gráficos VBE (si habilitado)

### Flags QEMU

**Memoria:**
- Por defecto: 512MB (`-m 512M`)

**Modos de Display:**
- Ventana GTK (por defecto)
- SDL2 (`-display sdl2`)
- Ninguno (`-display none`)
- Consola (`-nographic`)

**Opciones de Debug:**
- Registro de interrupciones (`-d int`)
- Registro de reset CPU (`-d cpu_reset`)
- Errores del huésped (`-d guest_errors`)
- Rastreo de ejecución (`-d exec`)
- Fallos de página (`-d page`)
- Todo debug (`-d int,cpu_reset,exec,guest_errors,page`)

**Archivo de Log:**
- `-D qemu_debug.log` - Escribe salida de debug en archivo

## Flujo de Trabajo de Desarrollo

### Ciclo de Desarrollo Típico

1. **Editar Fuente:**
   ```bash
   vim kernel/process.c
   ```

2. **Verificación Rápida de Compilación:**
   ```bash
   make unibuild kernel/process.c
   ```

3. **Build Completo:**
   ```bash
   make ir0
   ```

4. **Cargar Init (si cambió):**
   ```bash
   make load-init
   ```

5. **Ejecutar Kernel:**
   ```bash
   make run-debug
   ```

6. **Debuggear Problemas:**
   - Verificar salida de consola serial
   - Revisar `qemu_debug.log`
   - Usar monitor QEMU (puerto telnet 1234)

### Flujo de Trabajo de Gestión de Disco

1. **Crear Disco:**
   ```bash
   make create-disk minix 500
   ```

2. **Cargar Init:**
   ```bash
   sudo make load-init
   ```

3. **Ejecutar Kernel:**
   ```bash
   make run
   ```

4. **Modificar Init:**
   ```bash
   # Recompilar init en setup/pid1/
   sudo make load-init
   make run
   ```

### Desarrollo de Drivers Multi-Lenguaje

1. **Driver C++:**
   ```bash
   make unibuild-cpp cpp/examples/cpp_example.cpp
   make en-ext-drv
   make ir0
   ```

2. **Driver Rust:**
   ```bash
   make unibuild-rust rust/drivers/rust_simple_driver.rs
   make en-ext-drv
   make ir0
   ```

## Troubleshooting

### Problemas Comunes

**"grub-mkrescue: command not found":**
- Instalar herramientas GRUB: `apt-get install grub-pc-bin`

**"Permission denied" en load-init:**
- Requiere root: `sudo make load-init`

**"Disk image not found":**
- Crear disco primero: `make create-disk`

**"Init binary not found":**
- Compilar init en directorio `setup/pid1/`
- Asegurar que binario está en `setup/pid1/init`

**Fallo de compilación Rust:**
- Instalar toolchain Rust
- Configurar target personalizado: `rustup target add x86_64-ir0-kernel`

**Errores QEMU:**
- Verificar instalación QEMU: `qemu-system-x86_64 --version`
- Verificar soporte de hardware: `qemu-system-x86_64 -device help`

