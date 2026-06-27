#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Run process_lifecycle_probe under IR0 (QEMU + injected init + exec_helper).

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT="${1:-$ROOT/build/linux_abi_audit/ir0/process_lifecycle}"
OUT="$(mkdir -p "$OUT" && cd "$OUT" && pwd)"
PROBE_LINUX="$ROOT/build/linux_abi_audit/process_lifecycle_probe"
PROBE="$OUT/process_lifecycle_probe_ir0"
HELPER="$ROOT/build/linux_abi_audit/exec_helper"
ISO="${KERNEL_USERSPACE_ISO:-$ROOT/kernel-x64-userspace.iso}"
LOG="$OUT/qemu_serial.log"
SMOKE="$ROOT/scripts/smoke_qemu_run.sh"
QEMU="${QEMU:-qemu-system-x86_64}"
MUSL_CC="musl-gcc"
command -v musl-gcc >/dev/null 2>&1 || MUSL_CC="gcc"

mkdir -p "$OUT"

if [[ ! -x "$HELPER" ]]; then
	echo "run_ir0_process_lifecycle.sh: missing $HELPER" >&2
	exit 1
fi
if [[ ! -f "$ISO" ]]; then
	echo "run_ir0_process_lifecycle.sh: missing $ISO (make kernel-x64-userspace.iso)" >&2
	exit 1
fi

echo "  BUILD   IR0 process_lifecycle_probe (EXEC_HELPER_PATH=/sbin/exec_helper)"
"$MUSL_CC" -static -Os '-DEXEC_HELPER_PATH="/sbin/exec_helper"' \
	-o "$PROBE" "$ROOT/scripts/linux_abi/workloads/process_lifecycle_probe.c"

DISK="$(mktemp /tmp/ir0-linux-abi-pl.XXXXXX.img)"
truncate -s 200M "$DISK"
python3 "$ROOT/scripts/inject_init_minix.py" --format-large "$DISK"
python3 "$ROOT/scripts/inject_init_minix.py" "$DISK" "$PROBE" sbin/init
python3 "$ROOT/scripts/inject_init_minix.py" "$DISK" "$HELPER" sbin/exec_helper
python3 "$ROOT/scripts/verify_minix_rootfs.py" --gate "$DISK" /sbin /sbin/init /sbin/exec_helper

rm -f "$LOG"
echo "  LINUX_ABI  QEMU process_lifecycle_probe (IR0 workload, PID1 reparent)"
set +e
bash "$SMOKE" --log "$LOG" --timeout 180 --stale-sec 45 --done 'PROC_LIFECYCLEOK' -- \
	"$QEMU" -cdrom "$ISO" \
	-drive file="$DISK",format=raw,if=ide,index=0 \
	-serial stdio -display none -m 256M -no-reboot -net none
rc=$?
set -e
rm -f "$DISK"

cp "$LOG" "$OUT/stdout.log"
python3 "$ROOT/scripts/linux_abi/parse_process_lifecycle_trace.py" ir0 "$OUT/stdout.log" "$OUT/trace.json"

flat_log="$(tr -d '\n\r' < "$LOG" 2>/dev/null || true)"
if echo "$flat_log" | grep -q 'PROC_LIFECYCLEOK'; then
	rc=0
else
	rc=1
fi

if [[ $rc -ne 0 ]]; then
	echo "✗ IR0 process_lifecycle workload failed (see $LOG)" >&2
	exit 1
fi

echo "✓ IR0 process_lifecycle workload -> $OUT"
