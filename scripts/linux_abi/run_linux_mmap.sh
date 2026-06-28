#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Run mmap_probe on Linux with strace ground truth.

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT="${1:-$ROOT/build/linux_abi_audit/linux/mmap}"
OUT="$(mkdir -p "$OUT" && cd "$OUT" && pwd)"
PROBE="${LINUX_ABI_MMAP_PROBE:-$ROOT/build/linux_abi_audit/mmap_probe}"

mkdir -p "$OUT"

if [[ ! -x "$PROBE" ]]; then
	echo "run_linux_mmap.sh: missing $PROBE (run build-linux-abi-mmap-probe first)" >&2
	exit 1
fi

STDOUT="$OUT/stdout.log"
STRACE="$OUT/strace.log"
MAPS="$OUT/maps_after_mmap.txt"

: > "$STDOUT"
: > "$STRACE"

echo "  LINUX_ABI  strace mmap_probe (Linux ground truth)"
(
	cd "$OUT"
	strace -f -o "$STRACE" -e trace=mmap,munmap -s 128 "$PROBE"
) >"$STDOUT" 2>&1 || true

if [[ -r /proc/self/maps ]]; then
	cp /proc/self/maps "$MAPS" 2>/dev/null || true
fi

python3 "$ROOT/scripts/linux_abi/parse_mmap_trace.py" linux "$STDOUT" "$OUT/trace.json"

echo "✓ Linux mmap workload -> $OUT"
