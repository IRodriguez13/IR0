# IR0 Kernel - Sistema de Estrategias de Compilación

## 📋 Descripción General

El sistema de estrategias de compilación del kernel IR0 permite compilar el kernel con diferentes configuraciones según el caso de uso específico. Cada estrategia habilita o deshabilita subsistemas, ajusta parámetros de memoria y optimiza el kernel para un propósito particular.

## 🎯 Estrategias Disponibles

### 1. **Desktop** - Sistema de Escritorio Completo
**Descripción**: Kernel optimizado para sistemas de escritorio con interfaz gráfica, audio y multimedia.

**Características habilitadas**:
- ✅ GUI (Interfaz gráfica)
- ✅ Audio (Sistema de sonido)
- ✅ USB (Dispositivos USB)
- ✅ Networking (Redes)
- ✅ Filesystem (Sistema de archivos)
- ✅ Multimedia (Multimedia)
- ✅ Printing (Impresión)
- ✅ VFS (Sistema de archivos virtual)
- ✅ TCP/IP (Protocolos de red)
- ✅ Sockets (Comunicación)
- ✅ Ethernet (Red Ethernet)
- ✅ User Mode (Modo usuario)
- ✅ Memory Protection (Protección de memoria)
- ✅ Process Isolation (Aislamiento de procesos)

**Configuración de memoria**:
- 📊 Heap Size: 256MB
- 📊 Max Processes: 1024
- 📊 Max Threads: 4096
- 📊 Scheduler Quantum: 10ms
- 📊 IO Buffer Size: 64KB

### 2. **Server** - Servidor de Alto Rendimiento
**Descripción**: Kernel optimizado para servidores con networking, SSL y virtualización.

**Características habilitadas**:
- ❌ GUI (Sin interfaz gráfica)
- ❌ Audio (Sin sistema de sonido)
- ✅ USB (Dispositivos USB)
- ✅ Networking (Redes)
- ✅ Filesystem (Sistema de archivos)
- ❌ Multimedia (Sin multimedia)
- ❌ Printing (Sin impresión)
- ✅ VFS (Sistema de archivos virtual)
- ✅ TCP/IP (Protocolos de red)
- ✅ Sockets (Comunicación)
- ✅ Ethernet (Red Ethernet)
- ✅ User Mode (Modo usuario)
- ✅ Memory Protection (Protección de memoria)
- ✅ Process Isolation (Aislamiento de procesos)
- ✅ Network Security (Seguridad de red)

**Configuración de memoria**:
- 📊 Heap Size: 1GB
- 📊 Max Processes: 4096
- 📊 Max Threads: 16384
- 📊 Scheduler Quantum: 5ms
- 📊 IO Buffer Size: 256KB

### 3. **IoT** - Sistema IoT Ligero
**Descripción**: Kernel optimizado para dispositivos IoT con gestión de energía y timers de baja potencia.

**Características habilitadas**:
- ❌ GUI (Sin interfaz gráfica)
- ❌ Audio (Sin sistema de sonido)
- ❌ USB (Sin dispositivos USB)
- ✅ Networking (Redes)
- ✅ Filesystem (Sistema de archivos)
- ❌ Multimedia (Sin multimedia)
- ❌ Printing (Sin impresión)
- ✅ VFS (Sistema de archivos virtual)
- ✅ TCP/IP (Protocolos de red)
- ✅ Sockets (Comunicación)
- ✅ Ethernet (Red Ethernet)
- ❌ User Mode (Sin modo usuario)
- ✅ Memory Protection (Protección de memoria)
- ❌ Process Isolation (Sin aislamiento de procesos)
- ✅ Power Management (Gestión de energía)
- ✅ Sleep Modes (Modos de sueño)
- ✅ Low Power Timers (Timers de baja potencia)

**Configuración de memoria**:
- 📊 Heap Size: 16MB
- 📊 Max Processes: 64
- 📊 Max Threads: 256
- 📊 Scheduler Quantum: 20ms
- 📊 IO Buffer Size: 4KB

