# File System Subsystem

## English

### Overview
The File System Subsystem provides a filesystem framework for the IR0 kernel with the IR0FS filesystem structure and a Virtual File System (VFS) layer. It includes basic filesystem functionality with a framework for advanced features like journaling, compression, and integrity checks.

### Key Components

#### 1. IR0FS - File System (`ir0fs.c/h`)
- **Purpose**: Filesystem structure with framework for advanced features
- **Features**:
  - **Journaling Framework**: Structure for atomic transactions and crash recovery
  - **Compression Framework**: Framework for multi-algorithm compression (LZ4, ZSTD, LZMA)
  - **Integrity Framework**: Framework for CRC32 checksums per block
  - **Large File Support**: Framework for files up to 1PB
  - **High Capacity**: Framework for 1,000,000 files per filesystem
  - **Advanced Features**: Framework for defragmentation, health monitoring, optimization

#### 2. Virtual File System (`vfs.c/h`)
- **Purpose**: Abstraction layer for multiple filesystem types
- **Features**:
  - Filesystem registration and management
  - Mount point handling
  - Path resolution and traversal
  - File descriptor management
  - Directory operations
  - File operations (open, close, read, write, seek)

#### 3. Simple VFS Implementation (`vfs_simple.c/h`)
- **Purpose**: Simplified VFS for basic operations
- **Features**:
  - Basic file operations
  - Simple directory handling
  - Memory-based filesystem
  - Debugging and testing support

#### 4. File System Tools (`tools/`)
- **Purpose**: Utilities for filesystem management
- **Features**:
  - Filesystem creation and formatting framework
  - Health checking and repair framework
  - Performance analysis framework
  - Debugging tools

### IR0FS Technical Specifications

#### File System Layout
```
Superblock (4KB)
├── Filesystem metadata
├── Journal information framework
├── Compression settings framework
└── Integrity checksums framework

Inode Table
├── File metadata (256 bytes per inode)
├── Extended attributes framework
├── Compression information framework
└── Checksum data framework

Data Blocks (4KB each)
├── File data
├── Directory entries (64 bytes each)
├── Journal entries framework
└── Compression metadata framework
```

#### Framework Features

1. **Journaling System Framework**
   - Structure for atomic transactions
   - Framework for automatic crash recovery
   - Logging with rollback capability framework
   - Post-crash integrity verification framework

2. **Compression Framework**
   - **LZ4**: Framework for maximum speed (2:1 compression)
   - **ZSTD**: Framework for balanced speed/compression (3:1)
   - **LZMA**: Framework for maximum compression (10:1)
   - Framework for automatic algorithm selection

3. **Data Integrity Framework**
   - CRC32 checksums framework per block
   - Framework for automatic verification on read
   - Corruption detection framework
   - Framework for automatic recovery

4. **Scalability Framework**
   - Framework for files up to 1PB
   - Framework for 1,000,000 files per filesystem
   - Framework for automatic defragmentation
   - Framework for access optimization

### VFS Architecture

#### Mount Point Management
```c
// Mount a filesystem
int mount(const char* source, const char* target, const char* fstype);

// Unmount a filesystem
int umount(const char* target);

// List mounted filesystems
void list_mounts(void);
```

#### File Operations
```c
// Open a file
int vfs_open(const char* path, int flags, mode_t mode);

// Read from file
ssize_t vfs_read(int fd, void* buf, size_t count);

// Write to file
ssize_t vfs_write(int fd, const void* buf, size_t count);

// Close file
int vfs_close(int fd);
```

#### Directory Operations
```c
// Create directory
int vfs_mkdir(const char* path, mode_t mode);

// Remove directory
int vfs_rmdir(const char* path);

// Read directory
int vfs_readdir(int fd, struct dirent* entry);
```

### Performance Characteristics

#### IR0FS Performance Framework
- **Read Speed**: Framework for up to 500MB/s
- **Write Speed**: Framework for up to 300MB/s
- **Compression Ratio**: Framework for 2:1 to 10:1
- **Journal Recovery**: Framework for < 1 second recovery
- **Defragmentation**: Framework for automatic background process

#### VFS Performance
- **Path Resolution**: O(log n) complexity framework
- **File Operations**: Framework for optimized common patterns
- **Memory Usage**: Minimal overhead per mount
- **Cache Efficiency**: Framework for intelligent caching strategies

### Configuration Options

