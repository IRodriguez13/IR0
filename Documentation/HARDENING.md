# IR0 — Architecture hardening backlog

> **Last verified:** 2026-06-23  
> **Source of truth:** `kernel/syscalls.c`, `kernel/process.*`, `includes/ir0/*`, `scripts/architecture_guard.py`, CTR gates in `Makefile`, ktest runner in `kernel/test/test_runner.c`.

This document tracks **post-milestone sanitization** (not new features). Each sprint must close with green CTR gates before the next feature oleada. Canonical sprint IDs also appear in [`ROADMAP.md`](ROADMAP.md) and `.cursor/rules/ir0-optimization-arch-sprints.mdc`.

---

## Oleada cerrada (2026-06-23) — hardening + ARCH-1

| Area | Change | Evidence |
|------|--------|----------|
| Supplementary groups | `ir0_access_from_stat_groups()` + wiring | `ktest_cred_access_contract` |
| ARCH-3 lifecycle | devfs FD release, fork rollback | `kernel-tests` |
| ARCH-4 log hygiene | exit/destroy/exec gated `CONFIG_DEBUG_FASE50` | boot log |
| ktest open ABI | `KTEST_O_*` Linux flags | mount ktests |
| ATA sector count | `ata_get_size()` + IDENTIFY `.size` | `block_hda_read_contract` |
| **ARCH-1 syscall split** | `syscalls.c` **86 lines** (glue only) | submodules: `fs_path_syscalls`, `validate_user`, mm/process/io extended |
| **H2** FASE hot path | O(n) checkpoints every fork/exit | Side table; fields out of `process_t` |
| **H3** facades | 12 headers with drivers | **0** — arch-guard rule 14 |
| **H5** sys_read devfs | Dual path | `devfs_resolve_read_fd()` |
| **H4** | `test_musl_cred_abi` + `includes/ir0/abi/musl_cred_abi.h` | host 12/12 |
| **H5** | Removed dead `block_dev_register`/`get` stubs | `block_dev.c` |
| **H6** | `usb_host.c` describe fix (`hc` + return) | `build-matrix-min` |

**Gates (2026-06-23 post-H2/FASE migration):** full CTR green (29 ktests, 12 host tests); `kernel-text-budget` ~815754 B; `make health` includes text budget.

---

## Oleada anterior (2026-06-23 permisos)

| Area | Change | Evidence |
|------|--------|----------|
| Supplementary groups | `ir0_access_from_stat_groups()`; wired in `fs/permissions.c`, `kernel/credentials.c`, `includes/ir0/path_routed.c`, `kernel/syscalls.c` (`access`, `faccessat`, `chdir`) | `ktest_cred_access_contract`, `make smoke-multiuser-perms` |
| ARCH-3 lifecycle | `process_release_fds()` → `devfs_close_node()`; `fork_rollback()` releases child FDs | Code review + `kernel-tests` |
| ARCH-4 log hygiene | Serial storms in `process_exit` / `process_destroy` / exec fail gated with `CONFIG_DEBUG_FASE50` | Boot log on `smoke-tier1` |
| ktest open ABI | Direct `sys_open()` from ktest must use **Linux** open bits (`KTEST_O_*` in `kernel/test/ktest_harness.h`), not `includes/ir0/fcntl.h` `O_*` (IR0 internal) | `mount_*` ktests green |
| Block layer | `ata_identify` fills `ata_devices[].size`; `ata_get_size()` falls back to `capacity_bytes / 512` | `ktest_block_hda_read_contract` |
| devfs read | `sys_read` on bound devfs FDs uses `fd_table[fd].offset` | Sequential `/dev/hda` reads |
| sys_mount noise | No stderr flood on expected `-EBUSY` | Cleaner ktest serial |

**Gates (2026-06-23):** `kernel-x64.bin`, `kernel-tests` (29/29), `runtime-mount-check`, `arch-guard`, `build-matrix-min`, `tests/host run` (11/11).

---

## ktest ABI rule (do not regress)

`sys_open(2)` in `kernel/syscalls/fs_syscalls.c` always runs `linux_open_flags_to_ir0()` on incoming flags (Linux/musl ABI).

| Caller | Correct flags |
|--------|----------------|
| Userspace / `debug_bins` via `syscall(SYS_OPEN, …)` | Linux `O_*` (musl headers) |
| In-kernel ktest calling `sys_open()` directly | `KTEST_O_CREAT`, `KTEST_O_TRUNC`, `KTEST_O_RDWR`, … from `ktest_harness.h` |
| Wrong | `includes/ir0/fcntl.h` `O_CREAT` (`IR0_O_CREAT` = `0x100`) passed into `sys_open` — **`O_CREAT` and `O_TRUNC` are silently dropped** |

Symptom: mount succeeds, create/open on mounted FS fails with `-ENOENT` and no obvious VFS error.

---

## Massive hardening plan (concrete sprints)

Ordered by **risk reduction per diff size**. Each row: entry gate → files → deliverable → verification.

### Sprint H1 — ARCH-1 finish syscall monolith — **DONE**

`kernel/syscalls.c` is **86 lines** (init + stdin wake). Submodules:

