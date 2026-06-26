#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Run the same mmap_probe ELF under IR0 (QEMU userspace ISO + injected init).

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT="${1:-$ROOT/build/linux_abi_audit/ir0/mmap}"
OUT="$(mkdir -p "$OUT" && cd "$OUT" && pwd)"
PROBE="$ROOT/build/linux_abi_audit/mmap_probe"
ISO="${KERNEL_USERSPACE_ISO:-$ROOT/kernel-x64-userspace.iso}"
LOG="$OUT/qemu_serial.log"
SMOKE="$ROOT/scripts/smoke_qemu_run.sh"
QEMU="${QEMU:-qemu-system-x86_64}"

mkdir -p "$OUT"

if [[ ! -x "$PROBE" ]]; then
	echo "run_ir0_mmap.sh: missing $PROBE (run build-linux-abi-mmap-probe first)" >&2
	exit 1
fi

if [[ ! -f "$ISO" ]]; then
	echo "run_ir0_mmap.sh: missing $ISO (make kernel-x64-userspace.iso)" >&2
	exit 1
fi

if [[ ! -f "$ROOT/disk.img" ]]; then
	make -s -C "$ROOT" disk.img
fi

DISK="$(mktemp /tmp/ir0-linux-abi-mmap.XXXXXX.img)"
cp -f "$ROOT/disk.img" "$DISK"
python3 "$ROOT/scripts/inject_init_minix.py" "$DISK" "$PROBE" sbin/init

rm -f "$LOG"
echo "  LINUX_ABI  QEMU mmap_probe (IR0 workload)"
set +e
bash "$SMOKE" --log "$LOG" --timeout 90 --stale-sec 12 --done 'MMAPOK' -- \
	"$QEMU" -cdrom "$ISO" \
	-drive file="$DISK",format=raw,if=ide,index=0 \
	-serial stdio -display none -m 256M -no-reboot -net none
rc=$?
set -e
rm -f "$DISK"

cp "$LOG" "$OUT/stdout.log"
python3 "$ROOT/scripts/linux_abi/parse_mmap_trace.py" ir0 "$OUT/stdout.log" "$OUT/trace.json"

flat_log="$(tr -d '\n\r' < "$LOG" 2>/dev/null || true)"
if echo "$flat_log" | grep -q 'MMAPOK'; then
	rc=0
elif python3 -c "import json,sys; d=json.load(open('$OUT/trace.json')); sys.exit(0 if len(d.get('audit_steps',[]))>=7 else 1)" 2>/dev/null; then
	rc=0
fi

if [[ $rc -ne 0 ]]; then
	echo "✗ IR0 mmap workload failed (see $LOG)" >&2
	exit 1
fi

echo "✓ IR0 mmap workload -> $OUT"
