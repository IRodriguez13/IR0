<!-- IR0 AI dev rule: ir0-ktm-first-validation -->
<!-- alwaysApply: true -->
<!-- description: IR0 kernel — prefer built-in KTM when it adds validation value -->

# IR0 — KTM as preferred builtin tooling

**Preference, not a hard gate:** use KTM (in-kernel scenarios, `kernel-tests`,
the KTM userdev pilots, selftests) when it exercises **real guest behavior**
better than host stubs. Skip or extend KTM only when it clearly does not pay
off.

## Default validation stack

```bash
make -s kernel-x64.bin
make -s arch-guard
make -s -C tests/host run          # fast contracts; not a substitute for guest lifecycle
make -s kernel-tests               # in-kernel battery at boot — use when proc/sched/MM touched
make -s test-fast                  # arch-guard + host contracts + KTM guards
```

Add area targets **when the diff touches that subsystem** (see table below).
Do not run every guest battery for a one-line fix. QEMU goals take a private
`HOST_TMP=<dir>` when other agents share the host.

## When KTM tends to pay off

| Area | Prefer |
|------|--------|
| COW / fork MM | `smoke-mm-cow-lazy`, `ktm-userdev-cow-run`, `ktm-userdev-fork-storm-run` |
| signals / faults / syscall ABI | `ir0-selftest-*` (x86 and arm64), ktest cases |
| pseudo-fs / proc ABI | **ktest procfs** for format, `tests/host` procfs ABI tests |
| Host-only math/flags | `tests/host` is enough — no mandatory QEMU |

The other `tests/ktm/userdev/*` cases (pipe fault injection, exec drain,
epoll, pseudo-fs, …) have no make goal since the QA catalog was removed
(2480a5dd). Run them by hand with `scripts/ktm_userdev_runner.py`, or add a
goal to `scripts/make/ktm-userdev.mk` once the case passes on the current
tree.

## Improving KTM

If a gap blocks honest validation (e.g. `/proc/stat` counters), add a **small
ktest** or userdev probe rather than duplicating logic only in host stubs.
Keep new scenarios focused; do not expand KTM for coverage theater.

## Relation

- Pre-push order: `ir0-ci-local-pre-push.md`
- CTR: `skills/ctr/SKILL.md`