#### IR0FS Configuration
```c
// Filesystem creation options
struct ir0fs_config 
{
    uint32_t block_size;        // 4096 bytes
    uint32_t journal_size;      // 1MB
    uint32_t compression_level; // 0-9
    bool enable_journaling;     // true
    bool enable_compression;    // true
    bool enable_checksums;      // true;
};
```

#### VFS Configuration
```c
// VFS mount options
struct vfs_mount_options 
{
    bool read_only;             // false
    bool no_exec;               // false
    bool no_access_time;        // false
    uint32_t max_files;         // 1000000
    uint32_t max_path_length;   // 4096
};
```

### Error Handling

#### Error Codes
- `ENOENT`: File or directory not found
- `EACCES`: Permission denied
- `ENOMEM`: Out of memory
- `ENOSPC`: No space left on device
- `EIO`: Input/output error
- `EINVAL`: Invalid argument

#### Recovery Framework
- Framework for automatic journal recovery
- Framework for checksum verification
- Framework for defragmentation on corruption
- Framework for backup superblock usage

### Current Status

#### Working Features
- **VFS Framework**: Basic filesystem abstraction layer
- **IR0FS Structure**: Filesystem layout and metadata definitions
- **Mount Point Management**: Basic filesystem mounting
- **File Operations**: Basic file operation framework
- **Directory Operations**: Basic directory operation framework

#### Development Areas
- **IR0FS Implementation**: Complete low-level filesystem functions
- **Journaling**: Actual journaling implementation
- **Compression**: Real compression algorithms
- **Integrity Checks**: Actual checksum verification
- **Performance Optimization**: Complete performance features

---

## Español

### Descripción General
El Subsistema de Sistema de Archivos proporciona un framework de sistema de archivos para el kernel IR0 con la estructura del sistema de archivos IR0FS y una capa de Sistema de Archivos Virtual (VFS). Incluye funcionalidad básica de sistema de archivos con un framework para características avanzadas como journaling, compresión y verificaciones de integridad.

### Componentes Principales

#### 1. IR0FS - Sistema de Archivos (`ir0fs.c/h`)
- **Propósito**: Estructura de sistema de archivos con framework para características avanzadas
- **Características**:
  - **Framework de Journaling**: Estructura para transacciones atómicas y recuperación de fallos
  - **Framework de Compresión**: Framework para compresión multi-algoritmo (LZ4, ZSTD, LZMA)
  - **Framework de Integridad**: Framework para checksums CRC32 por bloque
  - **Soporte para Archivos Grandes**: Framework para archivos de hasta 1PB
  - **Alta Capacidad**: Framework para 1,000,000 archivos por filesystem
  - **Características Avanzadas**: Framework para defragmentación, monitoreo de salud, optimización

#### 2. Sistema de Archivos Virtual (`vfs.c/h`)
- **Propósito**: Capa de abstracción para múltiples tipos de filesystem
- **Características**:
  - Registro y gestión de filesystems
  - Manejo de puntos de montaje
  - Resolución y recorrido de rutas
  - Gestión de descriptores de archivo
  - Operaciones de directorio
  - Operaciones de archivo (abrir, cerrar, leer, escribir, buscar)

#### 3. Implementación VFS Simple (`vfs_simple.c/h`)
- **Propósito**: VFS simplificado para operaciones básicas
- **Características**:
  - Operaciones básicas de archivo
  - Manejo simple de directorios
  - Sistema de archivos en memoria
  - Soporte para debugging y testing

#### 4. Herramientas del Sistema de Archivos (`tools/`)
- **Propósito**: Utilidades para gestión del filesystem
- **Características**:
  - Framework de creación y formateo de filesystem
  - Framework de verificación de salud y reparación
  - Framework de análisis de rendimiento
  - Herramientas de debugging

### Especificaciones Técnicas de IR0FS

#### Layout del Sistema de Archivos
```
Superblock (4KB)
├── Metadatos del filesystem
├── Framework de información del journal
├── Framework de configuración de compresión
└── Framework de checksums de integridad

Tabla de Inodos
├── Metadatos de archivo (256 bytes por inodo)
├── Framework de atributos extendidos
├── Framework de información de compresión
└── Framework de datos de checksum

Bloques de Datos (4KB cada uno)
├── Datos de archivo
├── Entradas de directorio (64 bytes cada una)
├── Framework de entradas del journal
└── Framework de metadatos de compresión
```

#### Características de Framework

