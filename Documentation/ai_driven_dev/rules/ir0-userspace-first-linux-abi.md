<!-- IR0 AI dev rule: ir0-userspace-first-linux-abi -->
<!-- alwaysApply: true -->
<!-- description: Userspace-first — IR0 adapts to Linux/musl/BusyBox/GNU; never the reverse -->

# IR0 — Userspace-first Linux ABI (mandatory)

**IR0 adapts to what programs need.** BusyBox, bash, musl, GNU coreutils, and
future third-party binaries are the specification for **observable** behavior.
The kernel implements Linux + man-pages + musl semantics; userspace is not
required to carry IR0-specific patches to be "correct."

This rule **extends** (does not replace) `kernel-userspace-abi.md`,
`kernel-maintainer/linux-first-workflow.mdc`, and `ir0-contract-iteration-states.md`.

## North star

> **Don't break userspace.** (Linux)

When IR0 and a real program disagree, default assumption: **IR0 bug** until
classified otherwise in `kernel-maintainer/divergence-taxonomy.mdc`.

## Fix order (never reverse)

1. **Observe** — strace/LTP on Linux, or `scripts/linux_abi/` workload + compare.
2. **Classify** — bug | acceptable-diff | missing-feature | harness | instrumentation.
3. **Fix kernel** — minimal change matching Linux observable (ret, errno, ordering, TTY state).
4. **Verify** — `tests/host`, ktest, `linux-abi-audit-*`, tier smoke as applicable.
5. **Userspace patch** — only if upstream bug *and* kernel cannot honestly implement
   Linux semantics yet; mark **temporary belt** in commit/ABI board; no "IR0-only" API.

**Hard stop:** before proposing or merging any BusyBox/ash/GNU/musl patch for signal,
TTY, read/EINTR, or wait/status behavior — confirm steps 1–4 are done or document
why kernel fix is blocked (`kernel-maintainer/divergence-taxonomy.mdc`).

**Forbidden:** closing a console/signal/TTY bug with a BusyBox-only patch while the
kernel still returns wrong errno, corrupts syscall GPRs on `sigreturn`, or spurious EOF.

## Observable contract areas (Linux parity)

| Area | Programs assume | Kernel owns |
|------|-----------------|-------------|
| Syscall return | `rax` = result or `-errno`; args preserved on `-EINTR` | `process_syscalls.c`, dispatch |
| `read(2)` + signal | `-EINTR` without EOF; no handler GPRs in resume frame | `sys_sigreturn`, `kernel_sleep_syscall_frame` |
| SA_RESTART | Restart blocked syscall when `sa_flags` says so | `signal_sa_flags`, sigreturn restart path |
| TTY / console | VINTR, cooked/raw, `TCFLSH`, no sticky PS/2 mods | `kernel/lib/console.c`, `keyboard.c` |
| Abandoned sigframe | ash `longjmp` after ^C — no `rt_sigreturn` | `signals_try_abandon_sigframe` + TTY hygiene |
| SIGCHLD + lineedit | `-EINTR` or restart — **not** stdin EOF / logout | Same as read+signal; no `read=0` race |
| PTY multiplex | `/dev/ptmx` + `/dev/pts/N`, `TIOCGPTN`, dual xterm | `fs/devfs.c` + `includes/ir0/pty_devfs.h` |

Reference anchors: Linux `kernel/signal/`, `drivers/tty/pty.c`, `drivers/tty/n_tty.c`,
man7 `signal(7)`, `pty(7)`, `termios(3)`, musl `src/signal/`, BusyBox `shell/ash.c`
(read only — do not fork behavior).

## ISD / BusyBox patches

| Patch class | Role |
|-------------|------|
| **0001–0004** | Features / hardening (tab completion, ESC clamp) — not ABI spec |
| **0005–0009** | **Retired** (2026-09-05) — were temporary belts for signal/TTY; kernel `sigreturn_blocked_syscall` VERIFIED + smokes green without them → `ir0-patches/retired/` |
| **Goal** | Remove 0006–0009 when `linux-abi-audit` + smokes green **without** them |

Before adding a new `ir0-patches/*` ash/login patch for ABI-like behavior: **stop** —
implement Linux semantics in kernel first; prove with smoke + compare.

## Contract registry (0.0.1)

| Contract id | Area | Host / smoke gate |
|-------------|------|-------------------|
| `sigreturn_blocked_syscall` | rt_sigreturn after TTY/pipe block | `test_sigreturn_sleep_eintr_frame_abi` + console smokes |
| `pty_multiplex` | Two ptmx masters + pts I/O | `linux-abi-audit-pty-multiplex` + `smoke-ext2-startx` |

Add rows to `scripts/linux_abi/contracts.json` when a new observable area gets a gate.
State LINUX-LIKE until `linux-abi-audit-*` PASS; then VERIFIED on ABI board.

## Verification gates (ABI-touching changes)

Minimum before claiming fixed:

```bash
make -s kernel-x64.bin
make -s -C tests/host run
# When contract exists:
IR0_INCLUDE_QA=1 make linux-abi-audit-<contract>
# Console/signal oleada:
make -s smoke-ctrl-c-spam smoke-sigchld-no-false-logout smoke-sigchld-sysfs-stress
```

Update `Documentation/releases/IR0_0.0.1_ABI_BOARD.md` when contract state changes.

## Agent checklist (every ABI/signal/TTY task)

- [ ] Linux reference cited (`file:symbol` or man section)
- [ ] First divergence written (step, errno, ret)
- [ ] Fix is kernel-side unless classified acceptable-diff
- [ ] No new userspace patch unless kernel fix blocked with evidence
- [ ] Host test or audit for non-obvious resume/EINTR/TTY rule
- [ ] Smokes listed above if console/signals touched

## Anti-patterns

- "Fix ash so it tolerates our kernel."
- Merging handler `sigcontext` GPRs into blocked-syscall resume (`rdi=signum` → `#PF`).
- `read=0` on pending signal (VMIN=0 path) without `-EINTR`.
- Marking VERIFIED without `linux-abi-audit` or runnable compare.
- Documenting SA_RESTART as "not implemented" while code paths exist — verify in tree.
