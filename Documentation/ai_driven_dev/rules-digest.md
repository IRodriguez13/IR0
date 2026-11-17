# IR0 rules digest

Read this at session start instead of every rule. Open a full rule
(`rules/<name>`, last line of each section) only when the task touches its
topic. `scripts/architecture_guard.py` keeps one section per rule file.

## Always

- Bug fix: red unpatched, green patched, red again when reverted; same command, config, arch. No red log, no fix.
- Refactor, move, split: green/green with identical stripped objects (x86_64, arm64) via `scripts/baseline_compare.sh --expect green-green`.
- Never revert a fix in place for the red run; use throwaway trees (`baseline_compare.sh`).
- Atomic commits, English, `git commit -s`, maintainer identity, no `Co-authored-by`/`Helped-by`/AI trailers.
- Never patch Linux userspace (BusyBox, musl, ash) for missing ABI; fix the kernel to Linux semantics.
- Portable critical paths: no `#if`, ISA or register names, raw asm or atomics; use simple Linux-named facades (`switch_to`, `smp_mb`, `<ir0/atomic.h>`); ISA code lives in `arch/`.
- One public API per behaviour; the operation validates internally (`copy_*_user` returns -EFAULT); one declaration per symbol.
- ARM64 increments keep x86_64 green; unbuilt or unbooted stages are SKIP, never success.
- Parallel only on disjoint file sets, everything else FIFO; one worktree per feature.
- Gates: `make -s test-fast`; `kernel-tests` when guest code changes; `make -s ctr` before integrating. Report commands actually run.
- Integrate by ff-merge into `dev`; push both remotes only after gates pass; never touch `master`, never force-push `dev`.
- No stubs or fake success (honest `ENOSYS`); code and tests beat docs; no aspirational docs.
- C: Allman, SPDX header, includes first and once, English comments.
- A recurring review finding becomes an arch-guard check with an injected-violation self-test.

# Proof & gates

### ir0-bugfix-red-green-proof.md
**When it applies:** any change presented as a kernel bug fix.
- Smallest deterministic test asserting the contract (ret, errno, state); run unpatched (red), same command patched (green), reverted (red).
- Commit body: baseline `git rev-parse HEAD`, both commands, failure and pass signatures, gates run.
- Never weaken a test; ARM-only bugs need both phases on that target or an explicit statement.
→ rules/ir0-bugfix-red-green-proof.md

### ir0-baseline-branches.md
**When it applies:** producing red/green or green/green evidence; branch cleanup.
- `scripts/baseline_compare.sh <ref> -- -s <target>` (`--expect green-green` for refactors); keep `--keep` logs until review.
- Never build x86 and ARM64 in one tree without `make clean`.
- Internal branches (`test/*`, `backup/*`, `split/*`) are never pushed; `git branch -d` after dev is pushed, then `git worktree prune`.
→ rules/ir0-baseline-branches.md

### ir0-audit-baseline-worktree.mdc
**When it applies:** comparing an audit, ZIP or stable ref with HEAD.
- Use `git worktree add` at the ref; never checkout or delete paths in the only clone.
- Same commands in both trees; record command, exit code and git ref.
→ rules/ir0-audit-baseline-worktree.mdc

### ir0-ci-local-pre-push.md
**When it applies:** before integrating into dev or pushing.
- Order: `test-fast`, `kernel-tests` (boot-touching diffs), `ctr` (before dev merge and again on dev).
- Add area gates (`ir0 arm64`, `ir0-selftest-*`, `smoke-mm-cow-lazy`); QEMU goals take a private `HOST_TMP=<dir>`, never in parallel.
- Removed targets (`ci-local*`, `pre-submit`) are never reported as run.
→ rules/ir0-ci-local-pre-push.md

### ir0-ktm-first-validation.md
**When it applies:** choosing how to validate a kernel change.
- Prefer KTM guest checks over host stubs when they add fidelity; area goals only when the diff touches the area.
- Gap in validation: add a small ktest or userdev probe.
→ rules/ir0-ktm-first-validation.md

