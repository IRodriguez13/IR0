# Modelo de procesos de IR0

| Campo | Valor |
|-------|-------|
| Versión | 0.1 |
| Fase IR0 | T1 |
| Estado | stable |
| Depende de | scheduler, memory, syscalls, elf_loader |
| Página man | ir0-process (sección 7) |
| Fuentes principales | `kernel/process.c`, `kernel/process.h`, `kernel/elf_loader.c`, `kernel/credentials.c` |

## 1. Visión general

Un **proceso** envuelve un `task_t` planificable, tablas de páginas de usuario
aisladas (excepto idle), tabla fd, credenciales, cwd, estado de señales y lista
mmap. Las rutas de creación incluyen **spawn** (espacio de direcciones nuevo),
**fork** (copia completa de memoria) y **exec** (reemplazo de imagen). Userspace
POSIX musl depende de fork/execve y wait4.

## 2. Arquitectura interna

**Aspectos destacados de `process_t`:**

```text
  task_t task          (offset 0 — contrato ASM)
  page_directory, owns_page_directory
  fd_table[64]
  cwd[256], comm[16]
  uid/gid/euid/egid/umask
  mmap_list
  signal_pending, signal_handlers[], signal_mask
  syscall_user_frame, irq_frame_saved (resume syscall bloqueada)
  state: READY | RUNNING | BLOCKED | ZOMBIE
```

| API | Archivo | Uso |
|-----|---------|-----|
| `spawn_user` | process.c + elf_loader | Proceso ELF nuevo |
| `fork_process_create` | process.c | fork POSIX |
| `exec_replace_current` | elf_loader.c | execve in-place |
| `kexecve` | elf_loader.c | Carga iniciada por kernel |

## 3. Flujo de datos

**spawn / kexecve:**

```text
  vfs_read_file(path) → validar ELF64
       → spawn_user → nuevo PML4
       → elf_load_segments (map + copy bajo CR3 kernel)
       → elf_setup_stack (argc/argv/envp)
       → sched_add_process → retornar pid
```

**fork:**

```text
  sys_fork → fork_process_create (memcpy process_t)
       → fork_child_mm_create (nuevo PML4)
       → copy_process_memory (copia completa, SIN COW)
       → duplicate_fd_table (ref pipes/devfs/vfs)
       → child task.rax = 0, parent obtiene pid
```

**exec:**

```text
  sys_execve → exec_replace_current
       → cargar nuevo ELF en proceso actual
       → process_exec_close_cloexec()
       → en fallo: exec_fail_kill()
```

## 4. Responsabilidades

- La capa process posee ciclo de vida y tabla fd; el scheduler posee transiciones RUNNING/READY.
- ELF loader debe terminar mapeo antes de re-añadir al scheduler.
- Fork debe duplicar lista mmap e incrementar refcounts de objetos compartidos.

## 5. Límites del subsistema

- No planificar procesos user a medio construir.
- Entrega de señales interactúa con frames syscall guardados — captura específica de arch en entrada.
- Pipes IPC viven en tabla fd vía `pipe_t*` en campo `vfs_file`.

## 6. Relaciones con otros subsistemas

| Vecino | Interacción |
|--------|-------------|
| Scheduler | `sched_add/remove`, estado BLOCKED en wait |
| MM | PML4 por proceso, mmap_list, límites brk |
| VFS | resolución cwd, vfs_file en entradas fd |
| Syscalls | fork/exec/wait/kill/signals |
| Credentials | `check_file_access`, syscalls setuid |

## 7. Mapas visuales

```text
  fork                    exec
  parent ──► child         imagen vieja ──► nuevo ELF
  (copia toda RAM)         (mismo pid, nuevos mappings)

  herencia fd:
  spawn: tabla nueva (solo stdio 0-2)
  fork:  duplicar todos los fds + refcount objetos compartidos
  exec:  cerrar FD_CLOEXEC
```

Ciclo de vida:

```text
  spawn/fork ──► READY ──► RUNNING ──► exit ──► ZOMBIE ──► reap
                              │
                              └── block ──► BLOCKED ──► wake ──► READY
```

## 8. Invariantes importantes

1. **Sin COW en fork** — copia completa de memoria (documentado en fuente).
2. `MAX_FDS_PER_PROCESS = 64`.
3. Proceso idle comparte CR3 kernel (`owns_page_directory = 0`).
4. Hijo retorna 0 desde fork vía `task.rax`; parent obtiene pid del hijo.
5. CLOEXEC respetado en exec vía `process_exec_close_cloexec`.
6. **`wait4(pid, NULL, …)`** — el padre puede omitir `status`; el kernel bloquea y reanuda el frame de syscall correctamente.
7. **`wait4` bloqueante (options=0)** — bloqueo con `process_arm_kernel_syscall_sleep` (CS ring-0); la reanudación debe usar **kernel_ret** de vuelta a `process_wait`, no `iretq` user con `rax=0`. `ret=0` solo con WNOHANG si hay hijos vivos sin zombie.

## 9. Consejos de depuración

- `[EXEC_AUDIT][VFS]` durante carga ELF cuando auditoría activa.
- wait4 sobre zombie: asegurar que parent hace reap o init adopta huérfanos.
- Segfault tras exec: comprobar mapeo PT_LOAD y setup de pila.
- Smoke musl: `make smoke-userspace-musl`.

## 10. Hoja de ruta futura

- Fork copy-on-write — **no implementado**.
- Control de jobs / grupos de procesos completo — **no implementado**.
- Lista robust futex — syscall registrada; profundidad limitada.
- Paridad thread clone (`CLONE_VM`) — `sys_clone` parcial.
- Entrega de señales a syscalls bloqueadas — existe ruta guardado de frame; completitud en evolución.

Legado: `Documentation/PROCESSES.md`.
