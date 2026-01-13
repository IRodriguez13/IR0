# Deprecation Plan: Syscalls Ambiguas → Filesystem Polimórfico

## Resumen

El kernel IR0 puede deprecar las syscalls ambiguas usando el enfoque polimórfico de archivos. En lugar de tener syscalls específicas para cada funcionalidad (PS, DF, LSBLK, etc.), se pueden usar las syscalls universales de archivos (`open`, `read`, `write`, `close`) accediendo a archivos virtuales en `/proc` y `/dev`.

## Ventajas del Enfoque Polimórfico

1. **Simplicidad**: Reduce el número de syscalls en el dispatcher
2. **Consistencia**: Sigue el principio Unix "everything is a file"
3. **Flexibilidad**: Los programas pueden acceder directamente sin wrappers
4. **Extensibilidad**: Agregar nuevas funcionalidades solo requiere nuevos archivos virtuales
5. **Compatibilidad POSIX**: Más cercano al comportamiento de Linux

## Mapeo de Syscalls Ambiguas → Archivos Virtuales

### Sistema de Archivos `/proc`

| Syscall Actual | Archivo Virtual | Estado |
|----------------|-----------------|--------|
| `SYS_PS` (7) | `/proc/ps` | ✅ Ya existe |
| `SYS_DF` (95) | `/proc/mounts` o `/dev/disk` | ⚠️ Parcialmente implementado |
| `SYS_LSBLK` (92) | `/proc/blockdevices` o `/dev/disk` | ❌ Por implementar |
| `SYS_WHOAMI` (94) | `/proc/self/status` (campo Uid) | ✅ Ya existe |

### Sistema de Archivos `/dev`

| Syscall Actual | Archivo Virtual | Estado |
|----------------|-----------------|--------|
| `SYS_AUDIO_TEST` (112) | `/dev/audio` | ✅ Ya existe |
| `SYS_MOUSE_TEST` (113) | `/dev/mouse` | ✅ Ya existe |
| `SYS_PING` (115) | `/dev/net` (write) | ✅ Ya existe |
| `SYS_IFCONFIG` (116) | `/dev/net` (write) | ✅ Ya existe |

## Implementación Actual

### Ya Implementado con Wrappers

El archivo `includes/ir0/syscall.h` ya tiene wrappers que usan archivos virtuales:

```c
// Ya usando /proc
static inline int64_t ir0_ps(void) {
    return ir0_open("/proc", O_RDONLY, 0);
}

static inline int64_t ir0_netinfo(void) {
    return ir0_open("/proc/netinfo", O_RDONLY, 0);
}

static inline int64_t ir0_lsdrv(void) {
    return ir0_open("/proc/drivers", O_RDONLY, 0);
}

// Ya usando /dev
static inline int64_t ir0_audio_test(const void *data, size_t size) {
    int fd = ir0_open("/dev/audio", O_WRONLY, 0);
    if (fd < 0) return fd;
    int64_t result = ir0_write(fd, data, size);
    ir0_close(fd);
    return result;
}

static inline int64_t ir0_mouse_test(void *buf, size_t size) {
    int fd = ir0_open("/dev/mouse", O_RDONLY, 0);
    if (fd < 0) return fd;
    int64_t result = ir0_read(fd, buf, size);
    ir0_close(fd);
    return result;
}

static inline int64_t ir0_df(void) {
    return ir0_open("/dev/disk", O_RDONLY, 0);
}
```

### Syscalls que Todavía Existen en el Dispatcher

Estas syscalls todavía están en `syscall_dispatch()` pero pueden ser deprecadas:

- `SYS_PS` (7) → Ya usa `/proc/status` internamente, puede deprecarse
- `SYS_DF` (95) → Implementación directa, puede moverse a `/dev/disk` o `/proc/blockdevices`
- `SYS_LSBLK` (92) → Implementación directa, puede moverse a `/proc/blockdevices`
- `SYS_WHOAMI` (94) → Puede leerse de `/proc/self/status`
- `SYS_AUDIO_TEST` (112) → Ya tiene wrapper en `/dev/audio`
- `SYS_MOUSE_TEST` (113) → Ya tiene wrapper en `/dev/mouse`
- `SYS_PING` (115) → Ya tiene wrapper en `/dev/net`
- `SYS_IFCONFIG` (116) → Ya tiene wrapper en `/dev/net`

