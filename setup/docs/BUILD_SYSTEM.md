# IR0 Kernel - Sistema de Compilación Multi-Arquitectura

## Descripción General

El IR0 Kernel incluye un sistema de compilación condicional avanzado que permite compilar el kernel para múltiples arquitecturas y diferentes tipos de sistemas (build targets). El sistema está diseñado para ser flexible, eficiente y fácil de usar.

## Arquitecturas Soportadas

- **x86-32**: Arquitectura de 32 bits (i386)
- **x86-64**: Arquitectura de 64 bits (x86_64)

## Build Targets Soportados

- **desktop**: Sistema de escritorio con soporte completo de archivos
- **server**: Sistema servidor optimizado para rendimiento
- **iot**: Sistema IoT con funcionalidades mínimas
- **embedded**: Sistema embebido sin sistema de archivos

## Estructura del Sistema de Compilación

```
ir0-kernel/
├── Makefile                 # Makefile principal con compilación condicional
├── scripts/
│   ├── build_all.sh        # Script para compilar todas las combinaciones
│   ├── create_iso.sh       # Script para crear ISOs personalizadas
│   └── menu_builder.sh     # Menú interactivo de compilación
├── arch/
│   ├── x86-32/            # Código específico para x86-32
│   └── x86-64/            # Código específico para x86-64
└── [otros directorios...]
```

## Uso del Sistema

### 1. Compilación Básica

```bash
# Compilar para arquitectura por defecto (x86-32)
make

# Compilar para arquitectura específica
make ARCH=x86-64
make ARCH=x86-32

# Compilar para build target específico
make BUILD_TARGET=desktop
make BUILD_TARGET=server
make BUILD_TARGET=iot
make BUILD_TARGET=embedded

# Combinar arquitectura y build target
make ARCH=x86-64 BUILD_TARGET=desktop
```

### 2. Compilación Avanzada

```bash
# Compilar todas las arquitecturas
make all-arch

# Compilar todos los build targets
make all-targets

# Compilar todas las combinaciones
make all-combinations
```

### 3. Scripts de Automatización

#### Script de Compilación Completa (`scripts/build_all.sh`)

```bash
# Compilar todas las combinaciones (recomendado)
./scripts/build_all.sh -c

# Compilar solo todas las arquitecturas
./scripts/build_all.sh -a

# Compilar solo todos los build targets
./scripts/build_all.sh -t

# Ejecutar en QEMU después de compilar
./scripts/build_all.sh -c -r

# Modo debug (más información)
./scripts/build_all.sh -c -d
```

#### Script de ISO Personalizada (`scripts/create_iso.sh`)

```bash
# Crear ISO básica
./scripts/create_iso.sh -a x86-64 -t desktop

# Crear ISO con nombre personalizado
./scripts/create_iso.sh -a x86-32 -t server -o mi-kernel.iso

# Crear ISO con argumentos de boot
./scripts/create_iso.sh -a x86-64 -t desktop -b "console=ttyS0 debug"

# Crear ISO con archivo de configuración personalizada
./scripts/create_iso.sh -a x86-32 -t embedded -c config.txt
```

#### Menú Interactivo (`scripts/menu_builder.sh`)

```bash
# Iniciar menú interactivo
./scripts/menu_builder.sh
```

El menú interactivo ofrece:
- Compilación de kernel específico
- Compilación de todas las arquitecturas
- Compilación de todos los build targets
- Compilación de todas las combinaciones
- Creación de ISOs personalizadas
- Ejecución en QEMU
- Limpieza de archivos
- Información del sistema

## Configuración Condicional

### Flags de Compilación por Build Target

El sistema automáticamente incluye flags específicos según el build target:

- **desktop**: `-DIR0_DESKTOP`
- **server**: `-DIR0_SERVER`
- **iot**: `-DIR0_IOT`
- **embedded**: `-DIR0_EMBEDDED`

### Subsistemas Condicionales

Los subsistemas se incluyen condicionalmente según el build target:

- **desktop/server/iot**: Incluye sistema de archivos (fs)
- **embedded**: Sin sistema de archivos

### Objetos Condicionales