### 4. **Embedded** - Sistema Embebido Mínimo
**Descripción**: Kernel mínimo para sistemas embebidos sin GUI ni networking.

**Características habilitadas**:
- ❌ GUI (Sin interfaz gráfica)
- ❌ Audio (Sin sistema de sonido)
- ❌ USB (Sin dispositivos USB)
- ❌ Networking (Sin redes)
- ❌ Filesystem (Sin sistema de archivos)
- ❌ Multimedia (Sin multimedia)
- ❌ Printing (Sin impresión)
- ❌ VFS (Sin sistema de archivos virtual)
- ❌ TCP/IP (Sin protocolos de red)
- ❌ Sockets (Sin comunicación)
- ❌ Ethernet (Sin red Ethernet)
- ❌ User Mode (Sin modo usuario)
- ❌ Memory Protection (Sin protección de memoria)
- ❌ Process Isolation (Sin aislamiento de procesos)
- ✅ Power Management (Gestión de energía)
- ✅ Sleep Modes (Modos de sueño)
- ✅ Low Power Timers (Timers de baja potencia)

**Configuración de memoria**:
- 📊 Heap Size: 4MB
- 📊 Max Processes: 16
- 📊 Max Threads: 64
- 📊 Scheduler Quantum: 50ms
- 📊 IO Buffer Size: 1KB

## 🛠️ Uso del Sistema de Estrategias

### Scripts Disponibles

#### 1. **config_manager.sh** - Gestor Principal de Configuración
Script principal para gestionar la configuración del kernel y estrategias.

```bash
# Mostrar ayuda
./scripts/config_manager.sh help

# Validar configuración
./scripts/config_manager.sh validate

# Mostrar información de estrategia
./scripts/config_manager.sh info --strategy desktop
./scripts/config_manager.sh info --strategy server
./scripts/config_manager.sh info --strategy iot
./scripts/config_manager.sh info --strategy embedded

# Mostrar información de arquitectura
./scripts/config_manager.sh info --arch x86-64
./scripts/config_manager.sh info --arch x86-32

# Mostrar toda la información
./scripts/config_manager.sh info --all

# Gestionar configuración
./scripts/config_manager.sh config --show-current
./scripts/config_manager.sh config --set-arch x86-64
./scripts/config_manager.sh config --set-strategy desktop
./scripts/config_manager.sh config --reset

# Compilar con estrategia
./scripts/config_manager.sh build -a x86-64 -s desktop
./scripts/config_manager.sh build -a x86-32 -s server -c -r
```

#### 2. **strategy_builder.sh** - Constructor de Estrategias
Script específico para compilar con estrategias.

```bash
# Mostrar ayuda
./scripts/strategy_builder.sh -h

# Mostrar información de estrategia
./scripts/strategy_builder.sh -s desktop -i
./scripts/strategy_builder.sh -s server -i

# Compilar kernel
./scripts/strategy_builder.sh -a x86-64 -s desktop
./scripts/strategy_builder.sh -a x86-32 -s server -c
./scripts/strategy_builder.sh -a x86-64 -s iot -d -r -l
```

### Opciones de Compilación

#### Opciones de Arquitectura (-a, --arch)
- `x86-32`: Procesador Intel/AMD de 32 bits
- `x86-64`: Procesador Intel/AMD de 64 bits
- `arm32`: Procesador ARM de 32 bits
- `arm64`: Procesador ARM de 64 bits

#### Opciones de Estrategia (-s, --strategy)
- `desktop`: Sistema de escritorio completo
- `server`: Servidor de alto rendimiento
- `iot`: Sistema IoT ligero
- `embedded`: Sistema embebido mínimo

#### Opciones Adicionales
- `-c, --clean`: Limpiar antes de compilar
- `-d, --debug`: Compilar con debug
- `-r, --run`: Ejecutar QEMU automáticamente
- `-l, --logs`: Mostrar logs QEMU
- `-i, --info`: Mostrar información de estrategia

## 📁 Estructura de Archivos