### ir0-ktm-instrumentation.md
**When it applies:** adding KTM events or fault injection.
- NEVER `ktm_event_emit4` from the context switch or IRQ-off hot paths; use `ktm_deferred_record()`.
- Each `KTM_FAULT_HIT` hook needs a case asserting no leaked resource.
→ rules/ir0-ktm-instrumentation.md

### ir0-smoke-autokill.md
**When it applies:** any QEMU `smoke-*` target.
- MUST use `$(SMOKE_QEMU_RUN) --log --timeout --done <terminal tag>`; timeout is a ceiling only.
- Harness emits a unique terminal tag and `_FAIL_REASON`/`[FASE*][FAIL]`; no `timeout | tee`, no `| tail`.
→ rules/ir0-smoke-autokill.md

### ir0-smoke-behavior-validation.md
**When it applies:** smokes, host tests, ktests for console, TTY, signals, ABI.
- Reproduce the manual symptom; apply `scripts/smoke_tty_guards.py` over the session window (fatal before the tag is FAIL).
- Keyboard/login smokes run 2-3 trials; a new test must fail if the fix is reverted.
→ rules/ir0-smoke-behavior-validation.md

### ir0-ktm-host-portability-safety.md
**When it applies:** host scripts (`scripts/ktm_*`, `smoke_*`, tests/ktm, run_qemu.sh).
- POSIX sh, Python 3.9+ stdlib; no GNU-only flags; `mktemp` X's last; fresh clone must work.
- QEMU `-m` explicit and capped; bounded log reads; hard timeout, process-group kill, cleanup in `finally`.
- `make host-portability-guard` (in test-fast) enforces it; its ledger only shrinks.
→ rules/ir0-ktm-host-portability-safety.md

### ir0-optimization-arch-sprints.md
**When it applies:** after a tier milestone or wave closes green.
- Run a bounded ARCH sprint (split, facades, lifecycle, log hygiene) before the next big feature; close with `kernel-x64.bin`, `arch-guard`, `build-matrix-min`, `tests/host`.
- Optimize only measured hot paths.
→ rules/ir0-optimization-arch-sprints.md

# Layout & API design

### ir0-api-public-private.md
**When it applies:** adding or renaming a function, facade or backend.
- Public facade in `includes/ir0/` with a simple stable name; no `arch_` prefix or ISA suffix; asm-backed code gets one facade plus one impl per ISA.
- Backends and policies never include peers; all multi-CPU code lives in `smp/`.
- No `__asm__`, `__atomic_*`, `__sync_*` in portable trees (`barrier()`, `<ir0/atomic.h>`).
- Context switch is Linux-model (`switch_to`, sleep on kernel stack); no frame re-patching.
- At most one public predicate per contract; retired names stay guarded.
→ rules/ir0-api-public-private.md

### ir0-source-layout.md
**When it applies:** creating, moving or splitting files.
- Subsystem dirs hold generic code only, in atomic subdirs by responsibility; ISA code in `arch/<isa>/`; only `kernel/main.c` is loose in `kernel/`.
- A move is its own pure-move commit (identical stripped objects, both ISAs), then `scripts/include_prune.py`; update Makefile, `scripts/make/*.mk`, `subsystems.json`, tests/host Makefile and guard ledgers in the same commit, docs separately.
- Domains are isolated (fs members never reference each other); platform via `scripts/make/platform/<p>.mk`.
→ rules/ir0-source-layout.md

### ir0-scale-within-patterns.md
**When it applies:** adding a subsystem, driver, syscall or ISA.
- Order: common core in the right model, ISA-abstracted, then growth.
- Entry: minimal API, no ISA in portable paths, diagnostics in a separate file, tests on both ISAs, gates `test-fast`, `arch-guard`, `kernel-tests`, `ctr`, `ir0 arm64`.
- SMP and scheduler optimization only after the core complies.
→ rules/ir0-scale-within-patterns.md

