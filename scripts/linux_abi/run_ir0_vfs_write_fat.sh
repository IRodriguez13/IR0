#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# IR0 vfs_write workload on FAT16 (hdb), MINIX root for init only.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT="${1:-$ROOT/build/linux_abi_audit/ir0/vfs_write_fat}"
OUT="$(mkdir -p "$OUT" && cd "$OUT" && pwd)"
PROBE="$ROOT/build/linux_abi_audit/vfs_write_fat_probe"
ISO="${KERNEL_USERSPACE_ISO:-$ROOT/kernel-x64-userspace.iso}"
LOG="$OUT/qemu_serial.log"
SMOKE="$ROOT/scripts/smoke_qemu_run.sh"
QEMU="${QEMU:-qemu-system-x86_64}"
FAT_IMG="$ROOT/build/fat16_vfs_write.img"

mkdir -p "$OUT"

if [[ ! -x "$PROBE" ]]; then
	echo "run_ir0_vfs_write_fat.sh: missing $PROBE" >&2
	exit 1
fi
if [[ ! -f "$ISO" ]]; then
	echo "run_ir0_vfs_write_fat.sh: missing $ISO" >&2
	exit 1
fi
if [[ ! -f "$ROOT/disk.img" ]]; then
	make -s -C "$ROOT" disk.img
fi

chmod +x "$ROOT/scripts/create_fat16_smoke_disk.sh"
"$ROOT/scripts/create_fat16_smoke_disk.sh" "$FAT_IMG"

DISK="$(mktemp /tmp/ir0-linux-abi-vfs-write-fat.XXXXXX.img)"
cp -f "$ROOT/disk.img" "$DISK"
python3 "$ROOT/scripts/inject_init_minix.py" "$DISK" "$PROBE" sbin/init

FAT_RUN="$(mktemp /tmp/ir0-fat-vfs-write.XXXXXX.img)"
cp -f "$FAT_IMG" "$FAT_RUN"

rm -f "$LOG"
echo "  LINUX_ABI  QEMU vfs_write on FAT16 (IR0)"
set +e
bash "$SMOKE" --log "$LOG" --timeout 120 --stale-sec 20 --done 'VFSWRITEOK' -- \
	"$QEMU" -cdrom "$ISO" \
	-drive file="$DISK",format=raw,if=ide,index=0 \
	-drive file="$FAT_RUN",format=raw,if=ide,index=1 \
	-serial stdio -display none -m 256M -no-reboot -net none
rc=$?
set -e
rm -f "$DISK" "$FAT_RUN"

cp "$LOG" "$OUT/stdout.log"
python3 "$ROOT/scripts/linux_abi/parse_vfs_write_trace.py" ir0 "$OUT/stdout.log" "$OUT/trace.json"

flat_log="$(tr -d '\n\r' < "$LOG" 2>/dev/null || true)"
if echo "$flat_log" | grep -q 'VFSWRITEOK'; then
	rc=0
fi

if [[ $rc -ne 0 ]]; then
	echo "✗ IR0 vfs_write FAT workload failed (see $LOG)" >&2
	grep -E 'FAT_VFS|vfs_write|VFSWRITE|errno' "$LOG" | tail -40 >&2 || true
	exit 1
fi

echo "✓ IR0 vfs_write FAT workload -> $OUT"
