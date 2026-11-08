<!-- IR0 AI dev rule: ir0-integration-flow -->
<!-- alwaysApply: true -->
<!-- description: One worktree per feature; finished work merges into dev and dev is pushed to GitHub and GitLab; only the maintainer merges to master -->

# IR0 — Parallel features and integration into dev

Complements `ir0-baseline-branches.md` (red/green trees, internal branch
cleanup) and `ir0-git-commit-hygiene.md` (how to commit). This rule says
**where** each feature lives and **how** finished work reaches the remotes.
For IR0 it replaces the generic per-turn push approval: the maintainer has
granted a standing OK for the `dev` pushes described here.

## One worktree per feature

- Two features never share a working tree. Each `feat/<topic>` or
  `fix/<topic>` gets its own worktree:

  ```bash
  git worktree add ../IR0-wt/<topic> -b feat/<topic> dev
  ```

- Objects are built next to the sources (until the per-ISA build layout
  lands), so a tree must never mix the builds of two features or of two
  ISAs without `make clean`.
- `scripts/baseline_compare.sh` snapshots the working tree at the start of
  **each** run. While gates run in a tree, do not edit that tree; keep
  working on the next feature in its own worktree.
- A feature that depends on another branches from it and is rebased onto
  `dev` once the first one lands.

## Integrate finished work into dev

When a feature's commits are atomic, proven (red/green or green/green as
`ir0-bugfix-red-green-proof.md` requires) and its gates pass:

1. `git fetch origin mirror` and rebase the feature onto `dev`; rerun the
   touched gates if the rebase changed anything.
2. Merge into `dev` locally: `git switch dev && git merge --ff-only feat/<topic>`
   (use `--no-ff` only when a merge commit documents a multi-feature series).
3. On `dev`: `make -s arch-guard` and `make -s ctr` must pass.
4. Push `dev` to **both** remotes:

   ```bash
   git push origin dev   # GitHub
   git push mirror dev   # GitLab
   ```

5. Clean up per `ir0-baseline-branches.md`: delete the merged feature branch
   and its worktree (`git worktree remove`, `git worktree prune`).

Feature and internal branches stay local; they are not pushed.

## master belongs to the maintainer

- Never merge into or push `master` (or `stable`) unless the maintainer asks
  for it in that turn. The maintainer merges `dev` into `master`.
- Never force-push `dev`. If a remote `dev` moved, fetch, rebase the local
  work on top and push again.
- If the two remotes disagree on `dev`, stop and report; do not pick one by
  force.

## Anti-patterns

- Implementing a second feature in the tree whose gates are still running.
- Pushing `dev` to only one remote.
- Leaving finished, proven commits only on a local feature branch.
- Merging to `master` "because dev was green".
