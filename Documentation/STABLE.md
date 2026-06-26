# IR0 — Stable baseline (release 0.0.1)

> **Last verified:** 2026-06-26  
> **Source of truth:** `make release-0.0.1` (3/3 green on 2026-06-26), `Documentation/releases/IR0_0.0.1_SCOPE.md`, `Makefile` smoke targets, CTR gates.

This document is the **single checklist** for what is **stable enough to run and test in QEMU** (serial and GTK UI), what was formerly **in development** and is now closed for **0.0.1**, and what remains **future work** (see [`ROADMAP.md`](ROADMAP.md) P1+).

Maintainer sign-off (2026-06-26): treat the items below as **done for 0.0.1-rc1** after `make release-0.0.1` is green **3/3 consecutivas** (evidencia en `/tmp/release-run-{1,2,3}.log`).

---

## Release gate (0.0.1) — D1.20

```bash
make release-0.0.1          # kernel-text-budget + smoke-release-0.0.1
make smoke-release-0.0.1    # bundle determinista (sin health duplicado)
```

**`smoke-release-0.0.1` incluye (en orden):**

| Gate | Valida |
|------|--------|
| `roadmap-phase1-stability` | `kernel-x64.bin`, host tests, `kernel-tests`, memsafe, analyze, `build-matrix-min`, `arch-guard`, `smoke-mm-cow-lazy` |
| `linux-abi-audit` | contrato `brk` Linux↔IR0 + host/ktest |
| `smoke-runit-ash-interactive` | runit + BusyBox ash + TTY `echo hi` |
| `smoke-fat16-mount` | FAT16 read-only en `/dev/hdb` |

**`release-0.0.1` añade:** `kernel-text-budget`.

**Fuera del release gate (manual / WARN):**

- `smoke-fase52-tcc` — cuelgue en link estático en QEMU; no declarar TCC stable hasta smoke verde.
- `smoke-fase55*` Doom — tier-2 opcional; no requerido para 0.0.1-rc1.
- `IR0_LEGACY_SMOKE=1` fase53b — superseded por ktests + gate D1.20.

Inventario honesto: [`Documentation/releases/IR0_0.0.1_SCOPE.md`](releases/IR0_0.0.1_SCOPE.md).

**Debug release:** `CONFIG_DEBUG_D1_DIAG=n`, `CONFIG_KTM_MALLOC_FORENSICS=n`, `CONFIG_DEBUG_PAGE_FAULTS=n` en defconfig — boot sin tags `[D1.*]` / `[PF_AUDIT]`.

---

## Release 0.0.1 scope

| Area | Status | Primary proof |
|------|--------|---------------|
| **Hardening H1–H6** | **Closed** | [`HARDENING.md`](HARDENING.md); `make health` |
| **runit boot** | **Stable** | `make smoke-runit-boot` |
| **BusyBox ash + applets** | **Stable** | `make smoke-tier1`, `make smoke-runit-ash-interactive`; optional `smoke-fase58l-busybox-coreutils` |
| **TinyCC in-guest** | **Experimental** | Manual `build-tcc-fase52`; `smoke-fase52-tcc` **no** en release gate |
| **COW fork** | **Stable** | `make smoke-mm-cow-lazy` (FASE40 A–F) |
| **Lazy allocation** | **Stable** | `CONFIG_LAZY_ANON_MMAP`, `CONFIG_LAZY_BRK_HEAP`; same smoke |
| **T1 POSIX slice** | **Stable** | tier1 + musl manifests; cred/pthread/setuid smokes |
| **T2 graphics path** | **Stable for test** | `/dev/fb0`, `/dev/events0`, mmap; Doom-class stubs |
| **T3 desktop** | **Not in scope** | WM/compositor out of tree — planning only |

---

## Formerly in development — now stable (0.0.1)

These items were tracked as partial, WIP, or hardening backlog. They are **closed for this release** (code + smokes; re-run gates only on regression).

### Architecture hardening (H1–H6)