1. **Framework de Sistema de Journaling**
   - Estructura para transacciones atómicas
   - Framework para recuperación automática de fallos
   - Framework de logging con capacidad de rollback
   - Framework de verificación de integridad post-fallo

2. **Framework de Compresión**
   - **LZ4**: Framework para máxima velocidad (2:1 compresión)
   - **ZSTD**: Framework para balance velocidad/compresión (3:1)
   - **LZMA**: Framework para máxima compresión (10:1)
   - Framework para selección automática de algoritmo

3. **Framework de Integridad de Datos**
   - Framework de checksums CRC32 por bloque
   - Framework para verificación automática en lectura
   - Framework de detección de corrupción
   - Framework para recuperación automática

4. **Framework de Escalabilidad**
   - Framework para archivos de hasta 1PB
   - Framework para 1,000,000 archivos por filesystem
   - Framework para defragmentación automática
   - Framework para optimización de acceso

### Arquitectura VFS

#### Gestión de Puntos de Montaje
```c
// Montar un filesystem
int mount(const char* source, const char* target, const char* fstype);

// Desmontar un filesystem
int umount(const char* target);

// Listar filesystems montados
void list_mounts(void);
```

#### Operaciones de Archivo
```c
// Abrir un archivo
int vfs_open(const char* path, int flags, mode_t mode);

// Leer de un archivo
ssize_t vfs_read(int fd, void* buf, size_t count);

// Escribir a un archivo
ssize_t vfs_write(int fd, const void* buf, size_t count);

// Cerrar archivo
int vfs_close(int fd);
```

#### Operaciones de Directorio
```c
// Crear directorio
int vfs_mkdir(const char* path, mode_t mode);

// Eliminar directorio
int vfs_rmdir(const char* path);

// Leer directorio
int vfs_readdir(int fd, struct dirent* entry);
```

### Características de Rendimiento

#### Framework de Rendimiento de IR0FS
- **Velocidad de Lectura**: Framework para hasta 500MB/s
- **Velocidad de Escritura**: Framework para hasta 300MB/s
- **Ratio de Compresión**: Framework para 2:1 a 10:1
- **Recuperación de Journal**: Framework para < 1 segundo de recuperación
- **Defragmentación**: Framework para proceso automático en background

#### Rendimiento de VFS
- **Resolución de Rutas**: Framework de complejidad O(log n)
- **Operaciones de Archivo**: Framework para patrones comunes optimizados
- **Uso de Memoria**: Overhead mínimo por montaje
- **Eficiencia de Cache**: Framework para estrategias de cache inteligentes

### Opciones de Configuración

#### Configuración de IR0FS
```c
// Opciones de creación de filesystem
struct ir0fs_config 
{
    uint32_t block_size;        // 4096 bytes
    uint32_t journal_size;      // 1MB
    uint32_t compression_level; // 0-9
    bool enable_journaling;     // true
    bool enable_compression;    // true
    bool enable_checksums;      // true
};
```

#### Configuración de VFS
```c
// Opciones de montaje VFS
struct vfs_mount_options 
{
    bool read_only;             // false
    bool no_exec;               // false
    bool no_access_time;        // false
    uint32_t max_files;         // 1000000
    uint32_t max_path_length;   // 4096
};
```

### Manejo de Errores

#### Códigos de Error
- `ENOENT`: Archivo o directorio no encontrado
- `EACCES`: Permiso denegado
- `ENOMEM`: Sin memoria disponible
- `ENOSPC`: Sin espacio en dispositivo
- `EIO`: Error de entrada/salida
- `EINVAL`: Argumento inválido

#### Framework de Recuperación
- Framework para recuperación automática de journal
- Framework para verificación de checksum
- Framework para defragmentación en corrupción
- Framework para uso de superblock de respaldo

### Estado Actual

#### Características Funcionando
- **Framework VFS**: Capa básica de abstracción de sistema de archivos
- **Estructura IR0FS**: Layout del sistema de archivos y definiciones de metadatos
- **Gestión de Puntos de Montaje**: Montaje básico de sistemas de archivos
- **Operaciones de Archivo**: Framework básico de operaciones de archivo
- **Operaciones de Directorio**: Framework básico de operaciones de directorio

#### Áreas de Desarrollo
- **Implementación IR0FS**: Funciones completas de bajo nivel del sistema de archivos
- **Journaling**: Implementación real de journaling
- **Compresión**: Algoritmos reales de compresión
- **Verificaciones de Integridad**: Verificación real de checksums
- **Optimización de Rendimiento**: Características completas de rendimiento
