# Virtual Filesystems en IR0

> **Última verificación:** 2026-07-29
> **Fuente de verdad:** `fs/procfs.c`, `fs/sysfs.c`, `fs/devfs.c`, `fs/heartfs.h`,
> [`VIRTUAL_FILESYSTEMS.md`](../VIRTUAL_FILESYSTEMS.md) (canónico en inglés)

Este documento se enfoca en pseudo-filesystems expuestos por VFS.

## `/proc`

`procfs` expone estado runtime de kernel y procesos.

### Endpoints comunes

- `/proc/meminfo`
- `/proc/uptime`
- `/proc/stat` — líneas `cpu`/`cpu0` (jiffies) para BusyBox `top`
- `/proc/loadavg`
- `/proc/version`
- `/proc/filesystems`
- `/proc/mounts`
- `/proc/drivers`
- `/proc/interrupts`
- `/proc/blockdevices`
- `/proc/partitions`
- `/proc/[pid]/status`
- `/proc/[pid]/cmdline`
- `/proc/[pid]/stat`

### `/proc/stat` y BusyBox `top`

- Registro: `proc_stat_read()` en `fs/procfs.c`.
- Formato Linux: `cpu` + `cpu0` con ≥4 campos tras la etiqueta (user nice system idle …).
- Smoke: `python3 scripts/smoke_proc_stat_top.py`.

### Readdir de `/proc`

Los dirents numéricos (PID) van **antes** que el registry estático porque
`GETDENTS_BATCH_MAX` es 24; si solo caben nombres estáticos, `top`/`ps` ven
`no process info in /proc`.

### Notas

- Los datos se generan al momento de leer.
- Se endureció el formateo numérico para valores de 64 bits.
- El tracking de contexto por proceso evita colisiones entre procesos.

## `/dev`

`devfs` expone puntos de entrada de dispositivos del kernel.

### Nodos comunes

- `/dev/null`, `/dev/zero`
- `/dev/console`, `/dev/tty`
- `/dev/kmsg`
- `/dev/disk`
- `/dev/net`
- `/dev/audio`
- `/dev/mouse`

### Notas

- El acceso usa I/O estándar por syscalls desde binarios user-style.
- El registro de dispositivos pasa por infraestructura de drivers/bootstrap.

## `/sys`

`sysfs` expone datos de kernel/sistema en un namespace estructurado.

### Notas

- Los paths de error usan retornos errno negativos consistentes.
- Consola y backends se exponen vía facades.

## Backends pseudo en memoria

- `tmpfs`: árbol escribible en RAM con uid/gid y create consciente de umask.
- `procfs`, `devfs`, `sysfs`: filesystems pseudo dinámicos.

## Puntos fuertes

- Observabilidad runtime sin tooling externo.
- Modelo de acceso abierto/lectura/escritura/stat coherente.
- `top -bn1` usable con `/proc/stat` + PIDs en readdir.

## Puntos débiles

- Algunos endpoints siguen mínimos a propósito.
- El batch de `getdents` sigue truncando listados grandes de `/proc`.
- El desglose CPU de `/proc/stat` es heurístico hasta haber cputime por tarea.
