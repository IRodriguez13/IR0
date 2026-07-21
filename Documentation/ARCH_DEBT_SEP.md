# ARCH debt inventory (post-DESK / SEP + Campaña 1)

> **Last verified:** 2026-07-18  
> **Source of truth:** `scripts/architecture_guard.py`, Makefile smokes,  
> [`HOSTSHARE_PRODUCT.md`](HOSTSHARE_PRODUCT.md),  
> sibling [`IR0-desktop/Documentation/TREE_CONTRACT.md`](../../IR0-desktop/Documentation/TREE_CONTRACT.md)

## Campaña 1 — ARCH-DEBT (closed this wave)

| ID | Area | Result |
|----|------|--------|
| **P0-2 / C1-A** | Pseudo-fd routing | **DONE** — policy in `pseudo_fs.h`; `sys_close` prefers `fd_table` is_pseudo; legacy `open_path`/`find_by_fd` documented; host test acquire path |
| **P1-2 / C1-B** | Log hygiene | **DONE** — wait/exec already `IR0_DEBUG_WAIT` / `CONFIG_DEBUG_FASE50`; `SIGNAL_DELIVER_LOG` default **0** |
| **P1-1 / C1-C** | Lifecycle pipe/fd | **AUDITED** — `sys_pipe_install` rolls back on EMFILE/`copy_to_user` fail; `pipe_acquire_end`/`close_end` balanced; no code fix required |
| **P0-3 / P1-3 / C1-D** | F8 honesty + hostshare | **DONE** — mandoc/STABLE; [`HOSTSHARE_PRODUCT.md`](HOSTSHARE_PRODUCT.md); VBox not canónico; E1000 BLOCKED |

## Current hygiene snapshot

| Check | Result |
|-------|--------|
| `python3 scripts/architecture_guard.py` | Run at C1 close |
| Kernel ↔ TinyX code link | None |
| `release-0.0.1` depends on IR0-desktop | No |

## Campaña 2 — TINYX-LAB

| ID | Area | Status |
|----|------|--------|
| **P0-1** | TinyX guest stable | **BLOCKED** — see [`IR0-desktop/Documentation/TINYX_LAB.md`](../../IR0-desktop/Documentation/TINYX_LAB.md): `force_tinyx` → `XSERVER_SELECT tinyx` then `#PF`/`#GP` + `CONTEXT_LIFETIME_BROKEN` → `XFBDEV_BOOT_FAIL not_running` (RIP→`abort`) |
| **P0-2** | Desk session kernel panic | **PARTIAL** — `#UD` executing `.bss` (`current_process`) via PIE `%rbx`+`call*` under context switch; fixed `-fno-pie -fno-pic` (Makefile). Residual panic/timeout on `smoke-desk-session` — see [`IR0-desktop/Documentation/DESK_SESSION.md`](../../IR0-desktop/Documentation/DESK_SESSION.md) |
| **P0-4** | Manual QEMU GTK checklist | **Deferred** until TinyX lab green (checklist in TINYX_LAB.md) |

## Still out of ARCH debt (BLOCKED / product future)

| ID | Area | Notes |
|----|------|-------|
| **P1-4** | Desktop rootfs v0 ship | Skeleton only (`build-desktop-rootfs.sh` fail-closed) |
| **P1-5** | ARM / RPi | musl aarch64 BLOCKED |
| **P2-1…5** | ClassiCube+GL, JVM, move BusyBox, virtiofs, CFS/SMP | Unchanged |

## Anti-patterns

- Starting product WM before Campaña 2 TinyX PASS or honest BLOCKED.  
- Adding new `PSEUDO_FS_*_FD_BASE` values.  
- Claiming VBox or E1000 without drivers/smokes.
