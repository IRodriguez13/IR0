#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
#
# Run the same make target on an unpatched baseline and on the patched tree,
# each in its own throwaway worktree, and report both results side by side.
#
#   scripts/baseline_compare.sh [--expect red-green|green-green] [--keep]
#                               BASE_REF [PATCHED_REF] -- TARGET [MAKEVARS...]
#
# BASE_REF     unpatched commit/branch (e.g. dev, HEAD~1, origin/dev).
# PATCHED_REF  commit to test as "patched"; default: the current working tree
#              (tracked and untracked, non-ignored files), so WIP is covered.
# --expect     red-green (default): baseline must fail and patched must pass
#              (bug-fix proof). green-green: both must pass (refactor or
#              regression check).
# --keep       keep the worktrees and logs instead of removing them.
#
# Both trees get the caller's .config and a toolchain/ symlink; nothing in
# the caller's tree is modified. Exit status: 0 when the expectation holds.

set -eu

expect=red-green
keep=0
while [ $# -gt 0 ]; do
	case "$1" in
	--expect) expect=$2; shift 2 ;;
	--keep) keep=1; shift ;;
	--) break ;;
	-*) echo "baseline_compare: unknown option $1" >&2; exit 2 ;;
	*) break ;;
	esac
done

[ $# -ge 1 ] || { echo "usage: $0 BASE_REF [PATCHED_REF] -- TARGET" >&2; exit 2; }
base_ref=$1; shift
patched_ref=
if [ $# -gt 0 ] && [ "$1" != "--" ]; then
	patched_ref=$1; shift
fi
[ $# -gt 0 ] && [ "$1" = "--" ] && shift
[ $# -ge 1 ] || { echo "baseline_compare: missing make target" >&2; exit 2; }

case "$expect" in
red-green|green-green) ;;
*) echo "baseline_compare: --expect must be red-green or green-green" >&2; exit 2 ;;
esac

root=$(cd "$(dirname "$0")/.." && pwd -P)
git -C "$root" rev-parse --verify --quiet "$base_ref^{commit}" >/dev/null ||
	{ echo "baseline_compare: unknown BASE_REF $base_ref" >&2; exit 2; }
work=$(mktemp -d "${TMPDIR:-/tmp}/ir0-baseline.XXXXXX")
main_root=$(dirname "$(git -C "$root" rev-parse --path-format=absolute --git-common-dir)")

cleanup()
{
	if [ "$keep" -eq 0 ]; then
		rm -rf "$work"
	else
		echo "baseline_compare: kept $work"
	fi
}
trap cleanup EXIT HUP INT TERM

prepare()
{
	dir=$1
	if [ -f "$root/.config" ]; then
		cp "$root/.config" "$dir/.config"
	fi
	# A worktree's own toolchain symlink may already be in the copy. A
	# linked worktree has none: use the main checkout's (worktree.mk does
	# the same, but needs git, which an archived tree lacks).
	for cand in "$root/toolchain" "$main_root/toolchain"; do
		if [ -d "$cand" ] && [ ! -e "$dir/toolchain" ]; then
			ln -s "$(cd "$cand" && pwd -P)" "$dir/toolchain"
		fi
	done
}

mkdir -p "$work/base" "$work/patched"
git -C "$root" archive "$base_ref" | tar -x -C "$work/base"
if [ -n "$patched_ref" ]; then
	git -C "$root" archive "$patched_ref" | tar -x -C "$work/patched"
else
	(cd "$root" && git ls-files -z --cached --others --exclude-standard |
		xargs -0 tar -cf - --) | tar -x -C "$work/patched"
fi
prepare "$work/base"
prepare "$work/patched"

# Host tests find the sibling ISD checkout relative to the tree
# (tests/host/Makefile: $(KERNEL_ROOT)/../ISD); a throwaway tree has no
# sibling, so pass the caller's ISD explicitly when it exists.
# A linked worktree has no sibling either: fall back to the ISD next to the
# main checkout (git common dir).
isd_root=${IR0_ISD_ROOT:-}
for cand in "$root/../ISD" "$main_root/../ISD"; do
	if [ -z "$isd_root" ] && [ -d "$cand" ]; then
		isd_root=$(cd "$cand" && pwd -P)
	fi
done

run()
{
	name=$1; shift
	if (cd "$work/$name" &&
	    if [ -n "$isd_root" ]; then IR0_ISD_ROOT=$isd_root; export IR0_ISD_ROOT; fi &&
	    make "$@") >"$work/$name.log" 2>&1; then
		echo 0
	else
		echo 1
	fi
}

base_rc=$(run base "$@")
patched_rc=$(run patched "$@")

echo "target:  make $*"
echo "base:    $base_ref rc=$base_rc log=$work/base.log"
echo "patched: ${patched_ref:-working tree} rc=$patched_rc log=$work/patched.log"

if [ "$expect" = red-green ]; then
	[ "$base_rc" -ne 0 ] && [ "$patched_rc" -eq 0 ] && { echo "result:  RED -> GREEN"; exit 0; }
else
	[ "$base_rc" -eq 0 ] && [ "$patched_rc" -eq 0 ] && { echo "result:  GREEN -> GREEN"; exit 0; }
fi
echo "result:  expectation $expect NOT met" >&2
[ "$keep" -eq 1 ] || { keep=1; echo "baseline_compare: logs kept for inspection" >&2; }
exit 1
