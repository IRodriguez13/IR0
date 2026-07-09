#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Run a static ABI probe under IR0 (QEMU userspace ISO + injected init).

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
CONTRACT="${1:?contract}"
PROBE_BASENAME="${2:?probe_basename}"
DONE_TAG="${3:?done tag e.g. POLLOK}"
OUT="${4:-$ROOT/build/linux_abi_audit/ir0/$CONTRACT}"
OUT="$(mkdir -p "$OUT" && cd "$OUT" && pwd)"
PROBE="$ROOT/build/linux_abi_audit/$PROBE_BASENAME"
ISO="${KERNEL_USERSPACE_ISO:-$ROOT/kernel-x64-userspace.iso}"
LOG="$OUT/qemu_serial.log"
SMOKE="$ROOT/scripts/smoke_qemu_run.sh"
QEMU="${QEMU:-qemu-system-x86_64}"

mkdir -p "$OUT"

if [[ ! -x "$PROBE" ]]; then
	echo "run_ir0_workload.sh: missing $PROBE" >&2
	exit 1
fi

if [[ ! -f "$ISO" ]]; then
	echo "run_ir0_workload.sh: missing $ISO (make kernel-x64-userspace.iso)" >&2
	exit 1
fi

if [[ ! -f "$ROOT/disk.img" ]]; then
	make -s -C "$ROOT" disk.img
fi

DISK="$(mktemp /tmp/ir0-linux-abi-${CONTRACT}.XXXXXX.img)"
cp -f "$ROOT/disk.img" "$DISK"
python3 "$ROOT/scripts/inject_init_minix.py" "$DISK" "$PROBE" sbin/init

rm -f "$LOG"
echo "  LINUX_ABI  QEMU $PROBE_BASENAME (IR0 workload)"
set +e
bash "$SMOKE" --log "$LOG" --timeout 90 --stale-sec 12 --done "$DONE_TAG" -- \
	"$QEMU" -cdrom "$ISO" \
	-drive file="$DISK",format=raw,if=ide,index=0 \
	-serial stdio -display none -m 256M -no-reboot -net none
rc=$?
set -e
rm -f "$DISK"

cp "$LOG" "$OUT/stdout.log"
python3 "$ROOT/scripts/linux_abi/parse_simple_trace.py" \
	"$CONTRACT" ir0 "$OUT/stdout.log" "$OUT/trace.json"

flat_log="$(tr -d '\n\r' < "$LOG" 2>/dev/null || true)"
if echo "$flat_log" | grep -q "$DONE_TAG"; then
	rc=0
fi

if [[ $rc -ne 0 ]]; then
	echo "✗ IR0 $CONTRACT workload failed (see $LOG)" >&2
	exit 1
fi

echo "✓ IR0 $CONTRACT workload -> $OUT"
