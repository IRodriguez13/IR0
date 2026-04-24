# Arquitectura de Filesystem en IR0

IR0 usa un diseno VFS-first donde la politica se centraliza y los backends
implementan operaciones concretas.

## Capas Activas

1. `fs/vfs.c`: resolucion de paths, dispatch de mounts y flujo open/read/write.
2. Backends:
   - persistente: `minix`
   - en memoria: `tmpfs`
   - pseudo: `procfs`, `devfs`, `sysfs`
3. Drivers de storage/dispositivos accedidos via API de backend.

## Set Actual de Filesystems

- Root filesystem: seleccion por config con `vfs_init_root()`.
- `procfs`: estado runtime de kernel y procesos.
- `devfs`: puntos de entrada tipo dispositivo.
- `sysfs`: estado runtime de subsistemas.
- `tmpfs`: arbol volatil con uid/gid y umask en create.
- `minix`: filesystem base en disco.

## Modelo de Permisos en el Path

- Los checks usan credenciales efectivas (`euid`, `egid`).
- `chmod`: owner-o-root en frontera syscall y VFS.
- `chown`: root-only en frontera syscall y VFS.
- Los backends tambien aplican politica para evitar bypass.

## Semantica y Comportamiento

- Errores como `errno` negativo en forma consistente.
- `O_TRUNC` soportado por dispatch de truncate en VFS.
- Paths relativos resueltos con `cwd` por proceso.
- Contexto por proceso en `/proc` evita colisiones de pseudo-fd.

## Puntos Fuertes

- Separacion clara entre politica VFS e implementacion backend.
- Composicion configurable por Kconfig + Makefile.
- Alta observabilidad via pseudo-filesystems.

## Puntos Debiles

- La paridad de backends para semantica Unix avanzada sigue evolucionando.
- Algunos casos borde de metadata aun estan en nivel hobby-kernel.
- La correccion fuerte depende de pruebas de integracion amplias.
