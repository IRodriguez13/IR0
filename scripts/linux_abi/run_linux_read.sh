#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Run read_probe on Linux with strace ground truth.

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT="${1:-$ROOT/build/linux_abi_audit/linux/read}"
OUT="$(mkdir -p "$OUT" && cd "$OUT" && pwd)"
PROBE="${LINUX_ABI_READ_PROBE:-$ROOT/build/linux_abi_audit/read_probe}"

mkdir -p "$OUT"

if [[ ! -x "$PROBE" ]]; then
	echo "run_linux_read.sh: missing $PROBE (run build-linux-abi-read-probe first)" >&2
	exit 1
fi

STDOUT="$OUT/stdout.log"
STRACE="$OUT/strace.log"

: > "$STDOUT"
: > "$STRACE"

echo "  LINUX_ABI  strace read_probe (Linux ground truth)"
(
	cd "$OUT"
	strace -f -o "$STRACE" -e trace=read,pipe,write,dup,dup2,close -s 128 "$PROBE"
) >"$STDOUT" 2>&1 || true

python3 "$ROOT/scripts/linux_abi/parse_read_trace.py" linux "$STDOUT" "$OUT/trace.json"

echo "✓ Linux read workload -> $OUT"
