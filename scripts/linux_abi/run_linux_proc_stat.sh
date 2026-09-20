#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Run proc_stat_probe on Linux (quiet idle accounting ground truth).

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT="${1:-$ROOT/build/linux_abi_audit/linux/proc_stat}"
OUT="$(mkdir -p "$OUT" && cd "$OUT" && pwd)"
PROBE="${LINUX_ABI_PROC_STAT_PROBE:-$ROOT/build/linux_abi_audit/proc_stat_probe}"

mkdir -p "$OUT"

if [[ ! -x "$PROBE" ]]; then
	echo "run_linux_proc_stat.sh: missing $PROBE (make build-linux-abi-proc-stat-probe)" >&2
	exit 1
fi

STDOUT="$OUT/stdout.log"
: > "$STDOUT"

echo "  LINUX_ABI  proc_stat_probe (Linux ground truth)"
"$PROBE" >"$STDOUT" 2>&1 || true

python3 "$ROOT/scripts/linux_abi/parse_simple_trace.py" proc_stat linux "$STDOUT" "$OUT/trace.json"

echo "✓ Linux proc_stat workload -> $OUT"
