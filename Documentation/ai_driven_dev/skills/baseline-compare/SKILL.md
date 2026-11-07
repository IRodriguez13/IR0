---
name: baseline-compare
description: Run the same IR0 gate on the unpatched kernel and on the patched tree in separate throwaway trees, report RED->GREEN or GREEN->GREEN, and clean up internal branches after merge and push. Use for every kernel bug fix (red/green proof), every refactor or file split (no regression), and when finishing a series.
---

# baseline-compare — unpatched vs patched kernel

Rule: `Documentation/ai_driven_dev/rules/ir0-baseline-branches.md`.
Proof requirements: `Documentation/ai_driven_dev/rules/ir0-bugfix-red-green-proof.md`.

## 1. Pick the baseline and the gate

| Change | Baseline ref | Expect | Gate |
|--------|--------------|--------|------|
| Bug fix | parent of the fix (`dev`, `HEAD~1`) | `red-green` | the smallest test that encodes the bug |
| Refactor / move / split | `dev` | `green-green` | `kernel-x64.bin`, `ir0 arm64`, `arch-guard`, area tests |
| ISA-specific fix | parent | `red-green` | that ISA's selftest or smoke |

The test must exist in the patched tree. If the test is new, the baseline
run fails because the target is missing: that is not a red proof. Commit or
copy the test into the baseline first (`--keep`, then add it), or pin a
baseline ref that already has the test.

## 2. Run it

```bash
# Bug fix (patched = current working tree, including uncommitted edits)
scripts/baseline_compare.sh dev -- -s ir0-selftest-fault-arm64

# Refactor, both trees must pass
scripts/baseline_compare.sh --expect green-green dev -- -s kernel-x64.bin

# Compare two commits and keep the logs
scripts/baseline_compare.sh --keep 6f75a44c 9e3e3e35 -- -s ir0 arm64
```

Output is three lines (`base`, `patched`, `result`) plus the log paths.
Exit status 0 means the expectation held. Long QEMU targets can take
minutes: run them in the background and wait for completion.

## 3. Record the evidence

Put the `base` and `patched` lines (ref, rc, failure signature from the
base log) in the commit body. Keep the red log until review.

## 4. After merge and push: delete internal branches

```bash
git fetch origin
git branch --merged dev          # internal branches must appear here
git branch -d test/<topic>-base split/<topic>
git range-diff dev...backup/<topic>-v1   # only then:
git branch -D backup/<topic>-v1
git worktree prune && git worktree list
```

Never push `test/*`, `split/*` or `backup/*`. Delete remote branches only
with the maintainer's OK in that turn.
