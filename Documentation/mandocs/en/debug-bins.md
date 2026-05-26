# IR0 Debug Shell (debug_bins)

| Field | Value |
|-------|-------|
| Version | 0.1 |
| IR0 phase | T0 |
| Status | stable |
| Depends on | syscalls, vfs, boot |
| Man page | IR0-debug-bins (section 7) |
| Primary sources | `debug_bins/dbgshell.c`, `debug_bins/debug_bins_registry.c`, `debug_bins/debug_bins.h`, `kernel/init.c`, `kernel/main.c` |

## 1. Overview

When `KERNEL_DEBUG_SHELL=1`, PID 1 runs the in-kernel **dbgshell** instead of
`/sbin/init`. Commands in `debug_bins/cmd_*.c` implement a minimal userspace-like
toolset (ls, cat, ping, mount, …) using **syscalls only** — no direct kernel API
calls from command handlers.

## 2. Internal architecture

| Piece | Role |
|-------|------|
| `dbgshell.c` | REPL: read stdin, parse line, dispatch |
| `debug_bins_registry.c` | `debug_commands[]` table, `debug_find_command` |
| `cmd_*.c` | Each exports `struct debug_command cmd_*` |
| `debug_bins.h` | Contract: syscall I/O helpers |
| `kernel/init.c` | `init_1` → `shell_entry()` as PID1 |
| `kernel/main.c` | `#if KERNEL_DEBUG_SHELL` → `start_init_process()` |

Command groups gated by Kconfig: `CONFIG_DEBUG_BINS_GROUP_{CORE,FS,TEXT,IDENTITY,DIAG,NET,BT}`.

## 3. Data flow

```text
  kmain → start_init_process()
       → spawn KERNEL_MODE task, entry init_1, comm "debshell"
       → shell_entry() loop:
            read(0) → parse → debug_find_command → handler(argc, argv)
            close fds 3..63 before each command
```

Registration:

```text
  cmd_ls.c: struct debug_command cmd_ls = { .name, .handler, ... }
       → listed in debug_commands[] in debug_bins_registry.c
       → linked if group enabled in Makefile
```

ASCII:

```text
  [kmain] ──► PID1 dbgshell ──► syscall ──► VFS/devfs/net
                  │
                  └── cmd_* handlers (ring 0, syscall-only style)
```

## 4. Responsibilities

- Handlers: open/read/write/close/ioctl only; no `#include` of kernel/fs headers.
- Shell: built-ins `help`, `clear`, `exit` (not in registry).
- `cmd_ktest` only when `IR0_KERNEL_TESTS` (separate registry object).

## 5. Subsystem boundaries

- Design intent: behave like ring-3 userspace (AGENTS.md).
- **Exception:** process is `KERNEL_MODE` so `copy_user` may bypass strict checks for shell stack.
- Must not call `bt_sysfs_*`, direct VFS, or driver internals from cmd modules.

## 6. Relations to other subsystems

| Neighbor | Interaction |
|----------|---------------|
| Boot | Alternative to `kexecve("/sbin/init")` |
| Syscalls | All I/O paths |
| VFS/net/proc | Via paths like `/`, `/proc/*`, `/dev/net` |
| Userspace | Mutually exclusive default with musl init (`IR0_USERSPACE_INIT_BOOT=1` forces shell off) |

## 7. Visual maps

```text
  KERNEL_DEBUG_SHELL=1          KERNEL_DEBUG_SHELL=0
         │                              │
         ▼                              ▼
    dbgshell PID1                  /sbin/init (irinit)
         │                              │
    cmd_cat/cmd_ping               BusyBox/musl
```

## 8. Important invariants

1. Input line max **255** chars; history **32** lines.
2. Max **64** args per command (`debug_parse_args`).
3. FD table sweep closes 3..63 before each external command.
4. `IR0_USERSPACE_INIT_BOOT=1` overrides Kconfig to disable debug shell.
5. Linked objects selected in Makefile per debug group.

## 9. Debugging tips

- Default `setup/defconfig` has `KERNEL_DEBUG_SHELL=y` — normal ISO boots shell unless userspace ISO built.
- `make kernel-x64-userspace.iso` sets userspace init path.
- Device checklist in dbgshell probes `/dev/fb0`, `/dev/events0`.
- Net commands require `CONFIG_ENABLE_NETWORKING`.

## 10. Future roadmap

- Reduce overlap with BusyBox once T1 init stable.
- Stricter ring-3 test harness for cmd modules (currently KERNEL_MODE).
- Split registry auto-generation — manual extern list today.

See: `IR0-boot`, `IR0-userspace`, `IR0-net`.