### ir0-userspace-monolith-debt.md
**When it applies:** touching syscalls, exec, mm or sched facades.
- Do not grow `kernel/syscalls/`; new logic goes in owner subsystems or `includes/ir0/*`.
- Linux open flags translate only in the ABI layer; never assume `active_cr3` for another process (`copy_*_user_mm`).
→ rules/ir0-userspace-monolith-debt.md

### kernel-architecture-rigor.md
**When it applies:** every kernel change.
- Extend `includes/ir0/*` facades instead of coupling; selection config-driven across Kconfig, defconfig, Makefile.
- Kernel build plus `arch-guard` after substantial changes; no hardcoded runtime-derivable values; docs honest.
→ rules/kernel-architecture-rigor.md

### kernel-c-allman-style.md
**When it applies:** editing kernel C or headers.
- Allman braces; SPDX kept; includes, macros, types at top; no mid-file or duplicate include; run `scripts/include_prune.py` on new or split files.
- Private headers include the facade instead of copying prototypes.
→ rules/kernel-c-allman-style.md

# Arch/ISA

### ir0-arm64-rpi5-x86-regression-gate.md
**When it applies:** any ARM64 or Raspberry Pi 5 work.
- ISA, frames, page-table formats and platform boot stay in `arch/arm64/` or an ARM64 backend; never weaken x86_64 to advance ARM64.
- Each increment: `make -s ir0 arm64`, `ir0 arm64 rpi5`, `arch-guard`, `python3 scripts/isa_security_guard.py`, `build-matrix-min`, `kernel-x64.bin`, `-C tests/host run`; `ctr` is the x86_64 aggregate.
- Boot, MM, exceptions, signals, syscalls, scheduling or shared-driver changes also need the x86_64 QEMU boot gate.
- `ir0 arm64 rpi5` proves link only; unsupported stages are SKIP/blocked.
→ rules/ir0-arm64-rpi5-x86-regression-gate.md

### oss-kernel-reference.md
**When it applies:** MM/paging faults, ring-3 bring-up, scheduler or serious design bugs.
- Compare ordering and invariants with Linux/BSD (local `~/Escritorio/linux` first); never paste code; name consulted symbols in the summary.
- Full PFN mask, never descend leaf/huge entries, no NX on table levels, load process CR3 before first user entry; enqueue a process only when ELF, stack and entry state are ready.
→ rules/oss-kernel-reference.md

# ABI/userspace

### ir0-no-userspace-patches.md
**When it applies:** any urge to change BusyBox, musl, ports, `setup/pid1/` or `ir0-patches/` for ABI behaviour.
- Observe, classify, fix the kernel via facades, verify; no fake success in ports or rootfs.
- ABI gates: `kernel-x64.bin`, `arch-guard`, `tests/host run`, `linux_abi_audit.py --contract`; update contracts.json and ABI board only after PASS.
→ rules/ir0-no-userspace-patches.md

### ir0-userspace-first-linux-abi.md
**When it applies:** syscall, signal, TTY, wait, errno, PTY behaviour.
- Real programs are the spec; assume an IR0 bug. Order: observe, classify, kernel fix, verify, userspace belt only as marked temporary with evidence.
- LINUX-LIKE until `linux-abi-audit-*` passes, then VERIFIED on the ABI board; cite Linux `file:symbol`.
→ rules/ir0-userspace-first-linux-abi.md

