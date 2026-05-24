# FASE57 Experimental Reintegration Plan

Controlled rollback from `fase57-experimental-broken-arch-prctl` onto stable
(`8012866` → `fase57-stable-base`), re-applying experimental work in atomic
vertical slices with smoke gates between each step.

## Branch / commit baseline

| Commit | Step | Status |
|--------|------|--------|
| `ab9b7c5` | A — panicex/log cleanup | **Merged** |
| `27680c1` | B-minimal — console/TTY facade | **Merged** |
| `9b0765d` | C — TF/#DB gating | **Merged** |
| — | D — process list sentinel / FASE57L | **Deferred** (see below) |
| — | E — per-process kstack | **Reference only** — branch `fase57-e-kstack-probe` (do not merge) |
| — | F — syscall entry / FASE57O sysret | **Next** — branch `fase57-f-syscall-probe` |

Experimental reference preserved on branch `fase57-experimental-broken-arch-prctl`
(commit `cc55ee5`). Rollback summary on that branch:
`Documentation/fase57-experimental-rollback-summary.md`.

## Step D — **Deferred** (do not apply before E/F)

### Decision

**Step D is out of the current plan.** Do not cherry-pick or apply D until E/F
are understood in isolation and there is a reproducible bug or an approved
explicit refactor.

### Rationale

1. **No real bug today** — TCC and BusyBox pass on B+C; no evidence that
   `current_process` is invalid because of list semantics.
2. **Original diagnosis was a semantic false positive** — see technical note
   below. Logs that looked like “current == list head” were often
   `current_process == process_list` where both pointed at the **first live
   process**, not a sentinel.
3. **High cost / low benefit** — ~9 files, sentinel migration, magic fields,
   procfs and syscall debug walkers, scheduler hooks.
4. **Regression risk** — experimental D bundles changes that are **not** list
   hygiene:
   - `rr_sched` **idle fallback** when all tasks are blocked (semantic scheduler
     change; same risk class as the Step C `switch_context_x64` regression).
   - **Wait/reap reorder** (`sched_remove_process` in reap path, syscall_block
     wake paths) — overlaps pre-E/F wait4 work.
   - **FASE57I/L tags and panics** — diagnostic, not fixes.

### What D contained (for future reference)

| Area | Experimental change | Classification |
|------|---------------------|----------------|
| `process_list` → `process_list_head` sentinel | Dummy node, magic `IR0H` | Refactor — not needed now |
| ~15 list walkers | `process_list_head.next` | Mechanical — wide diff |
| `fase57l_current.c` | `process_set_current`, asserts | Hardening — needs sentinel |
| `fase57i_context_lifetime.c` | `IR0_PROCESS_MAGIC`, iret audits | Hardening + diag — coupled to E/F fields |
| `rr_sched.c` | Skip sentinel, idle fallback, magic checks | **Risky** — idle fallback |
| `process.c` wait/reap | syscall_block, sched_remove reorder | **Risky** — pre-F |
| `process.h` extra fields | kstack, syscall_block, FASE57J/P | Out of scope (E/F/traces) |

### If D is ever revived

- Treat as an **explicit refactor**, not a “fix”.
- **Exclude** idle fallback and wait4/syscall_block changes from the same slice.
- Gates: `smoke-fase52-tcc`, `smoke-fase50-busybox`, `smoke-fase55d-doomgeneric`
  (first frame).
- Add scheduler/wait-specific tests before merge.

---

## Technical note: `process_list` is not a sentinel

**Do not confuse the current `process_list` pointer with a list-head sentinel.**

In stable (post B+C):

- `process_t *process_list` points to the **first real `process_t`** on a
  singly-linked LIFO list (see `spawn` in `kernel/process.c`).
- Initially `process_list == NULL`; after the first spawn it equals that
  process’s address.
- When only one process exists (e.g. `/sbin/init`),  
  `current_process == process_list` is **valid** — both refer to the same live
  process structure.
