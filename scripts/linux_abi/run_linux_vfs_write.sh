#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT="${1:-$ROOT/build/linux_abi_audit/linux/vfs_write}"
OUT="$(mkdir -p "$OUT" && cd "$OUT" && pwd)"
PROBE="${LINUX_ABI_VFS_WRITE_PROBE:-$ROOT/build/linux_abi_audit/vfs_write_probe}"

mkdir -p "$OUT"

if [[ ! -x "$PROBE" ]]; then
	echo "run_linux_vfs_write.sh: missing $PROBE" >&2
	exit 1
fi

STDOUT="$OUT/stdout.log"
STRACE="$OUT/strace.log"
: > "$STDOUT"
: > "$STRACE"

echo "  LINUX_ABI  strace vfs_write_probe (Linux ground truth)"
(
	cd "$OUT"
	strace -f -o "$STRACE" -e trace=open,openat,creat,close,read,write,lseek,unlink,rename,mkdir,rmdir,truncate,ftruncate,mount "$PROBE"
) >"$STDOUT" 2>&1 || true

python3 "$ROOT/scripts/linux_abi/parse_vfs_write_trace.py" linux "$STDOUT" "$OUT/trace.json"
echo "✓ Linux vfs_write workload -> $OUT"