Los objetos se compilan condicionalmente:

```makefile
# Objetos condicionales según build target
ifeq ($(BUILD_TARGET),desktop)
    CONDITIONAL_OBJS = fs/vfs_simple.o
else ifeq ($(BUILD_TARGET),server)
    CONDITIONAL_OBJS = fs/vfs_simple.o
else ifeq ($(BUILD_TARGET),iot)
    CONDITIONAL_OBJS = fs/vfs_simple.o
else ifeq ($(BUILD_TARGET),embedded)
    CONDITIONAL_OBJS = 
endif
```

## Archivos Generados

### Nomenclatura de Archivos

Los archivos generados siguen el patrón:
- `kernel-{ARCH}-{TARGET}.bin`: Kernel compilado
- `kernel-{ARCH}-{TARGET}.iso`: ISO booteable

Ejemplos:
- `kernel-x86-64-desktop.bin`
- `kernel-x86-32-server.iso`
- `kernel-x86-64-iot.bin`

### Ubicación de Archivos

- **Kernels**: Directorio raíz del proyecto
- **ISOs**: Directorio raíz del proyecto
- **Objetos temporales**: Directorios de cada subsistema

## Dependencias

### Requeridas para Compilación

```bash
sudo apt-get install build-essential nasm
```

### Requeridas para ISOs

```bash
sudo apt-get install grub-pc-bin xorriso
```

### Requeridas para QEMU

```bash
sudo apt-get install qemu-system-x86
```

### Instalación Completa

```bash
sudo apt-get install build-essential nasm grub-pc-bin xorriso qemu-system-x86
```

## Comandos Útiles

### Limpieza

```bash
# Limpiar arquitectura actual
make clean

# Limpiar todas las arquitecturas
make clean-all
```

### Información

```bash
# Mostrar información de arquitectura
make arch-info

# Mostrar información de build
make build-info

# Mostrar ayuda
make help
```

### Ejecución

```bash
# Ejecutar en QEMU
make run

# Ejecutar en QEMU con debug
make debug
```

## Configuración Avanzada

### Variables de Entorno

```bash
# Establecer arquitectura por defecto
export ARCH=x86-64

# Establecer build target por defecto
export BUILD_TARGET=desktop
```

### Personalización de Flags

Los flags de compilación se pueden personalizar modificando el Makefile principal:

```makefile
# Flags específicos por arquitectura
ifeq ($(ARCH),x86-64)
    CFLAGS += -m64 -mcmodel=large
else ifeq ($(ARCH),x86-32)
    CFLAGS += -m32 -march=i686
endif
```

### Agregar Nuevas Arquitecturas

Para agregar una nueva arquitectura:

1. Crear directorio `arch/{nueva-arch}/`
2. Agregar configuración en el Makefile principal
3. Crear Makefile específico para la arquitectura
4. Agregar objetos de arquitectura en la sección correspondiente

### Agregar Nuevos Build Targets

Para agregar un nuevo build target:

1. Agregar el target a `VALID_TARGETS`
2. Agregar configuración de flags
3. Agregar subsistemas condicionales
4. Agregar objetos condicionales

## Solución de Problemas

### Errores Comunes

1. **Dependencias faltantes**: Ejecutar `./scripts/build_all.sh` para verificar dependencias
2. **Permisos de scripts**: Ejecutar `chmod +x scripts/*.sh`
3. **Directorio incorrecto**: Asegurarse de estar en el directorio raíz del kernel

### Debug

```bash
# Compilar con información detallada
make ARCH=x86-64 BUILD_TARGET=desktop V=1

# Usar modo debug en scripts
./scripts/build_all.sh -d
./scripts/create_iso.sh -d
```

### Logs

- Los logs de QEMU se guardan en `qemu_debug.log`
- Los archivos de dependencias se generan con extensión `.d`

## Contribución

Para contribuir al sistema de compilación:

1. Mantener compatibilidad con todas las arquitecturas
2. Documentar nuevos build targets
3. Actualizar scripts de automatización
4. Probar todas las combinaciones antes de commit

## Licencia

Este sistema de compilación está bajo la misma licencia que el IR0 Kernel.