- A future sentinel migration (`process_list_head` dummy node +
  `process_list_head.next`) must be a **named refactor** with updated procfs,
  debug walkers, and scheduler invariants — not a drive-by “fix” during E/F.

---

## Steps E and F — evaluate separately, isolated branches

Do **not** integrate E and F on the stable line until each is evaluated on its
own branch with the baseline gates below.

| Step | Scope | Known risk |
|------|-------|------------|
| **E** | Per-process kstack (`kstack.c/h`, `process_t` fields, fork/spawn alloc) | MM/syscall stack overlap with F |
| **F** | `syscall_insn_entry_64.asm`, FASE57O sysret snapshot, user GPR restore | **musl `arch_prctl` blocker** on experimental branch |

E and F were coupled in experimental work. **E probe verdict (branch
`fase57-e-kstack-probe`): do not merge dead kstack** — compiles and alloc/canary
work, but LSTAR still uses global `kernel_syscall_stack`; adds PMM pressure and
fork-heavy OOM with no syscall benefit until F lands.

### Step F — `fase57-f-syscall-probe` (rules before any LSTAR/sysret change)

**Iteration gate (one smoke):** run **only** full TCC during F exploration.

```bash
make -s smoke-fase52-tcc    # ~170s — sole validation loop for F diffs
```

Rationale: a broken syscall entry / sysret / musl path fails TCC anyway (fork,
exec, write, toolchain). Running arch_prctl + BusyBox + Doom on every diff is
redundant; any regression in the hot path shows up in TCC.

| When | Command |
|------|---------|
| **Every F diff** | `make smoke-fase52-tcc` (full harness, `FASE52_OK`) |
| **Optional ~9s hint** | `make smoke-musl-arch-prctl` — narrow `ARCH_SET_FS` canary only |
| **Pre-merge to stable** | `make smoke-fase50-busybox` + `make smoke-fase55d-doomgeneric` |

**Stable baselines (`fase57-stable-base`, 2025-05-24):**

- `smoke-musl-arch-prctl` **PASS** (~9s) — documented reference for `arch_prctl`
- `smoke-fase52-tcc` **PASS** (~170s) — **primary F iteration gate**

**Hard rules:**

1. **No dead kstack on stable** — do not merge E alone; if F requires per-process
   stack, use an explicit **E+F branch**, not infra-only E.
2. **No syscall entry rewrite without TCC passing** — full `smoke-fase52-tcc` after
   every F diff; BusyBox + Doom only pre-merge.
3. **`arch_prctl` musl** — baseline PASS on stable; TCC subsumes it for iteration
   (experimental F broke musl via #PF on user stack — TCC or optional arch_prctl
   smoke will catch regressions).
4. **F scope:** syscall entry / sysret / user GPR restore only — no FASE57D–P
   traces, `syscall_block`, wait4 rework, or scheduler idle fallback.

**Smoke harness (stable):** `scripts/smoke_autokill.py` — autokill on success tag;
TCC profile 180s max, kills on `FASE52_OK` (~170s typical).

---

## Baseline gates (B+C line)

Run after B and C commits and before starting E/F experiments:

```bash
export BUSYBOX_SRC="$PWD/setup/third-party/busybox-1.36.1"
export REAL_WAD_PATH=/path/to/freedoom1.wad   # for Doom smoke

make -s kernel-x64.bin
make -s arch-guard
make -s smoke-fase52-tcc
make -s smoke-fase50-busybox
make -s smoke-fase55d-doomgeneric
```

---

## Excluded from all slices until explicitly scoped

- FASE57D–P diagnostic traces
- `syscall_block` / wait4 kernel-resume rework
- Scheduler idle fallback (experimental D)
- Double `sched_schedule_next` in idle loop
- Extra kernel idle spawn in `main.c` (unless separately approved)
- Large `fs_syscalls.c` / read instrumentation changes
