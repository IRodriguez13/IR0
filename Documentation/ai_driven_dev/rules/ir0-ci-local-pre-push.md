<!-- IR0 AI dev rule: ir0-ci-local-pre-push -->
<!-- alwaysApply: true -->
<!-- description: IR0 — gates to run before integrating into dev and pushing (test-fast, ctr, kernel-tests) -->

# IR0 — Pre-push gates

**Goal:** catch failures on the developer machine **before** `git push`, so
the remotes stay green. Pairs with `ir0-integration-flow.md` (where work
lands) and `skills/ctr/SKILL.md` (what CTR checks).

## Pipeline (canonical order)

```text
edit in the feature worktree
    │
    ▼
make -s test-fast        ← every substantial change
    │
    ▼
make -s kernel-tests     ← when the diff touches code that boots
    │
    ▼
make -s ctr              ← before merging into dev, and again on dev
    │
    ▼
git push origin dev && git push mirror dev   (ir0-integration-flow.md)
```

## Targets

| Target | What it proves |
|--------|----------------|
| `make -s test-fast` | `kernel-x64.bin`, `arch-guard`, `tests/host`, selftest TAP and Kconfig host tests, defconfig drift, KTM classify/panic/adoption guards, host portability guard |
| `make -s kernel-tests` | In-kernel battery (`kernel-x64-test.iso`) boots in QEMU and ends with `TEST_END\|kernel.ktest\|PASS` |
| `make -s ctr` | `scripts/ctr.sh`: kernel, `arch-guard`, `build-matrix-min`, `tests/host` (`CTR_KTM=1` adds `kernel-tests`) |
| `make -s agent-fast` | `test-fast` + `kernel-tests` |

Add the area gates the diff needs: `make -s ir0 arm64` and
`make -s ir0 arm64 rpi5` for ARM work (`ir0-arm64-rpi5-x86-regression-gate.md`),
the `ir0-selftest-*` goals for syscall ABI, and `ktm-userdev-cow-run`,
`ktm-userdev-fork-storm-run`, `smoke-mm-cow-lazy` for fork/COW/MM
(`ir0-ktm-first-validation.md`). Every QEMU-booting goal takes a private
`HOST_TMP=<dir>` when other agents share the host.

## Agent duties (before proposing push)

1. Run at least **`make -s test-fast`** for the touched scope, and
   **`make -s ctr`** before integrating into `dev`.
2. Report a pass/fail table with the commands **executed**, not assumed.
3. Push only as `ir0-integration-flow.md` allows.

## Removed targets

`ci-local`, `ci-local-fast`, `ci-local-boot`, `ci-local-docker`,
`ci-local-rc`, `tooling-check`, `release-check-boot*`, `fresh-clone-check`
and `pre-submit` were removed with the QA catalog (2480a5dd). Never report
them as run; `scripts/ci_local.sh` still names them but has no make goal.

## Anti-patterns

- Push without `test-fast` on multi-file or build-touching diffs.
- Treat a green `test-fast` as boot proof when the diff changes code that
  only runs in the guest; run `kernel-tests` or the area smoke.
- Running QEMU goals in parallel, or without `HOST_TMP`, next to other
  agents.
