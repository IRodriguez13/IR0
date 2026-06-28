#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Run mount_probe on Linux inside user+mount namespace (tmpfs ground truth).

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT="${1:-$ROOT/build/linux_abi_audit/linux/mount}"
OUT="$(mkdir -p "$OUT" && cd "$OUT" && pwd)"
PROBE="${LINUX_ABI_MOUNT_PROBE:-$ROOT/build/linux_abi_audit/mount_probe}"

mkdir -p "$OUT"

if [[ ! -x "$PROBE" ]]; then
	echo "run_linux_mount.sh: missing $PROBE (run build-linux-abi-mount-probe first)" >&2
	exit 1
fi

if ! command -v unshare >/dev/null 2>&1; then
	echo "run_linux_mount.sh: unshare required for Linux tmpfs mount ground truth" >&2
	exit 1
fi

STDOUT="$OUT/stdout.log"
STRACE="$OUT/strace.log"

: > "$STDOUT"
: > "$STRACE"

echo "  LINUX_ABI  strace mount_probe (Linux ground truth, unshare mount ns)"
(
	cd "$OUT"
	unshare --user --map-root-user --mount bash -c "
		mount -t proc proc /proc 2>/dev/null || true
		strace -f -o '$STRACE' -e trace=mount,umount2,umount,mkdir,open,read,write,close '$PROBE'
	"
) >"$STDOUT" 2>&1 || true

python3 "$ROOT/scripts/linux_abi/parse_mount_trace.py" linux "$STDOUT" "$OUT/trace.json"

echo "✓ Linux mount workload -> $OUT"
