#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
#
# Keep the GitLab mirror aligned with GitHub (source of truth).
#
# Usage:
#   ./scripts/sync_gitlab_mirror.sh           # push master/stable/dev + tags (FF)
#   ./scripts/sync_gitlab_mirror.sh --force   # allow non-FF update (one-shot realign)
#
# Remotes expected:
#   origin  → git@github.com:IRodriguez13/IR0.git
#   mirror  → git@gitlab.com:IvanR013/IR0.git

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

FORCE=0
if [[ "${1:-}" == "--force" ]]; then
	FORCE=1
fi

if ! git remote get-url origin >/dev/null 2>&1; then
	echo "✗ remote 'origin' missing" >&2
	exit 1
fi
if ! git remote get-url mirror >/dev/null 2>&1; then
	echo "✗ remote 'mirror' missing (git remote add mirror git@gitlab.com:IvanR013/IR0.git)" >&2
	exit 1
fi

echo "  FETCH   origin + mirror"
git fetch origin --prune
# Local tag objects may diverge from GitHub; do not fail the sync over that.
git fetch origin --tags --force 2>/dev/null || true
git fetch mirror --prune 2>/dev/null || true

BRANCHES=(master stable dev)
if [[ "$FORCE" -eq 1 ]]; then
	echo "  MODE    --force (realign divergent history; Maintainers only)"
else
	echo "  MODE    fast-forward only"
fi

for b in "${BRANCHES[@]}"; do
	if ! git show-ref --verify --quiet "refs/remotes/origin/${b}"; then
		echo "  SKIP    origin/${b} (missing)"
		continue
	fi
	echo "  PUSH    origin/${b} → mirror/${b}"
	if [[ "$FORCE" -eq 1 ]]; then
		git push --force mirror "refs/remotes/origin/${b}:refs/heads/${b}"
	else
		git push mirror "refs/remotes/origin/${b}:refs/heads/${b}"
	fi
done

echo "  PUSH    tags → mirror"
# Skip tags that already exist with a different object on the mirror.
git push mirror --tags 2>&1 | rg -v 'already exists' || true

echo "  CHECK   heads origin vs mirror"
mapfile -t origin_heads < <(git ls-remote --heads origin | awk '{print $2" "$1}' | sort)
mapfile -t mirror_heads < <(git ls-remote --heads mirror | awk '{print $2" "$1}' | sort)

drift=0
for line in "${origin_heads[@]}"; do
	ref="${line%% *}"
	sha="${line##* }"
	mline="$(printf '%s\n' "${mirror_heads[@]}" | awk -v r="$ref" '$1==r {print; exit}')"
	if [[ -z "$mline" ]]; then
		echo "  DRIFT   ${ref} missing on mirror"
		drift=1
		continue
	fi
	msha="${mline##* }"
	if [[ "$sha" != "$msha" ]]; then
		echo "  DRIFT   ${ref}: origin=${sha:0:8} mirror=${msha:0:8}"
		drift=1
	else
		echo "  OK      ${ref} @ ${sha:0:8}"
	fi
done

# Extra heads only on mirror (should not exist for a pure mirror).
while read -r mref msha; do
	[[ -z "$mref" ]] && continue
	if ! printf '%s\n' "${origin_heads[@]}" | awk -v r="$mref" '$1==r {found=1} END{exit !found}'; then
		echo "  EXTRA   mirror-only ${mref} @ ${msha:0:8} (delete if junk)"
		drift=1
	fi
done < <(git ls-remote --heads mirror | awk '{print $2" "$1}' | sort)

if [[ "$drift" -ne 0 ]]; then
	echo "✗ mirror not fully aligned — re-run with --force after allowing force-push on protected branches" >&2
	exit 1
fi

echo "✓ GitLab mirror aligned with GitHub (master/stable/dev + tags)"
