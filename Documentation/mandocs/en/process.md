# IR0 Process Model

| Field | Value |
|-------|-------|
| Version | 0.1 |
| IR0 phase | T1 |
| Status | stable |
| Depends on | scheduler, memory, syscalls, elf_loader |
| Man page | IR0-process (section 7) |
| Primary sources | `kernel/process.c`, `kernel/process.h`, `kernel/elf_loader.c`, `kernel/credentials.c` |

## 1. Overview

A **process** wraps a schedulable `task_t`, isolated user page tables (except idle),
fd table, credentials, cwd, signal state, and mmap list. Creation paths include
**spawn** (fresh address space), **fork** (full memory copy), and **exec**
(replace image). POSIX musl userspace depends on fork/execve and wait4.

## 2. Internal architecture

**`process_t` highlights:**

```text
  task_t task          (offset 0 — ASM contract)
  page_directory, owns_page_directory
  fd_table[64]
  cwd[256], comm[16]
  uid/gid/euid/egid/umask
  mmap_list
  signal_pending, signal_handlers[], signal_mask
  syscall_user_frame, irq_frame_saved (blocked syscall resume)
  state: READY | RUNNING | BLOCKED | ZOMBIE
```

| API | File | Use |
|-----|------|-----|
| `spawn_user` | process.c + elf_loader | New ELF process |
| `fork_process_create` | process.c | POSIX fork |
| `exec_replace_current` | elf_loader.c | execve in-place |
| `kexecve` | elf_loader.c | Kernel-initiated load |

## 3. Data flow

**spawn / kexecve:**

```text
  vfs_read_file(path) → validate ELF64
       → spawn_user → new PML4
       → elf_load_segments (map + copy under kernel CR3)
       → elf_setup_stack (argc/argv/envp)
       → sched_add_process → return pid
```

**fork:**

```text
  sys_fork → fork_process_create (memcpy process_t)
       → fork_child_mm_create (new PML4)
       → copy_process_memory (full copy, NO COW)
       → duplicate_fd_table (ref pipes/devfs/vfs)
       → child task.rax = 0, parent gets pid
```

**exec:**

```text
  sys_execve → exec_replace_current
       → load new ELF into current process
       → process_exec_close_cloexec()
       → on failure: exec_fail_kill()
```

## 4. Responsibilities

- Process layer owns lifecycle and fd table; scheduler owns RUNNING/READY transitions.
- ELF loader must finish mapping before re-adding to scheduler.
- Fork must duplicate mmap list and bump shared object refcounts.

## 5. Subsystem boundaries

- Do not schedule half-built user processes.
- Signal delivery interacts with saved syscall frames — arch-specific capture at entry.
- IPC pipes live in fd table via `pipe_t*` in `vfs_file` field.

## 6. Relations to other subsystems

| Neighbor | Interaction |
|----------|---------------|
| Scheduler | `sched_add/remove`, state BLOCKED on wait |
| MM | per-process PML4, mmap_list, brk bounds |
| VFS | cwd resolution, vfs_file in fd entries |
| Syscalls | fork/exec/wait/kill/signals |
| Credentials | `check_file_access`, setuid syscalls |

## 7. Visual maps

```text
  fork                    exec
  parent ──► child         old image ──► new ELF
  (copy all RAM)          (same pid, new mappings)

  fd inheritance:
  spawn: fresh table (stdio 0-2 only)
  fork:  duplicate all fds + refcount shared objects
  exec:  close FD_CLOEXEC
```

Lifecycle:

```text
  spawn/fork ──► READY ──► RUNNING ──► exit ──► ZOMBIE ──► reap
                              │
                              └── block ──► BLOCKED ──► wake ──► READY
```

## 8. Important invariants

1. **No COW on fork** — full memory copy (documented in source).
2. `MAX_FDS_PER_PROCESS = 64`.
3. Idle process shares kernel CR3 (`owns_page_directory = 0`).
4. Child returns 0 from fork via `task.rax`; parent gets child pid.
5. CLOEXEC honored on exec via `process_exec_close_cloexec`.

## 9. Debugging tips

- `[EXEC_AUDIT][VFS]` during ELF load when audit active.
- wait4 on zombie: ensure parent reaps or init adopts orphans.
- Segfault after exec: check PT_LOAD mapping and stack setup.
- musl smoke: `make smoke-userspace-musl`.

## 10. Future roadmap

- Copy-on-write fork — **not implemented**.
- Full job control / process groups — **not implemented**.
- futex robust list — syscall registered; depth limited.
- Thread clone (`CLONE_VM`) parity — partial `sys_clone`.
- Signal delivery to blocked syscalls — frame save path exists; completeness evolving.

Legacy: `Documentation/PROCESSES.md`.
