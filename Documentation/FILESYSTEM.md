# IR0 Kernel Filesystem Subsystem

## Overview

The IR0 kernel implements a Virtual File System (VFS) abstraction layer that provides a unified interface to multiple filesystem types, both virtual (pseudo-filesystems) and physical (disk-based). The design follows the Unix philosophy of "everything is a file," allowing kernel objects and devices to be accessed through standard file operations.

## Architecture

The filesystem subsystem consists of three main layers:

1. **VFS Layer** (`fs/vfs.c`, `fs/vfs.h`) - Abstract interface and mount point management
2. **Filesystem Implementations** - Concrete filesystem drivers (MINIX, RAMFS, TMPFS, PROCFS, DEVFS)
3. **Storage Drivers** - Low-level disk access (ATA driver)

## Virtual File System (VFS)

The VFS layer provides abstraction between user-space file operations and filesystem-specific implementations. Key components:

- **Inode operations** - File lookup, creation, directory operations
- **File operations** - Read, write, seek, directory reading
- **Superblock operations** - Filesystem-level operations
- **Mount point management** - Unified namespace across multiple filesystems

### Key Structures

- `vfs_inode` - Represents a file or directory
- `vfs_superblock` - Filesystem metadata
- `file_operations` - File-level operations (read, write, seek)
- `inode_operations` - Directory-level operations (lookup, create, mkdir)
- `mount_point` - Mount point information

## Pseudo-Filesystems

### /proc Filesystem

The `/proc` filesystem provides read-only access to kernel and process information through virtual files. Files are dynamically generated on access.

#### System Information Files

- `/proc/meminfo` - Memory usage statistics (physical memory, heap, frames)
- `/proc/ps` - Process list (PID, state, names)
- `/proc/netinfo` - Network interface information and ARP cache
- `/proc/drivers` - Registered kernel drivers (name, version, language)
- `/proc/status` - Current process status (state, PID, memory)
- `/proc/uptime` - System uptime in seconds
- `/proc/version` - Kernel version and build information
- `/proc/cpuinfo` - CPU and architecture information
- `/proc/loadavg` - System load average statistics
- `/proc/filesystems` - List of registered filesystems
- `/proc/cmdline` - Process command line arguments
- `/proc/blockdevices` - Block device information
- `/proc/partitions` - Disk partition information (MBR/GPT)
- `/proc/mounts` - Mounted filesystems list
- `/proc/interrupts` - Interrupt statistics (IRQ usage)
- `/proc/iomem` - I/O memory map (placeholder)
- `/proc/ioports` - I/O port allocation (placeholder)
- `/proc/modules` - Loaded kernel drivers/modules
- `/proc/timer_list` - Active timer information

#### Process-Specific Files

- `/proc/[pid]/status` - Process status by PID
- `/proc/[pid]/cmdline` - Process command line by PID

#### Implementation

- Location: `fs/procfs.c`
- Uses special file descriptors (1000-1018) for different virtual files
- Content generated dynamically by dedicated functions (e.g., `proc_meminfo_read()`)
- Supports offset-based reading for large files

### /dev Filesystem

The `/dev` filesystem provides access to hardware devices through special device files.

#### Standard Device Nodes

- `/dev/null` - Null device (read: EOF, write: discards data)
- `/dev/zero` - Zero device (read: returns zeros, write: discards)
- `/dev/console` - System console (write: VGA display)
- `/dev/tty` - Current terminal (same as console)
- `/dev/kmsg` - Kernel message buffer (4KB circular buffer, read/write)
- `/dev/audio` - Audio device (Sound Blaster 16, IOCTL: volume control)
- `/dev/mouse` - Mouse device (PS/2 mouse, read: x/y/buttons, IOCTL: sensitivity)
- `/dev/net` - Network device (IOCTL: ping, network configuration)
- `/dev/disk` - Disk device (read/write sectors, IOCTL: geometry)

#### Implementation

- Location: `fs/devfs.c`
- Device nodes registered via `devfs_register_device()`
- Supports read, write, and IOCTL operations
- Polymorphic interface - devices accessed via standard file operations

### RAMFS Filesystem

Simple in-memory filesystem for boot files and temporary data. All data stored in kernel memory, lost on reboot.

- Maximum 64 files
- Maximum 4KB per file
- Maximum 255 characters per filename
- No directory hierarchy

**Implementation**: `fs/ramfs.c`

### TMPFS Filesystem

In-memory filesystem optimized for temporary directories. More structured than RAMFS with inode-based design.

