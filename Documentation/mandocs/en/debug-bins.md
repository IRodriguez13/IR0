# IR0 Debug Shell (debug_bins) — removed

| Field | Value |
|-------|-------|
| Version | 0.3 |
| IR0 phase | historical |
| Status | **removed from tree** (2026-07-25) |
| Depends on | — |
| Man page | IR0-debug-bins (section 7) |
| Primary sources | git history only; product path: [`USERSPACE.md`](../../USERSPACE.md) |

> **Last verified:** 2026-07-25

## 1. Overview

The in-kernel **dbgshell** (`debug_bins/`, `kernel/init.c` / `start_init_process`)
was the Tier-0 laboratory REPL. It is **gone from the IR0 tree**.

Product and lab exploration use the sibling **[IR0-userspace](https://github.com/IRodriguez13/IR0-userspace)**
(runit → getty/ash) plus kernel contracts (`make kernel-tests`, linux-abi audits).

`make run-dbgshell` exits with a retirement message. There are no
`CONFIG_KERNEL_DEBUG_SHELL` / `CONFIG_DEBUG_BINS*` Kconfig symbols.

## 2. Replacement map

| Former role | Current path |
|-------------|--------------|
| PID1 REPL | `/sbin/init` (runit) via `kexecve` |
| Shell cmds (`ls`, `cat`, …) | BusyBox ash applets in IR0-userspace |
| Syscall / proc contracts | `kernel/test/*`, `make kernel-tests` |
| Boot / kmsg exploration | ash + `cat /proc/kmsg` ([`KLOG.md`](../../KLOG.md)) |
| Coupling docs | [`USERSPACE.md`](../../USERSPACE.md), `userspace/README.md` |

## 3. Do not

- Re-add `debug_bins/` or an in-kernel mono shell without a dedicated oleada + maintainer OK.
- Treat old README “debug_bins %” tier claims as current product readiness.
- Document `CONFIG_KERNEL_DEBUG_SHELL=y` as a supported product profile.

## 4–10. Historical note

Earlier versions of this chapter described Kconfig groups, `dbgshell.c`, and
`debug_bins_registry.c`. Those sources exist only in git history prior to the
`chore/kernel-userspace-boundary` removal. See section 10 of other mandocs for
related debt; this chapter no longer describes live code.
