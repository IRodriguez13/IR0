#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Run brk_probe on Linux with strace + /proc/self/maps snapshot.

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT="${1:-$ROOT/build/linux_abi_audit/linux/brk}"
PROBE="${LINUX_ABI_BRK_PROBE:-$ROOT/build/linux_abi_audit/brk_probe}"

mkdir -p "$OUT"

if [[ ! -x "$PROBE" ]]; then
	echo "run_linux_brk.sh: missing $PROBE (run build-linux-abi-brk-probe first)" >&2
	exit 1
fi

STDOUT="$OUT/stdout.log"
STRACE="$OUT/strace.log"
MAPS="$OUT/maps_after_brk.txt"

: > "$STDOUT"
: > "$STRACE"

echo "  LINUX_ABI  strace brk_probe (Linux ground truth)"
(
	cd "$OUT"
	strace -f -o "$STRACE" -e trace=brk -s 128 "$PROBE"
) >"$STDOUT" 2>&1 || true

if [[ -r /proc/self/maps ]]; then
	cp /proc/self/maps "$MAPS" 2>/dev/null || true
fi

python3 "$ROOT/scripts/linux_abi/parse_trace.py" linux "$STDOUT" "$OUT/trace.json"

echo "✓ Linux brk workload -> $OUT"
