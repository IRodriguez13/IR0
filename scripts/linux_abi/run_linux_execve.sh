#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Run execve_probe on Linux with strace ground truth.

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT="${1:-$ROOT/build/linux_abi_audit/linux/execve}"
OUT="$(mkdir -p "$OUT" && cd "$OUT" && pwd)"
HELPER_SRC="${LINUX_ABI_EXEC_HELPER:-$ROOT/build/linux_abi_audit/exec_helper}"
SRC_PROBE="$ROOT/scripts/linux_abi/workloads/execve_probe.c"
HELPER="$OUT/exec_helper"
PROBE="$OUT/execve_probe"

mkdir -p "$OUT"

if [[ ! -x "$HELPER_SRC" ]]; then
	echo "run_linux_execve.sh: missing $HELPER_SRC (run build-linux-abi-execve-probe first)" >&2
	exit 1
fi

if command -v musl-gcc >/dev/null 2>&1; then
	CC=musl-gcc
else
	CC=gcc
fi

cp -f "$HELPER_SRC" "$HELPER"
chmod +x "$HELPER"
"$CC" -static -Os -DEXEC_HELPER_PATH=\"$HELPER\" -o "$PROBE" "$SRC_PROBE"

STDOUT="$OUT/stdout.log"
STRACE="$OUT/strace.log"

: > "$STDOUT"
: > "$STRACE"

echo "  LINUX_ABI  strace execve_probe (Linux ground truth)"
(
	cd "$OUT"
	strace -f -o "$STRACE" -e trace=execve,fork,wait4,waitpid,exit_group,exit "$PROBE"
) >"$STDOUT" 2>&1 || true

python3 "$ROOT/scripts/linux_abi/parse_execve_trace.py" linux "$STDOUT" "$OUT/trace.json"

echo "✓ Linux execve workload -> $OUT"