- Maximum 128 files
- Maximum 64KB per file
- Maximum 32 directories
- Supports directory hierarchy
- Inode-based structure

**Implementation**: `fs/tmpfs.c`

## Physical Filesystems

### MINIX Filesystem

Traditional Unix-like filesystem for persistent disk storage. Based on MINIX v1/v2 filesystem design.

#### Features

- Inode-based structure
- Directory and file support
- Fixed-size blocks (1KB)
- Bitmap-based allocation (inodes and zones)
- Root inode at inode 1
- Maximum file size: Limited by zone addressing

#### Operations

- `minix_read_inode()` - Read inode from disk
- `minix_write_inode()` - Write inode to disk
- `minix_fs_mkdir()` - Create directory
- `minix_fs_open()` - Open file
- `minix_fs_read()` - Read file
- `minix_fs_write()` - Write file
- `minix_fs_unlink()` - Remove file/directory

#### Implementation

- Location: `fs/minix_fs.c`, `fs/minix_fs.h`
- Mounts at `/` as root filesystem
- Device: `/dev/hda` (first ATA drive)
- Auto-formats if superblock invalid or disk uninitialized

#### Disk Layout

```
Block 0: Boot block (unused)
Block 1: Superblock
Block 2: Inode bitmap (first block)
Block N: Zone bitmap (first block)
Block M: Inode table
Block K: Data zones
```

## Filesystem Registration

Filesystems register themselves with the VFS layer:

```c
struct filesystem_type minix_fs_type = {
    .name = "minix",
    .mount = minix_fs_mount
};

register_filesystem(&minix_fs_type);
```

## Mount Points

Mount points are managed by the VFS layer:

- Root filesystem mounted at `/`
- Virtual filesystems (`/proc`, `/dev`) are always available
- Physical filesystems require device and mount point

## File Operations

All filesystems support standard POSIX-like operations:

- `open()` - Open file or directory
- `read()` - Read from file
- `write()` - Write to file
- `close()` - Close file descriptor
- `stat()` - Get file metadata
- `mkdir()` - Create directory
- `unlink()` - Remove file/directory

## Error Handling

Filesystem operations return standard error codes:

- `0` - Success
- `-ENOENT` - File not found
- `-ENOMEM` - Out of memory
- `-ENODEV` - Device not available
- `-EINVAL` - Invalid parameter
- `-ENOSPC` - No space left (for `/dev/full`)

All errors are logged to serial console for debugging.

## Implementation Notes

- VFS maintains unified namespace across all filesystems
- Virtual filesystems don't require disk storage
- Files in `/proc` and `/dev` are dynamically generated
- MINIX filesystem requires ATA storage to be available
- Disk availability is checked before mounting

## Filesystem Type Summary

| Filesystem | Type | Storage | Mount Point | Purpose |
|-----------|------|---------|-------------|---------|
| `proc` | Virtual | Dynamic | `/proc` | Kernel/process information |
| `devfs` | Virtual | Dynamic | `/dev` | Device access |
| `ramfs` | Virtual | Memory | Any | Simple temporary storage |
| `tmpfs` | Virtual | Memory | Any | Structured temporary storage |
| `minix` | Physical | Disk | Any | Persistent disk storage |

---

# Subsistema de Archivos del Kernel IR0

## Resumen

El kernel IR0 implementa una capa de abstracción de Sistema de Archivos Virtual (VFS) que proporciona una interfaz unificada a múltiples tipos de sistemas de archivos, tanto virtuales (pseudo-sistemas de archivos) como físicos (basados en disco). El diseño sigue la filosofía Unix de "todo es un archivo", permitiendo que objetos del kernel y dispositivos sean accedidos a través de operaciones estándar de archivo.

## Arquitectura

El subsistema de archivos consta de tres capas principales:

1. **Capa VFS** (`fs/vfs.c`, `fs/vfs.h`) - Interfaz abstracta y gestión de puntos de montaje
2. **Implementaciones de Sistemas de Archivos** - Drivers concretos (MINIX, RAMFS, TMPFS, PROCFS, DEVFS)
3. **Drivers de Almacenamiento** - Acceso de bajo nivel a disco (driver ATA)

## Sistema de Archivos Virtual (VFS)

La capa VFS proporciona abstracción entre las operaciones de archivo del espacio de usuario y las implementaciones específicas de cada sistema de archivos. Componentes clave:

