#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Resolve ISD checkout path: IR0_ISD_ROOT or sibling ../ISD only (no heuristics).
set -euo pipefail

usage() {
	echo "Usage: resolve_isd_root.sh [--require] [IR0_REPO_ROOT]" >&2
	echo "  Prints absolute ISD path on success." >&2
	echo "  Exit 1 when missing; with --require also prints Set IR0_ISD_ROOT= hint." >&2
}

require=0
if [ "${1:-}" = "--require" ]; then
	require=1
	shift
fi

if [ $# -gt 1 ]; then
	usage
	exit 2
fi

if [ $# -eq 1 ]; then
	IR0_ROOT="$(cd "$1" && pwd)"
else
	IR0_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
fi

if [ -n "${IR0_ISD_ROOT:-}" ]; then
	candidate="$(cd "${IR0_ISD_ROOT}" 2>/dev/null && pwd || true)"
else
	candidate="$(cd "${IR0_ROOT}/../ISD" 2>/dev/null && pwd || true)"
fi

if [ -n "$candidate" ] && [ -f "${candidate}/Makefile" ]; then
	printf '%s\n' "$candidate"
	exit 0
fi

echo "✗ ISD not found" >&2
if [ -n "${IR0_ISD_ROOT:-}" ]; then
	echo "  IR0_ISD_ROOT=${IR0_ISD_ROOT} (no Makefile)" >&2
else
	echo "  expected sibling: ${IR0_ROOT}/../ISD" >&2
fi
echo "  Set IR0_ISD_ROOT=/path/to/ISD" >&2
echo "  Supported layout: workspace/IR0 + workspace/ISD" >&2
exit 1
