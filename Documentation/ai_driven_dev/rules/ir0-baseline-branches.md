<!-- IR0 AI dev rule: ir0-baseline-branches -->
<!-- alwaysApply: true -->
<!-- description: Test every kernel patch against the unpatched kernel in separate throwaway trees, and delete internal branches once merged and pushed -->

# IR0 — Baseline trees and internal branch lifecycle

Complements `ir0-bugfix-red-green-proof.md` (what to prove),
`ir0-audit-baseline-worktree.md` (audits against a stable ref) and
`ir0-git-commit-hygiene.md` (how to commit). This rule says **where** the
unpatched and patched runs happen and **when** internal branches go away.

## Compare against the unpatched kernel in separate trees

- Never revert the fix in your working tree to get the red run. Build the
  baseline in its own throwaway tree from the unpatched ref and the patched
  candidate in another, then run the **same** make target in both.
- Use `scripts/baseline_compare.sh` (skill `baseline-compare`):

  ```bash
  scripts/baseline_compare.sh dev -- -s ir0-selftest-fault-arm64          # bug fix: RED -> GREEN
  scripts/baseline_compare.sh --expect green-green dev -- -s ctr          # refactor: GREEN -> GREEN
  ```

  The patched side defaults to the current working tree (tracked and
  untracked, non-ignored files), so uncommitted work is covered. Both trees
  get the caller's `.config` and `toolchain/` link; the caller's tree is not
  touched.
- Bug fixes expect `red-green`. Refactors, moves and splits expect
  `green-green` on the gates that cover the touched code (at minimum
  `kernel-x64.bin`, `ir0 arm64`, `arch-guard`, and the selftests/ktests of
  the area).
- Keep the logs (`--keep`) of the red run until the change is reviewed and
  cite the result line in the commit body.
- x86-64 and ARM64 share object paths until the per-ISA build layout lands:
  never run an x86 and an ARM64 build in the same tree without `make clean`.
  Separate throwaway trees avoid that entirely.

## Internal branches

Internal branches are local helpers that never reach `origin`:

| Prefix | Use |
|--------|-----|
| `test/<topic>-base`, `test/<topic>-patched` | Pinned refs for a comparison you need to rerun |
| `backup/<topic>-vN` | Safety copy before rewriting an unpublished series |
| `split/<topic>` | Scratch branch while splitting a series into atomic commits |

The feature branch itself (`feat/…`, `fix/…`) follows
`ir0-git-feature-branch-mr.md`.

## Delete internal branches after merge and push

Once the feature branch is merged into the integration branch (`dev`)
**and** that branch is pushed with the maintainer's OK:

1. Confirm every internal branch is contained:
   `git branch --merged dev` must list it, or its commits must be in
   `git log dev` (a rewritten series may differ only by rebase).
2. Delete with `git branch -d` (refuses unmerged work). Use `-D` only for a
   `backup/*` copy of a series that was rewritten and then merged, after
   checking with `git range-diff`.
3. Remove leftover worktrees: `git worktree prune`, then
   `git worktree list` must show only the main tree.
4. Delete the merged feature branch locally; delete it on the remote only
   with the maintainer's OK in that turn.

Never delete a branch whose commits are not reachable from a pushed branch,
and never push internal branches.

## Anti-patterns

- Reverting the fix in place to get a red log, then forgetting to restore it.
- Comparing a baseline built with a different `.config` or ISA.
- Leaving `backup/*`, `split/*` or `test/*` branches and stale worktrees after
  the series is merged and pushed.
- Deleting a branch with `-D` without checking that its work is merged.
