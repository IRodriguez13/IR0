#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-only
#
# Create a small standalone FAT16 disk image for smoke-fat16-mount (hdb in QEMU).
# Usage: create_fat16_smoke_disk.sh [OUTPUT.img]

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="${1:-$ROOT/build/fat16_smoke.img}"
SIZE_MB=16
LABEL="IR0FAT16"
PAYLOAD="FAT16-SMOKE-OK"

mkdir -p "$(dirname "$OUT")"

if ! command -v mkfs.vfat >/dev/null 2>&1; then
	echo "create_fat16_smoke_disk.sh: mkfs.vfat not found (install dosfstools)" >&2
	exit 1
fi

echo "  FAT16   creating ${SIZE_MB}MB image: $OUT"
dd if=/dev/zero of="$OUT" bs=1M count="$SIZE_MB" status=none
mkfs.vfat -F 16 -n "$LABEL" "$OUT" >/dev/null

TMPDIR="$(mktemp -d)"
trap 'rm -rf "$TMPDIR"' EXIT
printf '%s\n' "$PAYLOAD" > "$TMPDIR/HELLO.TXT"

if command -v mcopy >/dev/null 2>&1; then
	MTOOLS_SKIP_CHECK=1 mcopy -i "$OUT" "$TMPDIR/HELLO.TXT" ::HELLO.TXT
else
	echo "create_fat16_smoke_disk.sh: mcopy not found (install mtools)" >&2
	exit 1
fi

echo "✓ FAT16 smoke disk ready: $OUT (HELLO.TXT=\"${PAYLOAD}\")"