- **Operaciones de inodo** - Búsqueda de archivos, creación, operaciones de directorio
- **Operaciones de archivo** - Leer, escribir, buscar, leer directorio
- **Operaciones de superbloque** - Operaciones a nivel de sistema de archivos
- **Gestión de puntos de montaje** - Espacio de nombres unificado entre múltiples sistemas de archivos

### Estructuras Clave

- `vfs_inode` - Representa un archivo o directorio
- `vfs_superblock` - Metadatos del sistema de archivos
- `file_operations` - Operaciones a nivel de archivo (leer, escribir, buscar)
- `inode_operations` - Operaciones a nivel de directorio (búsqueda, crear, mkdir)
- `mount_point` - Información de punto de montaje

## Pseudo-Sistemas de Archivos

### Sistema de Archivos /proc

El sistema de archivos `/proc` proporciona acceso de solo lectura a información del kernel y procesos a través de archivos virtuales. Los archivos se generan dinámicamente al acceder.

#### Archivos de Información del Sistema

- `/proc/meminfo` - Estadísticas de uso de memoria (memoria física, heap, frames)
- `/proc/ps` - Lista de procesos (PID, estado, nombres)
- `/proc/netinfo` - Información de interfaces de red y caché ARP
- `/proc/drivers` - Drivers del kernel registrados (nombre, versión, lenguaje)
- `/proc/status` - Estado del proceso actual (estado, PID, memoria)
- `/proc/uptime` - Tiempo de actividad del sistema en segundos
- `/proc/version` - Versión del kernel e información de compilación
- `/proc/cpuinfo` - Información de CPU y arquitectura
- `/proc/loadavg` - Estadísticas de carga promedio del sistema
- `/proc/filesystems` - Lista de sistemas de archivos registrados
- `/proc/cmdline` - Argumentos de línea de comandos del proceso
- `/proc/blockdevices` - Información de dispositivos de bloque
- `/proc/partitions` - Información de particiones de disco (MBR/GPT)
- `/proc/mounts` - Lista de sistemas de archivos montados
- `/proc/interrupts` - Estadísticas de interrupciones (uso de IRQ)
- `/proc/iomem` - Mapa de memoria I/O (placeholder)
- `/proc/ioports` - Asignación de puertos I/O (placeholder)
- `/proc/modules` - Drivers/módulos del kernel cargados
- `/proc/timer_list` - Información de timers activos

#### Archivos Específicos de Proceso

- `/proc/[pid]/status` - Estado del proceso por PID
- `/proc/[pid]/cmdline` - Línea de comandos del proceso por PID

#### Implementación

- Ubicación: `fs/procfs.c`
- Usa descriptores de archivo especiales (1000-1018) para diferentes archivos virtuales
- Contenido generado dinámicamente por funciones dedicadas (ej: `proc_meminfo_read()`)
- Soporta lectura basada en offset para archivos grandes

### Sistema de Archivos /dev

El sistema de archivos `/dev` proporciona acceso a dispositivos de hardware a través de archivos de dispositivo especiales.

#### Nodos de Dispositivo Estándar

- `/dev/null` - Dispositivo nulo (leer: EOF, escribir: descarta datos)
- `/dev/zero` - Dispositivo cero (leer: devuelve ceros, escribir: descarta)
- `/dev/console` - Consola del sistema (escribir: pantalla VGA)
- `/dev/tty` - Terminal actual (igual que consola)
- `/dev/kmsg` - Buffer de mensajes del kernel (buffer circular de 4KB, leer/escribir)
- `/dev/audio` - Dispositivo de audio (Sound Blaster 16, IOCTL: control de volumen)
- `/dev/mouse` - Dispositivo de mouse (mouse PS/2, leer: x/y/botones, IOCTL: sensibilidad)
- `/dev/net` - Dispositivo de red (IOCTL: ping, configuración de red)
- `/dev/disk` - Dispositivo de disco (leer/escribir sectores, IOCTL: geometría)

#### Implementación

- Ubicación: `fs/devfs.c`
- Nodos de dispositivo registrados via `devfs_register_device()`
- Soporta operaciones read, write e IOCTL
- Interfaz polimórfica - dispositivos accedidos mediante operaciones estándar de archivo

### Sistema de Archivos RAMFS

Sistema de archivos simple en memoria para archivos de arranque y datos temporales. Todos los datos almacenados en memoria del kernel, se pierden al reiniciar.

- Máximo 64 archivos
- Máximo 4KB por archivo
- Máximo 255 caracteres por nombre de archivo
- Sin jerarquía de directorios

