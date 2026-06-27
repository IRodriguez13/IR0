#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Run wait4_wnohang_probe on Linux with strace ground truth.

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT="${1:-$ROOT/build/linux_abi_audit/linux/wait4_wnohang}"
OUT="$(mkdir -p "$OUT" && cd "$OUT" && pwd)"
PROBE="${LINUX_ABI_WAIT4_WNOHANG_PROBE:-$ROOT/build/linux_abi_audit/wait4_wnohang_probe}"

mkdir -p "$OUT"

if [[ ! -x "$PROBE" ]]; then
	echo "run_linux_wait4_wnohang.sh: missing $PROBE (run build-linux-abi-wait4-wnohang-probe first)" >&2
	exit 1
fi

STDOUT="$OUT/stdout.log"
STRACE="$OUT/strace.log"

: > "$STDOUT"
: > "$STRACE"

echo "  LINUX_ABI  strace wait4_wnohang_probe (Linux ground truth)"
(
	cd "$OUT"
	strace -f -o "$STRACE" -e trace=fork,wait4,waitpid,read,write,exit,exit_group,nanosleep -s 128 "$PROBE"
) >"$STDOUT" 2>&1 || true

python3 "$ROOT/scripts/linux_abi/parse_wait4_wnohang_trace.py" linux "$STDOUT" "$STRACE" "$OUT/trace.json"

echo "✓ Linux wait4_wnohang workload -> $OUT"
