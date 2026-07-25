# IR0 Debug Shell (debug_bins) — legacy harness

| Field | Value |
|-------|-------|
| Version | 0.2 |
| IR0 phase | T0 (test-only) |
| Status | **retired from product**; opt-in via Kconfig |
| Depends on | syscalls, vfs, boot |
| Man page | IR0-debug-bins (section 7) |
| Primary sources | `debug_bins/dbgshell.c`, `debug_bins/debug_bins_registry.c`, `kernel/init.c`, `setup/Kconfig` |

## 1. Overview

**Product path (2026-07-24+):** desktop defconfigs use
`CONFIG_KERNEL_DEBUG_SHELL=n` and `CONFIG_DEBUG_BINS=n`. Daily `make run*` boots
runit → getty/ash. T0 exploration is **ash + `/proc/kmsg`**, not the ring-0 shell.

When `CONFIG_KERNEL_DEBUG_SHELL=y` (and/or `CONFIG_DEBUG_BINS=y`), PID 1 can still
run the in-kernel **dbgshell**. Command objects under `debug_bins/cmd_*.c` remain
in-tree for migrating contracts to ktest/userspace — **do not delete the tree yet**.

## 2. Kconfig

| Symbol | Product default | Role |
|--------|-----------------|------|
| `CONFIG_KERNEL_DEBUG_SHELL` | n | PID1 = `start_init_process` / `shell_entry` |
| `CONFIG_DEBUG_BINS` | n | Link `dbgshell.o` + cmd groups into the kernel |
| `CONFIG_DEBUG_BINS_GROUP_*` | (groups) | Only linked if `CONFIG_DEBUG_BINS=y` |

Makefile: `DEBUG_BINS_ENABLED` is y if either `DEBUG_BINS` or `KERNEL_DEBUG_SHELL` is y
(so matrix profiles that still enable the shell keep a linkable `shell_entry`).

## 3. Architecture (when enabled)

| Piece | Role |
|-------|------|
| `dbgshell.c` | REPL: read stdin, parse line, dispatch |
| `debug_bins_registry.c` | `debug_commands[]` table |
| `cmd_*.c` | Syscall-only command handlers |
| `kernel/init.c` | `init_1` → `shell_entry()` (compiled only if `KERNEL_DEBUG_SHELL`) |

## 4. Migration status

| Area | Destination | Notes |
|------|-------------|-------|
| Boot / identity / kmsg | runit + `/proc/kmsg` + [`KLOG.md`](../../KLOG.md) | Done for product |
| Syscall contracts | `make kernel-tests`, linux-abi audits, ktest | Prefer over new dbgshell cmds |
| Device probes (fb0/events0) | GUI/runit smokes | See STABLE.md |
| Remaining cmd_* surface | TBD | Keep tree until contracts migrate |

## 5. Do not

- Add new product features that require dbgshell as PID1.
- Treat README “debug_bins %” as proof of product readiness.
- Re-enable `CONFIG_KERNEL_DEBUG_SHELL` in desktop defconfig without maintainer OK.