**Implementación**: `fs/ramfs.c`

### Sistema de Archivos TMPFS

Sistema de archivos en memoria optimizado para directorios temporales. Más estructurado que RAMFS con diseño basado en inodos.

- Máximo 128 archivos
- Máximo 64KB por archivo
- Máximo 32 directorios
- Soporta jerarquía de directorios
- Estructura basada en inodos

**Implementación**: `fs/tmpfs.c`

## Sistemas de Archivos Físicos

### Sistema de Archivos MINIX

Sistema de archivos tradicional estilo Unix para almacenamiento persistente en disco. Basado en el diseño de sistema de archivos MINIX v1/v2.

#### Características

- Estructura basada en inodos
- Soporte de directorios y archivos
- Bloques de tamaño fijo (1KB)
- Asignación basada en bitmap (inodos y zonas)
- Inodo raíz en inodo 1
- Tamaño máximo de archivo: Limitado por direccionamiento de zonas

#### Operaciones

- `minix_read_inode()` - Leer inodo del disco
- `minix_write_inode()` - Escribir inodo al disco
- `minix_fs_mkdir()` - Crear directorio
- `minix_fs_open()` - Abrir archivo
- `minix_fs_read()` - Leer archivo
- `minix_fs_write()` - Escribir archivo
- `minix_fs_unlink()` - Eliminar archivo/directorio

#### Implementación

- Ubicación: `fs/minix_fs.c`, `fs/minix_fs.h`
- Se monta en `/` como sistema de archivos raíz
- Dispositivo: `/dev/hda` (primer disco ATA)
- Auto-formatea si el superbloque es inválido o el disco no está inicializado

#### Layout del Disco

```
Bloque 0: Bloque de arranque (no usado)
Bloque 1: Superbloque
Bloque 2: Bitmap de inodos (primer bloque)
Bloque N: Bitmap de zonas (primer bloque)
Bloque M: Tabla de inodos
Bloque K: Zonas de datos
```

## Registro de Sistemas de Archivos

Los sistemas de archivos se registran con la capa VFS:

```c
struct filesystem_type minix_fs_type = {
    .name = "minix",
    .mount = minix_fs_mount
};

register_filesystem(&minix_fs_type);
```

## Puntos de Montaje

Los puntos de montaje son gestionados por la capa VFS:

- Sistema de archivos raíz montado en `/`
- Sistemas de archivos virtuales (`/proc`, `/dev`) siempre disponibles
- Sistemas de archivos físicos requieren dispositivo y punto de montaje

## Operaciones de Archivo

Todos los sistemas de archivos soportan operaciones estándar tipo POSIX:

- `open()` - Abrir archivo o directorio
- `read()` - Leer de archivo
- `write()` - Escribir a archivo
- `close()` - Cerrar descriptor de archivo
- `stat()` - Obtener metadatos de archivo
- `mkdir()` - Crear directorio
- `unlink()` - Eliminar archivo/directorio

## Manejo de Errores

Las operaciones de sistema de archivos devuelven códigos de error estándar:

- `0` - Éxito
- `-ENOENT` - Archivo no encontrado
- `-ENOMEM` - Sin memoria
- `-ENODEV` - Dispositivo no disponible
- `-EINVAL` - Parámetro inválido
- `-ENOSPC` - Sin espacio (para `/dev/full`)

Todos los errores se registran en la consola serial para debugging.

## Notas de Implementación

- VFS mantiene espacio de nombres unificado entre todos los sistemas de archivos
- Los sistemas de archivos virtuales no requieren almacenamiento en disco
- Los archivos en `/proc` y `/dev` se generan dinámicamente
- El sistema de archivos MINIX requiere que el almacenamiento ATA esté disponible
- Se verifica la disponibilidad del disco antes de montar

## Resumen de Tipos de Sistema de Archivos

| Sistema de Archivos | Tipo | Almacenamiento | Punto de Montaje | Propósito |
|---------------------|------|----------------|------------------|-----------|
| `proc` | Virtual | Dinámico | `/proc` | Información del kernel/procesos |
| `devfs` | Virtual | Dinámico | `/dev` | Acceso a dispositivos |
| `ramfs` | Virtual | Memoria | Cualquiera | Almacenamiento temporal simple |
| `tmpfs` | Virtual | Memoria | Cualquiera | Almacenamiento temporal estructurado |
| `minix` | Físico | Disco | Cualquiera | Almacenamiento persistente en disco |