## Plan de Deprecación

### Fase 1: Mantener Compatibilidad (Transición)

1. **Mantener las syscalls actuales** pero hacer que deleguen a archivos virtuales
2. **Marcar como deprecated** en documentación y código
3. **Actualizar wrappers** para usar directamente archivos virtuales

Ejemplo de migración de `sys_ps()`:

```c
// ANTES (syscalls.c)
int64_t sys_ps(void) {
    int fd = sys_open("/proc/status", O_RDONLY, 0);
    // ... código de impresión directa
    return 0;
}

// DESPUÉS (mantener por compatibilidad, pero deprecated)
int64_t sys_ps(void) {
    // DEPRECATED: Usar 'cat /proc/ps' en su lugar
    // Mantener por compatibilidad con código legacy
    int fd = sys_open("/proc/ps", O_RDONLY, 0);
    if (fd < 0) return -1;
    
    char buffer[4096];
    int64_t bytes = sys_read(fd, buffer, sizeof(buffer) - 1);
    sys_close(fd);
    
    if (bytes > 0) {
        buffer[bytes] = '\0';
        sys_write(STDOUT_FILENO, buffer, bytes);
    }
    return bytes > 0 ? 0 : -1;
}
```

### Fase 2: Crear Archivos Virtuales Faltantes

#### `/proc/blockdevices` o `/proc/partitions`

Para reemplazar `sys_lsblk()`:

```c
// En fs/procfs.c
int proc_blockdevices_read(char *buf, size_t count) {
    // Formato: NAME MAJ:MIN SIZE MODEL
    // Similar a la salida actual de sys_lsblk()
    // Iterar sobre ata_get_device_info() para cada drive
}
```

#### Mejorar `/dev/disk` para `sys_df()`

Actualmente `/dev/disk` existe pero puede necesitar mejoras para proporcionar información de filesystems montados.

### Fase 3: Remover del Dispatcher (Futuro)

Una vez que todo el código use archivos virtuales:

1. **Remover casos del switch** en `syscall_dispatch()`
2. **Remover del enum** `syscall_num_t`
3. **Actualizar documentación**

## Ejemplo de Uso Post-Deprecación

### Antes (Syscall Específica)

```c
// Usuario debe conocer syscall específica
syscall(SYS_PS, 0, 0, 0);
```

### Después (Filesystem Polimórfico)

```c
// Usuario usa syscalls universales
int fd = open("/proc/ps", O_RDONLY, 0);
char buf[4096];
read(fd, buf, sizeof(buf));
close(fd);
// Procesar buf como cualquier archivo de texto
```

O usando el wrapper:

```c
int fd = ir0_open("/proc/ps", O_RDONLY, 0);
// ... leer y procesar
ir0_close(fd);
```

## Shell Commands - Ya Usan Archivos Virtuales

El shell ya está migrado en muchos casos:

```c
// kernel/dbgshell.c
static void cmd_lsdrv(const char *args) {
    cmd_cat("/proc/drivers");  // ✅ Ya usa /proc
}

static void cmd_dmesg(const char *args) {
    cmd_cat("/dev/kmsg");  // ✅ Ya usa /dev
}

static void cmd_ps(const char *args) {
    cmd_cat("/proc/ps");  // ✅ Ya usa /proc
}
```

Pero algunos todavía usan syscalls directas:

```c
static void cmd_df(const char *args) {
    syscall(95, 0, 0, 0);  // ❌ Todavía usa syscall directa
}

static void cmd_audio_test(const char *args) {
    syscall(112, 0, 0, 0);  // ❌ Todavía usa syscall directa
}
```

## Recomendaciones

1. **Prioridad Alta**: Migrar `cmd_df()` y `cmd_lsblk()` en el shell para usar archivos virtuales
2. **Prioridad Media**: Agregar `/proc/blockdevices` si no existe
3. **Prioridad Baja**: Mantener syscalls legacy marcadas como deprecated por compatibilidad

## Conclusión

**SÍ, definitivamente pueden seguir usando el enfoque polimórfico**. Es la dirección correcta y ya tienen buena parte implementado. El sistema de archivos virtuales proporciona una interfaz más limpia y extensible que las syscalls específicas.

