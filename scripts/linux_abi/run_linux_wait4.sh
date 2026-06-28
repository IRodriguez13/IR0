#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Run wait4_probe on Linux with strace ground truth.

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT="${1:-$ROOT/build/linux_abi_audit/linux/wait4}"
OUT="$(mkdir -p "$OUT" && cd "$OUT" && pwd)"
PROBE="${LINUX_ABI_WAIT4_PROBE:-$ROOT/build/linux_abi_audit/wait4_probe}"

mkdir -p "$OUT"

if [[ ! -x "$PROBE" ]]; then
	echo "run_linux_wait4.sh: missing $PROBE (run build-linux-abi-wait4-probe first)" >&2
	exit 1
fi

STDOUT="$OUT/stdout.log"
STRACE="$OUT/strace.log"

: > "$STDOUT"
: > "$STRACE"

echo "  LINUX_ABI  strace wait4_probe (Linux ground truth)"
(
	cd "$OUT"
	strace -f -o "$STRACE" -e trace=fork,wait4,waitpid,exit,exit_group -s 128 "$PROBE"
) >"$STDOUT" 2>&1 || true

python3 "$ROOT/scripts/linux_abi/parse_wait4_trace.py" linux "$STDOUT" "$OUT/trace.json"

echo "✓ Linux wait4 workload -> $OUT"
