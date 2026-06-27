#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Run kill_sigterm_probe on Linux with strace ground truth.

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT="${1:-$ROOT/build/linux_abi_audit/linux/kill_sigterm}"
OUT="$(mkdir -p "$OUT" && cd "$OUT" && pwd)"
PROBE="${LINUX_ABI_KILL_SIGTERM_PROBE:-$ROOT/build/linux_abi_audit/kill_sigterm_probe}"

mkdir -p "$OUT"

if [[ ! -x "$PROBE" ]]; then
	echo "run_linux_kill_sigterm.sh: missing $PROBE (run build-linux-abi-kill-sigterm-probe first)" >&2
	exit 1
fi

STDOUT="$OUT/stdout.log"
STRACE="$OUT/strace.log"

: > "$STDOUT"
: > "$STRACE"

echo "  LINUX_ABI  strace kill_sigterm_probe (Linux ground truth)"
(
	cd "$OUT"
	strace -f -o "$STRACE" -e trace=fork,kill,wait4,waitpid,pause,exit,exit_group -s 128 "$PROBE"
) >"$STDOUT" 2>&1 || true

python3 "$ROOT/scripts/linux_abi/parse_kill_sigterm_trace.py" linux "$STDOUT" "$STRACE" "$OUT/trace.json"

echo "✓ Linux kill_sigterm workload -> $OUT"
