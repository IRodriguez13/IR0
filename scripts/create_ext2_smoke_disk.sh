#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-only
# Create a small EXT2 disk image for smoke-ext2-mount (hdb).
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="${1:-$ROOT/build/ext2_smoke.img}"
SIZE_MB=8
PAYLOAD="EXT2-SMOKE-OK"

mkdir -p "$(dirname "$OUT")"

if ! command -v mkfs.ext2 >/dev/null 2>&1; then
	echo "create_ext2_smoke_disk.sh: mkfs.ext2 not found (install e2fsprogs)" >&2
	exit 1
fi

echo "  EXT2    creating ${SIZE_MB}MB image: $OUT"
dd if=/dev/zero of="$OUT" bs=1M count="$SIZE_MB" status=none
mkfs.ext2 -b 1024 -F -L IR0EXT2 "$OUT" >/dev/null

TMPDIR="$(mktemp -d)"
trap 'rm -rf "$TMPDIR"' EXIT
printf '%s\n' "$PAYLOAD" > "$TMPDIR/HELLO.TXT"

if command -v debugfs >/dev/null 2>&1; then
	debugfs -w -R "write $TMPDIR/HELLO.TXT HELLO.TXT" "$OUT" >/dev/null
else
	echo "create_ext2_smoke_disk.sh: debugfs not found (install e2fsprogs)" >&2
	exit 1
fi

echo "✓ EXT2 smoke disk ready: $OUT (HELLO.TXT=\"${PAYLOAD}\")"
