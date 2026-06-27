#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Run process_lifecycle_probe on Linux (strace ground truth).

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT="${1:-$ROOT/build/linux_abi_audit/linux/process_lifecycle}"
OUT="$(mkdir -p "$OUT" && cd "$OUT" && pwd)"
PROBE="${LINUX_ABI_PROCESS_LIFECYCLE_PROBE:-$ROOT/build/linux_abi_audit/process_lifecycle_probe}"
HELPER="${LINUX_ABI_EXEC_HELPER:-$ROOT/build/linux_abi_audit/exec_helper}"

mkdir -p "$OUT"

if [[ ! -x "$PROBE" ]]; then
	echo "run_linux_process_lifecycle.sh: missing $PROBE (build-linux-abi-process-lifecycle-probe)" >&2
	exit 1
fi
if [[ ! -x "$HELPER" ]]; then
	echo "run_linux_process_lifecycle.sh: missing $HELPER" >&2
	exit 1
fi

STDOUT="$OUT/stdout.log"
STRACE="$OUT/strace.log"

: > "$STDOUT"
: > "$STRACE"

echo "  LINUX_ABI  strace process_lifecycle_probe (Linux ground truth)"
(
	cd "$OUT"
	strace -f -o "$STRACE" \
		-e trace=fork,wait4,waitpid,execve,exit,exit_group,kill,rt_sigaction \
		-s 128 "$PROBE"
) >"$STDOUT" 2>&1 || true

python3 "$ROOT/scripts/linux_abi/parse_process_lifecycle_trace.py" linux "$STDOUT" "$OUT/trace.json"

if ! grep -q 'PROC_LIFECYCLEOK' "$STDOUT"; then
	echo "✗ Linux process_lifecycle_probe did not emit PROC_LIFECYCLEOK (see $STDOUT)" >&2
	exit 1
fi

echo "✓ Linux process_lifecycle workload -> $OUT"
