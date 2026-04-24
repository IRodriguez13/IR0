# Virtual Filesystems en IR0

Este documento se enfoca en pseudo-filesystems expuestos por VFS.

## `/proc`

`procfs` expone estado runtime de kernel y procesos.

### Endpoints comunes

- `/proc/meminfo`
- `/proc/uptime`
- `/proc/version`
- `/proc/filesystems`
- `/proc/mounts`
- `/proc/drivers`
- `/proc/interrupts`
- `/proc/blockdevices`
- `/proc/partitions`
- `/proc/[pid]/status`
- `/proc/[pid]/cmdline`

### Notas

- Los datos se generan al momento de leer.
- Se endurecio el formateo numerico para valores de 64 bits.
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

- El acceso usa I/O por syscall desde binarios estilo userspace.
- El registro de dispositivos pasa por bootstrap/registry de drivers.

## `/sys`

`sysfs` expone informacion estructurada de kernel/sistema.

### Notas

- Los paths de error retornan `errno` negativo de forma consistente.
- La exposicion de consola/backend pasa por interfaces facade.

## Backends Pseudo en Memoria

- `tmpfs`: arbol writable en RAM con uid/gid y umask en create.
- `procfs`, `devfs`, `sysfs`: pseudo-filesystems dinamicos.

## Puntos Fuertes

- Alta observabilidad runtime sin herramientas externas.
- Modelo uniforme para open/read/write/stat.

## Puntos Debiles

- Algunos endpoints son intencionalmente minimos y deben crecer.
- La cobertura de casos borde depende todavia de pruebas runtime.
