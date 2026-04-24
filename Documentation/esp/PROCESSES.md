# Modelo de Procesos en IR0

El manejo de procesos en IR0 prioriza ciclo de vida claro y semantica Unix de
credenciales en forma incremental.

## Areas Core

- Ciclo de vida y tablas en `kernel/process.c` y `kernel/process.h`.
- Integracion con syscalls en `kernel/syscalls.c`.
- Handoff al scheduler via scheduler API.
- Rutas de senales y wait/reap integradas al estado de proceso.

## Datos Clave por Proceso

- PID/PPID y enlaces de lista de procesos.
- Metadatos de contexto/tarea y address-space.
- Tabla de file descriptors y directorio de trabajo.
- Credenciales: `uid/gid/euid/egid` y `umask`.
- Estado de senales pendientes y metadata de salida.

## Semantica Actual de Credenciales

- Los checks de permisos usan credenciales efectivas.
- Superficie de syscalls de identidad:
  - `getuid/geteuid/getgid/getegid`
  - `setuid/setgid`
  - `umask`
- Existe un modelo minimo de usuarios para separacion root/user.

## Puntos Fuertes

- Ciclo de vida explicito con wait/reap bien definido.
- Las credenciales ya participan en decisiones de politica reales.
- Mejor alineacion con ownership y permisos tipo Unix.

## Puntos Debiles

- El modelo completo de cuentas/sesion sigue siendo liviano.
- Algunos casos borde de fork/exec/credenciales aun maduran.
- El modelo de threads no es foco principal por ahora.