| Sprint | Deliverable | Evidence |
|--------|-------------|----------|
| **H1** ARCH-1 | `kernel/syscalls.c` **86 lines**; submodules under `kernel/syscalls/` | `wc -l kernel/syscalls.c` |
| **H2** FASE | FASE43–48 in `kernel/debug/fase_audit.c`; hooks on hot path | `process.c` ~3014 L |
| **H3** facades | 0 `#include <drivers/` in `includes/ir0/` | `make arch-guard` rule 14 |
| **H4** host ABI | `test_musl_cred_abi`, `includes/ir0/abi/musl_cred_abi.h` | `make -C tests/host run` 12/12 |
| **H5** devfs read | `devfs_resolve_read_fd()` in `fs_syscalls.c` | `kernel-tests` |
| **H6** size budget | `make kernel-text-budget` in `make health` | ~815754 B / cap 850000 |

### Memory (COW + lazy)

| Item | Paths | Proof |
|------|-------|-------|
| PMM frame refcount | `mm/pmm.c` | ktests + FASE40 |
| Fork COW (hybrid) | `mm/paging.c`, `arch/x86-64/sources/fault.c` | `smoke-mm-cow-lazy` |
| Lazy anon `mmap` | `CONFIG_LAZY_ANON_MMAP` | same smoke |
| Lazy `brk` | `CONFIG_LAZY_BRK_HEAP` | same smoke |
| ELF PT_LOAD VMA metadata | `kernel/elf_loader.c`, `process_user_vma_prot()` | exec + mmap ktests |

**Not required for 0.0.1:** 2 MiB huge-page COW, optional stack COW (see ROADMAP P2/PERF-2).

### Userspace / init

| Item | Paths | Proof |
|------|-------|-------|
| runit PID1 | `setup/pid1/`, `load-userspace-runit` | `smoke-runit-boot` |
| BusyBox minimal | `setup/busybox/fase58_busybox.config`, `build-busybox-fase50-min` | `smoke-tier1` |
| BusyBox extended applets | `build-busybox-fase58-full`, `smoke-fase58l-busybox-coreutils` | optional smoke |
| Interactive ash on FB console | `includes/ir0/console.c`, TTY echo | [`fase58e-ash-interactive-console.md`](fase58e-ash-interactive-console.md) |
| musl static toolchain | `MUSL_CC`, `kernel-x64-userspace.iso` | tier1 smokes |
| TinyCC | `setup/tcc/build-fase52.sh` | `build-tcc-fase52` |

### Networking (UDP minimum)

| Item | Proof |
|------|-------|
| `socket` / `bind` / `sendto` / `recvfrom` / `connect` | tier1 manifest, `runtime-net-check` |
| UDP `accept` → `-EOPNOTSUPP` | documented in mandocs |
| **TCP stream** | **Not 0.0.1** — backlog P3 |

### Storage (phase2 baseline)

| Item | Status | Notes |
|------|--------|-------|
| ATA + `/dev/hda` read | **Stable** | `block_hda_read_contract` ktest |
| MINIX root on `disk.img` | **Stable** | default boot layout |
| VFS mount contracts | **Stable** | `runtime-mount-check`, mount ktests |
| FAT16 on `block_dev` | **Read-only MVP** | `fs/fat16_disk.c`; not full write path |
| EXT2 / AHCI / NVMe | **Future** | P1-storage |

---

## Roadmap achievements — stable for QEMU test

Everything in this table was reached in at least one oleada and has a **runnable proof** (automated smoke and/or documented GUI target). Use it to **explore the system in QEMU with UI**.

| Tier | Capability | Automated smoke | QEMU GTK (manual) |
|------|------------|-----------------|-------------------|
| **T0** | Kernel + debug_bins contracts | `make kernel-tests` (29/29) | `make run` (kernel dbgshell) |
| **T0** | pseudo-FS `/proc` `/dev` `/sys` | ktests, `runtime-mount-check` | explore from ash after tier1 boot |
| **T1** | runit + services | `make smoke-runit-boot` | `make run-fase58e-ash-gui` |
| **T1** | BusyBox ash interactive | `make smoke-tier1` | `make run-irinit-interactive-gui` |
| **T1** | multi-UID / permissions | `make smoke-multiuser-perms` | `su` / `id` in ash (rootfs) |
| **T1** | pthread / futex path | `make smoke-musl-pthread` | — |
| **T1** | setuid exec | `make smoke-setuid-exec` | — |
| **T1** | MM COW + lazy | `make smoke-mm-cow-lazy` | — |
| **T2** | framebuffer `/dev/fb0` | legacy `smoke-fase54a-fbdev`¹ | `make run-fase58c-fbdev-gui` |
| **T2** | input `/dev/events0` | legacy `smoke-fase54b-input`¹ | keyboard in GTK window |
| **T2** | Doom-class client stub | legacy `smoke-fase55b-doom-stub`¹ | `make run-fase55d-doomgeneric-gui` |
| **Dev** | KTM inventory | `make ktm-check` | — |
| **Dev** | arch + host contracts | `make arch-guard`, `make -C tests/host run` | — |