| File | ~Lines | Role |
|------|--------|------|
| `fs_syscalls.c` | 1981 | open/read/write/stat/… |
| `io_syscalls.c` | 1479 | close/dup/pipe/poll/ioctl/fcntl |
| `process_syscalls.c` | 1308 | fork/exec/wait/signals/cred |
| `mm_syscalls.c` | 1038 | mmap/munmap/mprotect/brk |
| `fs_path_syscalls.c` | 570 | mount/path/chmod/access |
| `syscall_dispatch.c` | 526 | table + dispatch |
| `socket_syscalls.c` | 337 | UDP socket ABI |
| `time_syscalls.c` | 89 | clock/time |
| `validate_user.c` | 98 | userspace pointer validation |

**Rule:** new syscalls go in the submodule matching Linux man7 section; `syscalls.c` must not grow.

---

### Sprint H2 — FASE audit dead weight — **DONE (2026-06-23)**

**Done:** FASE43–48 logic moved to `kernel/debug/fase_audit.c` (~1150 lines); `process.c` reduced (~2980 lines). Side table for `fase44_*` / `fase46_*`; hooks `fase_audit_note_*()` on hot path. `process_reap_zombie_child()` stays in `process.c` (production reap); debug wrap `fase_audit_reap_zombie()`.

**Legacy smokes:** build with `IR0_DEBUG_PROC=1` for FASE44–46 serial tags (`setup/pid1/init_fase*.c`).

---

### Sprint H3 — ARCH-2 facade decoupling — **DONE (2026-06-23)**

All `includes/ir0/*.h` decoupled from `<drivers/...>`. New canonical headers: `video_console.h`, `typewriter.h`, `vbe.h`, `ps2_mouse.h`; driver trees use shims. `arch-guard` rule **14** fails on regression (`rg '#include <drivers/' includes/ir0` → **0**).

---

### Sprint H4 — ARCH-5 host / ktest contracts — **DONE (musl)**

| Item | Status |
|------|--------|
| `test_musl_cred_abi.c` | Wired; uses `includes/ir0/abi/musl_cred_abi.h` |
| `test_ktm_panic_inventory.c` | Still via `make ktm-check` only |
| ktest `KTEST_O_*` | Documented |

---

### Sprint H5 — Duplicate / stub code removal — **DONE (sys_read devfs)**

| Code | Status |
|------|--------|
| `block_dev_register` / `block_dev_get` | **Removed** |
| Legacy `FD_DEV_BASE` vs `devfs_node_from_fd` in `sys_read` | **Merged** via `devfs_resolve_read_fd()` |

**Done when:** `make kernel-tests` + `make -C tests/host run`; no new `-Wunused-function` in touched files.

---

### Sprint H6 — Log + size budget — **DONE (2026-06-23)**

| Check | Status |
|-------|--------|
| USB host `-Wreturn-type` | **Fixed** |
| FASE46 wait hot path | Gated `IR0_DEBUG_PROC` |
| Kernel `.text` budget gate | **`make kernel-text-budget`** (wired into `make health`) |
| FASE module split | `kernel/debug/fase_audit.c` — audit-only; production paths in `process.c` |

---

## Release 0.0.1 baseline (assumed done — feature oleada)

Per maintainer sign-off, treat as **closed for 0.0.1 final** (smokes not re-run unless regression):

| Item | Notes |
|------|--------|
| runit boot | `smoke-runit-boot` |
| tcc / toolchain slice | tier-1 build path |
| BusyBox applets expansion | rootfs manifest |
| COW fork | MM vertical |
| lazy alloc | demand paging / mmap |

**Next work:** tier/features only after hardening H1–H6 green (this doc) + CTR gates below.

---

## What **not** to harden yet (feature work)

- TCP stream syscalls, X11, WM/compositor (T3)
- SMP, CFS scheduler backend
- Full FAT16 write path, EXT2, AHCI
- Kernel module loader (MOD-*)

Do **not** reopen H1–H6 unless regression; next work is P1-storage / tier features per [`STABLE.md`](STABLE.md).

---

## Recommended execution order

```text
H1 ✓ → H2 ✓ → H3 ✓ → H5 ✓ → H6 ✓ — hardening closed (2026-06-23)
```

**Escalar features / tier:** only after CTR gates green post-hardening.

**CTR gate every sprint:**

```bash
make -s kernel-x64.bin
make -s kernel-x64-test.bin && make -s kernel-tests
make -s runtime-mount-check
make -s arch-guard
make -s build-matrix-min
make -s -C tests/host run
```

After T1-sensitive changes: `make smoke-tier1` or at minimum `make smoke-runit-boot` ×3.

---

## Related docs

- [`STABLE.md`](STABLE.md) — release 0.0.1 baseline + QEMU UI testing
- [`ROADMAP.md`](ROADMAP.md) — tier backlog and sprint catalog
- [`CHANGELOG.md`](CHANGELOG.md) — iteration log
- [`Documentation/ai_driven_dev/rules/ir0-optimization-arch-sprints.md`](ai_driven_dev/rules/ir0-optimization-arch-sprints.md)
- [`tests/README.md`](../tests/README.md) — ktest / QEMU gates (Spanish ops guide)