### Archivos de Configuración
```
setup/
├── kernel_config.h      # Configuración principal del kernel
├── kernelconfig.h       # Sistema de estrategias de compilación
└── kernel_config.c      # Implementación de configuración
```

### Scripts de Gestión
```
scripts/
├── config_manager.sh    # Gestor principal de configuración
├── strategy_builder.sh  # Constructor de estrategias
├── build_simple.sh      # Script de compilación simple
└── menu_builder.sh      # Menú interactivo
```

## 🔧 Configuración del Sistema

### Archivo de Configuración (.kernel_config)
El sistema puede usar un archivo de configuración para establecer valores por defecto:

```bash
# Crear configuración personalizada
echo "ARCH=x86-64" > .kernel_config
echo "STRATEGY=desktop" >> .kernel_config

# Ver configuración actual
./scripts/config_manager.sh config --show-current

# Restablecer configuración
./scripts/config_manager.sh config --reset
```

### Variables de Entorno
También se pueden usar variables de entorno:

```bash
export ARCH=x86-64
export BUILD_TARGET=desktop
make kernel-x86-64-desktop.iso
```

## 🎯 Casos de Uso

### 1. Desarrollo de Escritorio
```bash
# Compilar kernel para desarrollo de escritorio
./scripts/config_manager.sh build -a x86-64 -s desktop -c -r
```

### 2. Servidor de Producción
```bash
# Compilar kernel para servidor
./scripts/config_manager.sh build -a x86-64 -s server -c
```

### 3. Dispositivo IoT
```bash
# Compilar kernel para dispositivo IoT
./scripts/config_manager.sh build -a x86-32 -s iot -c -r
```

### 4. Sistema Embebido
```bash
# Compilar kernel mínimo para sistema embebido
./scripts/config_manager.sh build -a x86-32 -s embedded -c
```

## 🔍 Validación y Debugging

### Validar Configuración
```bash
# Validar configuración completa
./scripts/config_manager.sh validate
```

### Verificar Estrategia
```bash
# Verificar configuración de estrategia específica
./scripts/config_manager.sh info --strategy desktop
```

### Debug de Compilación
```bash
# Compilar con debug y mostrar logs
./scripts/config_manager.sh build -a x86-64 -s desktop -d -r -l
```

## 📊 Comparación de Estrategias

| Característica | Desktop | Server | IoT | Embedded |
|----------------|---------|--------|-----|----------|
| GUI | ✅ | ❌ | ❌ | ❌ |
| Audio | ✅ | ❌ | ❌ | ❌ |
| USB | ✅ | ✅ | ❌ | ❌ |
| Networking | ✅ | ✅ | ✅ | ❌ |
| Filesystem | ✅ | ✅ | ✅ | ❌ |
| User Mode | ✅ | ✅ | ❌ | ❌ |
| Power Management | ❌ | ❌ | ✅ | ✅ |
| Heap Size | 256MB | 1GB | 16MB | 4MB |
| Max Processes | 1024 | 4096 | 64 | 16 |
| Scheduler Quantum | 10ms | 5ms | 20ms | 50ms |

## 🚀 Próximas Mejoras

### Estrategias Planificadas
- **Gaming**: Optimizado para juegos con baja latencia
- **Security**: Enfocado en seguridad y aislamiento
- **Real-time**: Para sistemas en tiempo real
- **Cloud**: Optimizado para entornos cloud

### Características Futuras
- Configuración dinámica en tiempo de ejecución
- Perfiles de configuración personalizados
- Integración con herramientas de análisis de rendimiento
- Soporte para más arquitecturas (RISC-V, MIPS)

## 📚 Referencias

- [Documentación del Kernel](README.MD)
- [Análisis Completo](ANALISIS_COMPLETO_KERNEL.txt)
- [Guía de Desarrollo](DEVELOPER_GUIDE.md)
- [Sistema de Build](BUILD_SYSTEM.md)

---

**IR0 Kernel** - *Sistema de Estrategias de Compilación Avanzado*
