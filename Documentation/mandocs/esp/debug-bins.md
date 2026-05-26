# Debug Shell (debug_bins) de IR0

| Campo | Valor |
|-------|-------|
| Versión | 0.1 |
| Fase IR0 | T0 |
| Estado | stable |
| Depende de | syscalls, vfs, boot |
| Página man | IR0-debug-bins (sección 7) |
| Fuentes principales | `debug_bins/dbgshell.c`, `debug_bins/debug_bins_registry.c`, `debug_bins/debug_bins.h`, `kernel/init.c`, `kernel/main.c` |

## 1. Visión general

Con `KERNEL_DEBUG_SHELL=1`, el PID 1 ejecuta **dbgshell** en el kernel en lugar
de `/sbin/init`. Los comandos en `debug_bins/cmd_*.c` implementan un conjunto
mínimo estilo userspace (ls, cat, ping, mount, …) usando **solo syscalls** — sin
llamadas directas a API del kernel desde los handlers de comando.

## 2. Arquitectura interna

| Pieza | Rol |
|-------|-----|
| `dbgshell.c` | REPL: read stdin, parse línea, dispatch |
| `debug_bins_registry.c` | tabla `debug_commands[]`, `debug_find_command` |
| `cmd_*.c` | Cada uno exporta `struct debug_command cmd_*` |
| `debug_bins.h` | Contrato: helpers I/O syscall |
| `kernel/init.c` | `init_1` → `shell_entry()` como PID1 |
| `kernel/main.c` | `#if KERNEL_DEBUG_SHELL` → `start_init_process()` |

Grupos de comandos condicionados por Kconfig: `CONFIG_DEBUG_BINS_GROUP_{CORE,FS,TEXT,IDENTITY,DIAG,NET,BT}`.

## 3. Flujo de datos

```text
  kmain → start_init_process()
       → spawn tarea KERNEL_MODE, entry init_1, comm "debshell"
       → bucle shell_entry():
            read(0) → parse → debug_find_command → handler(argc, argv)
            cierra fds 3..63 antes de cada comando
```

Registro:

```text
  cmd_ls.c: struct debug_command cmd_ls = { .name, .handler, ... }
       → listado en debug_commands[] en debug_bins_registry.c
       → enlazado si grupo habilitado en Makefile
```

Mapa ASCII:

```text
  [kmain] ──► PID1 dbgshell ──► syscall ──► VFS/devfs/net
                  │
                  └── handlers cmd_* (ring 0, estilo solo-syscall)
```

## 4. Responsabilidades

- Handlers: solo open/read/write/close/ioctl; sin `#include` de headers kernel/fs.
- Shell: built-ins `help`, `clear`, `exit` (no en registry).
- `cmd_ktest` solo con `IR0_KERNEL_TESTS` (objeto registry separado).

## 5. Límites del subsistema

- Intención de diseño: comportarse como userspace ring-3 (AGENTS.md).
- **Excepción:** el proceso es `KERNEL_MODE` así que `copy_user` puede omitir comprobaciones estrictas para stack del shell.
- No llamar `bt_sysfs_*`, VFS directo ni internals de driver desde módulos cmd.

## 6. Relación con otros subsistemas

| Vecino | Interacción |
|--------|-------------|
| Boot | Alternativa a `kexecve("/sbin/init")` |
| Syscalls | Todos los paths I/O |
| VFS/net/proc | Vía rutas como `/`, `/proc/*`, `/dev/net` |
| Userspace | Exclusivo mutuo por defecto con init musl (`IR0_USERSPACE_INIT_BOOT=1` fuerza shell off) |

## 7. Mapas visuales

```text
  KERNEL_DEBUG_SHELL=1          KERNEL_DEBUG_SHELL=0
         │                              │
         ▼                              ▼
    dbgshell PID1                  /sbin/init (irinit)
         │                              │
    cmd_cat/cmd_ping               BusyBox/musl
```

## 8. Invariantes importantes

1. Línea de entrada máx **255** chars; historial **32** líneas.
2. Máx **64** args por comando (`debug_parse_args`).
3. Barrido tabla FD cierra 3..63 antes de cada comando externo.
4. `IR0_USERSPACE_INIT_BOOT=1` anula Kconfig para deshabilitar debug shell.
5. Objetos enlazados seleccionados en Makefile por grupo debug.

## 9. Consejos de depuración

- `setup/defconfig` predeterminado tiene `KERNEL_DEBUG_SHELL=y` — ISO normal arranca shell salvo ISO userspace.
- `make kernel-x64-userspace.iso` fija path init userspace.
- Checklist de dispositivos en dbgshell sondea `/dev/fb0`, `/dev/events0`.
- Comandos net requieren `CONFIG_ENABLE_NETWORKING`.

## 10. Roadmap futuro

- Reducir solapamiento con BusyBox cuando init T1 estable.
- Harness ring-3 más estricto para módulos cmd (hoy KERNEL_MODE).
- Auto-generación registry split — lista extern manual hoy.

Ver: `IR0-boot`, `IR0-userspace`, `IR0-net`.
