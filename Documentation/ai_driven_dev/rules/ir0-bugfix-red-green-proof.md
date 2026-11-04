<!-- IR0 AI dev rule: ir0-bugfix-red-green-proof -->
<!-- alwaysApply: true -->
<!-- description: Every kernel bug fix must reproduce the bug on the unpatched tree and pass the same test on the patched tree -->

# IR0 — Bug-fix red/green proof

Applies to every change presented as a **kernel bug fix**: `kernel/`, `mm/`,
`fs/`, `net/`, `ipc/`, `sched/`, `drivers/`, `arch/`, `includes/`, and syscall
ABI behavior.

## Non-negotiable rule

A bug fix is accepted only when the **same test** proves both states:

1. **Red — unpatched:** the test fails for the expected reason on the tree
   without the fix (current `HEAD`, or the working tree with only the fix
   reverted).
2. **Green — patched:** the same command passes with the fix applied.
3. **Red again:** reverting the fix once more makes the test fail again, so the
   test is tied to the fix and was not weakened.

A code review, a "looks fixed" argument, or a test written after the fix
without a red run is **not** proof. No red log, no fix.

## Required evidence

Record this in the commit body, PR, or fix note:

- Baseline state: `git rev-parse HEAD`, plus the reverted files or patch.
- Baseline command and the observed failure signature (errno, host assertion,
  serial/KTM marker such as `TEST_END|kernel.ktest|FAIL` or a panic tag).
- Fixed command and the passing signature.
- Completion gates actually run (`make test-fast`, `make kernel-tests` when the
  area boots, plus any tier smoke).
- Log path or the relevant excerpt.

Keep the failing and passing logs until the change is reviewed.

## Workflow

1. Write the **smallest deterministic test** that encodes the bug contract:
   - host unit test under `tests/host/`, or
   - in-kernel case under `kernel/test/test_*.c` registered in
     `kernel/test/test_runner.c`, or
   - QEMU smoke/harness when the bug needs a real boot or userspace.
2. Run the test on the **unpatched baseline first** and save the failing log.
   If the fix is already in the working tree, isolate the baseline without
   losing work:
   ```bash
   git stash push -- <fix files>     # keep the new test in the tree
   # or
   git worktree add /tmp/ir0-baseline HEAD
   ```
3. Apply the fix and run **exactly the same command**. Save the passing log.
4. Revert the fix once and confirm the same failure returns; restore the fix.
5. Run the completion gates from `skills/ctr/SKILL.md`.

## Test rules

- Assert the observable contract: return value, `errno`, side effects,
  ordering, VMA/fd/wait state, bytes preserved. "Did not panic" is not enough.
- One test per bug. Never make an existing test pass by loosening or deleting
  it.
- The baseline and the fixed run must use the same config, arch, and command.
- If the bug is ARM64/hardware-only, run both phases on that target. If the
  target is unavailable, state that explicitly and provide the closest
  host/unit proof; never claim guest proof without a boot.
- Do not patch third-party userspace or the toolchain to hide a kernel bug
  (`ir0-no-userspace-patches.md`).
- Flaky tests must be repeated enough to show the bug is deterministic; do not
  accept a single lucky pass.

## Anti-patterns

- Test added only after the fix, with no unpatched run.
- Baseline measured on a different config/arch than the fix.
- Weakening the assertion until both trees pass.
- Fixing the test instead of the kernel.
- Reporting "fixed" without the red and green commands in the evidence.