### ir0-linux-fork-debug-reference.md
**When it applies:** serious bugs (segfault, #PF cs=8, signals, wait/TTY, uaccess).
- Read the Linux analogue in `$LINUX_TREE` (default `~/Escritorio/linux`) and `git log -S` before patching.
- Before coding state: subsystem and symptom, Linux anchors `path:symbol`, commits consulted, first divergence, one-line fix.
→ rules/ir0-linux-fork-debug-reference.md

### ir0-tier-t0-os-functional.md
**When it applies:** proc/sys registry, devfs, contract tests (T0).
- Serve via the registry or document the legacy fallback; `debug_bins` use syscalls only; no new `strcmp` ladders.
→ rules/ir0-tier-t0-os-functional.md

### ir0-tier-t1-userspace-posix.md
**When it applies:** init, musl, syscall gaps (T1).
- Research musl and Linux ABI first; done = `/sbin/init` boots without debug shell and static musl runs fork+exec; no fake-success stubs.
→ rules/ir0-tier-t1-userspace-posix.md

### ir0-tier-t2-graphics-fullscreen.md
**When it applies:** fb0, events0, mmap clients (T2).
- Check Linux uapi (`fb.h`, `input.h`) before changing layouts; add struct-size tests; no WM, GPU, Wayland/X11.
→ rules/ir0-tier-t2-graphics-fullscreen.md

### ir0-tier-t3-desktop-minimal.md
**When it applies:** desktop planning (T3).
- No WM, compositor or panel in the kernel repo; report T3 as ~15-20% until T1 boot and T2 client proof exist.
→ rules/ir0-tier-t3-desktop-minimal.md

# Process & git

### ir0-git-commit-hygiene.md
**When it applies:** every commit.
- English `<type>(<scope>): summary` (feat, fix, refactor, test, docs, chore, perf); one logical change.
- `git commit -s`; no `Co-authored-by`, `Helped-by`, `Reviewed-by`; pre-push check `git log --format=%B origin/dev..HEAD` and no gitignored artifacts staged.
→ rules/ir0-git-commit-hygiene.md

### ir0-integration-flow.md
**When it applies:** landing finished work.
- `git worktree add ../IR0-wt/<topic> -b feat/<topic> dev`; do not edit a tree while its gates run.
- Rebase on dev, `merge --ff-only`, `arch-guard` and `ctr` on dev, push `origin dev` and `mirror dev`.
- Never push or merge `master`/`stable` unless asked that turn; never force-push dev; stop if remotes disagree.
→ rules/ir0-integration-flow.md

### ir0-development-plan-mode.md
**When it applies:** 2+ subsystems, facade/Kconfig/ABI refactors, or an explicit plan request.
- Explore, scope, written plan (in/out, workstreams, order, done criteria), wait for approval; skip for single-file fixes; close with a wave report.
→ rules/ir0-development-plan-mode.md

### ir0-development-multiagent-format.md
**When it applies:** parallel agents or wave reports.
- 2-4 agents on independent files only; one integration step (order: Kconfig/Makefile, facades, portable, arch, drivers, tests, docs).
- Wave closes with `kernel-x64.bin` + `arch-guard`; report: changes, validation, tiers, P0/P1/P2, next step.
→ rules/ir0-development-multiagent-format.md

### ir0-roadmap-research-multiagent.md
**When it applies:** tier work and ABI or protocol specification.
- One tier at a time; no tier done without runnable proof; fetch a primary source and verify in the tree, record it in the commit.
- Merge gates: `kernel-x64.bin`, `arch-guard`, `build-matrix-min`, `tests/host`.
→ rules/ir0-roadmap-research-multiagent.md

### ir0-version-stamp.mdc
**When it applies:** changing version or banner.
- Keep `includes/ir0/version.h` and the top `Makefile` in lockstep; never invent versions; check `git tag -l 'v0.0.1*'`; bump only for release intent.
→ rules/ir0-version-stamp.mdc

### ir0-workspace.md
**When it applies:** orientation on rules and gates.
- CTR at wave close: `kernel-x64.bin`, `arch-guard`, `build-matrix-min`, `tests/host run`; style is Allman.
→ rules/ir0-workspace.md

# Docs/tooling

### kernel-docs-language-policy.md
**When it applies:** writing docs or comments.
- English; Spanish only under `Documentation/esp/`; never mixed in one document.
→ rules/kernel-docs-language-policy.md

### ir0-mandocs-initiative.md
**When it applies:** `Documentation/mandocs/` or `scripts/build_mandocs.py`.
- `mandocs/en/<slug>.md` plus `esp/` mirror; metadata table, ten sections in order, inline ASCII diagram.
- Register a `MandocChapter`, update both INDEX files, validate with `build_mandocs.py --lang en|es --mandoc-only --no-install`; `stable` only when grep-verified.
→ rules/ir0-mandocs-initiative.md