¹ Legacy phase smokes: `IR0_LEGACY_SMOKE=1 make smoke-fase54a-fbdev` (see `setup/make/legacy-smokes.mk`). Tier1 smokes above do **not** require legacy flag.

### Phase gates (batch confidence)

```bash
make roadmap-phase1-stability    # CTR + smoke-mm-cow-lazy
make roadmap-phase2-driver-expansion   # + runtime-net + runtime-mount
```

---

## QEMU — test the system with UI

**Prerequisites:** `make deptest` green; `musl-tools` for userspace ISO; QEMU with GTK (`-display gtk`).

### Quick path: runit + ash (recommended)

```bash
make defconfig
make run-fase58e-ash-gui
```

Builds its own temporary MINIX disk (irinit + BusyBox). **No** `IR0_LEGACY_SMOKE=1` — target lives in main `Makefile`.

- **Display:** QEMU GTK window (keyboard focus required for typing).
- **Expect:** BusyBox ash prompt `#`, echo on Enter, basic applets (`ls`, `pwd`, `echo`).
- **Serial:** stdio in the same terminal (tags for bring-up).

Details: [`fase58e-ash-interactive-console.md`](fase58e-ash-interactive-console.md), [`SETUP.md`](../SETUP.md).

### Alternative GUI entry points

| Target | What you get |
|--------|----------------|
| `make run` | Kernel debug shell (no `/sbin/init`) — VBE console |
| `make run-irinit-interactive-gui` | irinit → BusyBox ash (optional Doom if WAD injected) |
| `make run-fase58c-fbdev-gui` | Framebuffer probe on `/dev/fb0` |
| `make run-fase55d-doomgeneric-gui` | doomgeneric stub (needs IWAD + inject — see SETUP) |

### Headless regression (CI-style)

```bash
make smoke-tier1              # runit-boot + ash-interactive (automated)
make smoke-mm-cow-lazy
make kernel-tests
make -C tests/host run
make health                   # analyze + text budget + memsafe + ktests
```

---

## CTR gates (every merge)

```bash
make -s kernel-x64.bin
make -s kernel-x64-test.bin && make -s kernel-tests
make -s runtime-mount-check
make -s arch-guard
make -s build-matrix-min
make -s -C tests/host run
make -s kernel-text-budget
```

After T1-sensitive changes: `make smoke-tier1` or at minimum `make smoke-runit-boot` ×3.

---

## Explicitly **not** stable (do not claim in 0.0.1)

| Item | Where tracked |
|------|----------------|
| TCP stream syscalls | ROADMAP P3 |
| AF_UNIX, X11, Wayland | ROADMAP P3 / T3 prep |
| WM + panel | T3 — out of kernel tree |
| SMP, CFS scheduler backend | ROADMAP P2 |
| Kernel module loader (MOD-*) | ROADMAP P2 |
| FAT16 write, EXT2 ro, AHCI | ROADMAP P1-storage |
| `pthread_create` via musl libc only (no inline clone in smoke) | ROADMAP POSIX-1 |
| Full PTY / job control | ROADMAP P1-term |

---

## Related docs

| Doc | Topic |
|-----|--------|
| [`ROADMAP.md`](ROADMAP.md) | Full backlog P0–P3, evolution milestones |
| [`HARDENING.md`](HARDENING.md) | H1–H6 sprint detail |
| [`CHANGELOG.md`](CHANGELOG.md) | Iteration log |
| [`SETUP.md`](../SETUP.md) | Build, disk inject, Doom/TCC optional |
| [`TOOLING.md`](TOOLING.md) | Makefile targets taxonomy |
| [`esp/STABLE.md`](esp/STABLE.md) | Spanish mirror |
