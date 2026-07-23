# IPC y tuberías de IR0

| Campo | Valor |
|-------|-------|
| Versión | 0.1 |
| Fase IR0 | T0–T1 |
| Estado | stable |
| Depende de | process, syscalls, scheduler |
| Página man | IR0-ipc (sección 7) |
| Fuentes principales | `kernel/ipc.c`, `includes/ir0/pipe.c`, `kernel/syscalls.c`, `fs/devfs.c` |

## 1. Visión general

IR0 ofrece tres familias IPC en bring-up: **canales de mensajes en kernel** vía
ioctl de `/dev/ipc`, **tuberías** en memoria vía `pipe`/`pipe2`, y sockets
stream **AF_UNIX** (`socket` / `socketpair` / `bind` / `connect` / `send`/`recv`)
en `kernel/sock_stream.c` + `socket_syscalls.c`. Las tuberías siguen siendo el
path POSIX principal para redirección de shell. El IPC por canales es
experimental con limitaciones de wake documentadas abajo.

## 2. Arquitectura interna

| Mecanismo | Archivo | API |
|-----------|---------|-----|
| Canales | `kernel/ipc.c` | `ipc_channel_*`, ring buffer 4096 bytes |
| Tuberías | `includes/ir0/pipe.c` | `pipe_create`, `pipe_read/write`, refcounts |
| AF_UNIX stream | `kernel/sock_stream.c`, `socket_syscalls.c` | socket/socketpair/bind/connect/send/recv |
| Syscalls | `io_syscalls.c` / `socket_syscalls.c` | `sys_pipe`, `sys_pipe2`, familia socket |
| devfs | `fs/devfs.c` | `/dev/ipc` device_id 13, ioctl 0x5001–0x5003 |

Arranque: `ipc_init()` en `kmain` tras `process_init()`.

## 3. Flujo de datos

**Tubería:**

```text
  sys_pipe2 → pipe_create → fd[read], fd[write] (path "/dev/pipe", is_pipe=true)
  read(fd)  → pipe_read → si vacío: pipe_wait(BLOCKED) → wake en idle poll
  write(fd) → pipe_write → pipe_wake_all al escribir datos
  close(fd) → pipe_close_end → EOF lectores cuando writers==0
```

**Canal IPC:**

```text
  ioctl IPC_CREATE_CHANNEL → ipc_channel_get_or_create
  write /dev/ipc → ipc_channel_write (spin si lleno → wait write_queue)
  read /dev/ipc  → ipc_channel_read  (spin si vacío → wait read_queue)
  ioctl IPC_DESTROY → ipc_channel_unref
```

Mapa ASCII:

```text
  proceso A                    kernel                     proceso B
      │                    ┌─────────┐                        │
      ├──pipe write───────►│ ring 4K │◄────pipe read───────────┤
      │                    └─────────┘                        │
      │                    ┌─────────┐                        │
      ├──/dev/ipc write───►│ canal   │◄───/dev/ipc read───────┤
      │                    └─────────┘                        │
```

## 4. Responsabilidades

- Tuberías: flujo de bytes, `O_NONBLOCK`/`O_CLOEXEC` en `pipe2`; `-EPIPE` si no hay lectores.
- Fork duplica extremos de tubería con refcount `pipe_acquire_end`.
- Canales IPC: lista enlazada global; destroy despierta todos los waiters.

## 5. Límites del subsistema

- Las tuberías son objetos fd, no un nodo VFS real `/dev/pipe` (solo cadena de path).
- Sin syscalls SysV shm/msg/sem.
- Spinlocks IPC son busy-wait, no mutex con `semaphore_down` completo.

## 6. Relación con otros subsistemas

| Vecino | Interacción |
|--------|-------------|
| Process | tabla fd `is_pipe`, `pipe_end`, `vfs_file` guarda `pipe_t*` |
| Scheduler | bloqueo en vacío/lleno; `pipe_wake_check` desde idle |
| Syscalls | read bloqueado puede reanudar vía frame syscall guardado |
| fs_syscalls | ramas read/write antes de VFS |

## 7. Mapas visuales

```text
  pipe2()
     │
     ├─ fd[0] extremo lectura  ──► pipe_t.buf[4096] ◄── fd[1] extremo escritura
     │
  fork: duplica fds + pipe_acquire_end ambos extremos
  exec: FD_CLOEXEC cierra extremos vía process_exec_close_cloexec
```

## 8. Invariantes importantes

1. `PIPE_SIZE = 4096`; `IPC_CHANNEL_BUFFER_SIZE = 4096`.
2. `MAX_PIPE_WAITERS = 32`.
3. `IPC_MAX_CHANNELS = 64` en header — **no aplicado** en código.
4. EOF en read devuelve 0 cuando no quedan writers.
5. Write sin lectores → `-EPIPE`.

## 9. Consejos de depuración

- Tags: `[FASE49][PIPE]`, `[FASE50B][PIPE_WAKE]`, `[FASE48][IPC]`.
- Smoke espera `ipc_class=IPC_READY` en salida serial.
- Shell: `cmd_tr` y redirecciones usan tuberías vía syscalls estándar.

## 10. Roadmap futuro

- **Wake de lectura en canal IPC al escribir** — read_queue puede no despertar al escribir productor (deuda); productor/consumidor bloqueante en `/dev/ipc` poco fiable.
- `semaphore_down` no implementado por completo para semáforos de canal.
- Aplicar tope `IPC_MAX_CHANNELS`.
- AF_UNIX: profundizar credenciales/`SCM_RIGHTS`; datagram aún ausente.

Ver también: `IR0-process`, `IR0-syscalls`, `IR0-net`.
