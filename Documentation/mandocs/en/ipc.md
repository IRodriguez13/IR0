# IR0 IPC and Pipes

| Field | Value |
|-------|-------|
| Version | 0.1 |
| IR0 phase | T0–T1 |
| Status | stable |
| Depends on | process, syscalls, scheduler |
| Man page | IR0-ipc (section 7) |
| Primary sources | `kernel/ipc.c`, `includes/ir0/pipe.c`, `kernel/syscalls.c`, `fs/devfs.c` |

## 1. Overview

IR0 provides two IPC mechanisms: **kernel message channels** via `/dev/ipc` ioctl
and in-memory **pipes** via `pipe`/`pipe2` syscalls. Pipes are the production
POSIX path for shell redirection. Channel IPC is experimental with known wake
queue limitations documented below.

## 2. Internal architecture

| Mechanism | File | API |
|-----------|------|-----|
| Channels | `kernel/ipc.c` | `ipc_channel_*`, ring buffer 4096 bytes |
| Pipes | `includes/ir0/pipe.c` | `pipe_create`, `pipe_read/write`, refcounts |
| Syscalls | `kernel/syscalls.c` | `sys_pipe`, `sys_pipe2`, `pipe_wake_*` |
| devfs | `fs/devfs.c` | `/dev/ipc` device_id 13, ioctl 0x5001–0x5003 |

Boot: `ipc_init()` in `kmain` after `process_init()`.

## 3. Data flow

**Pipe:**

```text
  sys_pipe2 → pipe_create → fd[read], fd[write] (path "/dev/pipe", is_pipe=true)
  read(fd)  → pipe_read → if empty: pipe_wait(BLOCKED) → idle poll wake
  write(fd) → pipe_write → pipe_wake_all on data
  close(fd) → pipe_close_end → EOF readers when writers==0
```

**IPC channel:**

```text
  ioctl IPC_CREATE_CHANNEL → ipc_channel_get_or_create
  write /dev/ipc → ipc_channel_write (spin if full → wait write_queue)
  read /dev/ipc  → ipc_channel_read  (spin if empty → wait read_queue)
  ioctl IPC_DESTROY → ipc_channel_unref
```

ASCII:

```text
  process A                    kernel                     process B
      │                    ┌─────────┐                        │
      ├──pipe write───────►│ ring 4K │◄────pipe read───────────┤
      │                    └─────────┘                        │
      │                    ┌─────────┐                        │
      ├──/dev/ipc write───►│ channel │◄───/dev/ipc read───────┤
      │                    └─────────┘                        │
```

## 4. Responsibilities

- Pipes: byte stream, `O_NONBLOCK`/`O_CLOEXEC` in `pipe2`; `-EPIPE` if no readers.
- Fork duplicates pipe ends with `pipe_acquire_end` refcount.
- IPC channels: global linked list; destroy wakes all waiters.

## 5. Subsystem boundaries

- Pipes are fd objects, not a real `/dev/pipe` VFS node (path string only).
- No SysV shm/msg/sem syscalls.
- IPC spinlocks are busy-wait, not mutex with full semaphore_down.

## 6. Relations to other subsystems

| Neighbor | Interaction |
|----------|---------------|
| Process | fd table `is_pipe`, `pipe_end`, `vfs_file` holds `pipe_t*` |
| Scheduler | block on empty/full; `pipe_wake_check` from idle |
| Syscalls | blocked read may resume via saved syscall frame |
| fs_syscalls | read/write branches before VFS |

## 7. Visual maps

```text
  pipe2()
     │
     ├─ fd[0] read end  ──► pipe_t.buf[4096] ◄── fd[1] write end
     │
  fork: duplicate fds + pipe_acquire_end both ends
  exec: FD_CLOEXEC closes pipe ends per process_exec_close_cloexec
```

## 8. Important invariants

1. `PIPE_SIZE = 4096`; `IPC_CHANNEL_BUFFER_SIZE = 4096`.
2. `MAX_PIPE_WAITERS = 32`.
3. `IPC_MAX_CHANNELS = 64` in header — **not enforced** in code.
4. EOF on read returns 0 when no writers remain.
5. Write with no readers → `-EPIPE`.

## 9. Debugging tips

- Tags: `[FASE49][PIPE]`, `[FASE50B][PIPE_WAKE]`, `[FASE48][IPC]`.
- Smoke expects `ipc_class=IPC_READY` in serial output.
- Shell: `cmd_tr` and redirects use pipes via standard syscalls.

## 10. Future roadmap

- **IPC channel read wake on write** — read_queue may not wake on producer write (debt); blocking `/dev/ipc` producer/consumer unreliable.
- `semaphore_down` not fully implemented for channel semaphores.
- Enforce `IPC_MAX_CHANNELS` cap.
- Unix domain sockets — not present (socket syscalls ENOSYS).

See also: `IR0-process`, `IR0-syscalls`.
