#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Run pty_multiplex_probe on Linux with a fresh devpts instance (ground truth).

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT="${1:-$ROOT/build/linux_abi_audit/linux/pty_multiplex}"
OUT="$(mkdir -p "$OUT" && cd "$OUT" && pwd)"
PROBE="${LINUX_ABI_PTY_PROBE:-$ROOT/build/linux_abi_audit/pty_multiplex_probe}"

mkdir -p "$OUT"

if [[ ! -x "$PROBE" ]]; then
	echo "run_linux_pty_multiplex.sh: missing $PROBE" >&2
	exit 1
fi

if ! command -v unshare >/dev/null 2>&1; then
	echo "run_linux_pty_multiplex.sh: unshare required for devpts ground truth" >&2
	exit 1
fi

STDOUT="$OUT/stdout.log"
STRACE="$OUT/strace.log"

: > "$STDOUT"
: > "$STRACE"

echo "  LINUX_ABI  strace pty_multiplex_probe (Linux ground truth, devpts newinstance)"
(
	cd "$OUT"
	unshare --user --map-root-user --mount bash -c "
		mkdir -p /dev/pts
		if ! mountpoint -q /dev/pts 2>/dev/null; then
			mount -t devpts devpts /dev/pts \\
				-o newinstance,ptmxmode=666,mode=620,gid=5 2>/dev/null || \\
			mount -t devpts devpts /dev/pts \\
				-o newinstance,ptmxmode=666,mode=620 2>/dev/null || true
		fi
		if [[ ! -e /dev/ptmx ]]; then
			ln -sf pts/ptmx /dev/ptmx 2>/dev/null || true
		fi
		timeout 45 strace -f -o '$STRACE' -e trace=open,ioctl,read,write,close '$PROBE'
	"
) >"$STDOUT" 2>&1 || true

python3 "$ROOT/scripts/linux_abi/parse_simple_trace.py" \
	"pty_multiplex" linux "$STDOUT" "$OUT/trace.json"

echo "✓ Linux pty_multiplex workload -> $OUT"
