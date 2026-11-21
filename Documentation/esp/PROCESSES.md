# Modelo de Procesos en IR0

> **Última verificación:** 2026-11-20
> **Fuente de verdad:** `kernel/process/domains.c`, `kernel/syscalls/process_syscalls.c`,
> `includes/ir0/fd_types.h`, [`PROCESSES.md`](../PROCESSES.md) (canónico en inglés)

El manejo de procesos en IR0 prioriza ciclo de vida claro y semántica Unix de
credenciales en forma incremental.

## Áreas core

- Ciclo de vida bajo `kernel/process/` (`exit.c`, `wait.c`, fork/create, …).
- Integración con syscalls en `kernel/syscalls/process_syscalls.c`.
- Handoff al scheduler vía scheduler API.
- Rutas de señales y wait/reap integradas al estado de proceso.
- Frames/`siginfo` vía `copy_to_user_mm` (nunca CR3+`memcpy`);
  contrato: [`uaccess.md`](uaccess.md).

## Datos clave por proceso

- PID/PPID y enlaces de lista de procesos.
- Metadatos de contexto/tarea y address-space.
- Tabla de descriptores y directorio de trabajo. Crece con `files_grow()`
  hasta `RLIMIT_NOFILE`, con tope `IR0_NOFILE_CEILING` (4096) y default blando
  1024. Un solo asignador: `fd_alloc_lowest()`.
- Credenciales: `uid/gid/euid/egid` y `umask`.
- Señales pendientes. `clone(CLONE_SIGHAND)` comparte la tabla; sin el flag
  se copia. `CLONE_SIGHAND` sin `CLONE_VM` es `-EINVAL`. El bit de `SIG_IGN`
  se actualiza en todas las tareas que comparten la tabla. `exec` copia la
  tabla si más de una tarea la usa (`process_signals_unshare`).
- El contexto interrumpido no queda en el kernel: vive en el `sigframe` de
  usuario, que incluye el `ucontext` de Linux. El handler lo edita y
  `rt_sigreturn(2)` lo importa. En arm64 el `mcontext` es el `sigcontext`.

## Exit, reparent, wait

En `process_exit()`:

1. Reap de zombies propios.
2. **`process_reparent_children`** → hijos con `ppid` del moribundo pasan a
   **`ppid = 1`** (init).
3. El moribundo queda zombie hasta `wait` del padre (o de init).
4. Si no hay init (o muere el propio PID 1): huérfanos con **`ppid = 0`**.

ISD no reparenta en userspace; depende del kernel.

## Semántica actual de credenciales

- Los checks de permisos usan credenciales efectivas.
- Superficie de syscalls de identidad:
  - `getuid/geteuid/getgid/getegid`
  - `setuid/setgid`
  - `umask`
- Existe un modelo mínimo de usuarios para separación root/user.

## Puntos fuertes

- Ciclo de vida explícito con wait/reap bien definido.
- Reparent de huérfanos a init al estilo Unix clásico.
- Las credenciales ya participan en decisiones de política reales.

## Puntos débiles

- El modelo completo de cuentas/sesión sigue siendo liviano.
- Algunos casos borde de fork/exec/credenciales aún maduran.
- El modelo de threads no es foco principal por ahora.
- PID 1 debe hacer `wait` de zombies reparentados.
