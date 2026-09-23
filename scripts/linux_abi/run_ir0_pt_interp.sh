#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# QEMU: PT_INTERP present → jump to /lib/ld64.so.1 (not main e_entry).

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT="${1:-$ROOT/build/linux_abi_audit/ir0/pt_interp}"
OUT="$(mkdir -p "$OUT" && cd "$OUT" && pwd)"
ISO="${KERNEL_USERSPACE_ISO:-$ROOT/kernel-x64-userspace.iso}"
LOG="$OUT/qemu_serial.log"
SMOKE="$ROOT/scripts/smoke_qemu_run.sh"
QEMU="${QEMU:-qemu-system-x86_64}"
CC="${MUSL_CC:-}"
if [[ -z "$CC" ]]; then
	if command -v musl-gcc >/dev/null 2>&1; then
		CC=musl-gcc
	else
		echo "run_ir0_pt_interp.sh: need musl-gcc (or MUSL_CC) to emit PT_INTERP" >&2
		exit 1
	fi
fi

mkdir -p "$OUT"

if [[ ! -f "$ISO" ]]; then
	echo "run_ir0_pt_interp.sh: missing $ISO (make kernel-x64-userspace.iso)" >&2
	exit 1
fi

PROBE="$OUT/pt_interp_probe"
DMAIN="$OUT/dmain"
LDSO="$OUT/ld64.so.1"
NEEDED="$OUT/needed.so"

"$CC" -static -Os -o "$PROBE" "$ROOT/scripts/linux_abi/workloads/pt_interp_probe.c"
# -nostdlib alone is fully static (no PT_INTERP). A dummy .so forces INTERP.
echo 'int ir0_interp_needed;' | "$CC" -shared -fPIC -nostdlib -o "$NEEDED" -x c -
"$CC" -nostdlib -no-pie -e _start \
	-Wl,-dynamic-linker,/lib/ld64.so.1 \
	-o "$DMAIN" "$ROOT/scripts/linux_abi/workloads/pt_interp_freestanding.c" "$NEEDED"
"$CC" -nostdlib -shared -fPIC -e _start -DPT_INTERP_LDSO \
	-o "$LDSO" "$ROOT/scripts/linux_abi/workloads/pt_interp_freestanding.c"
if ! readelf -l "$DMAIN" | grep -q 'ld64.so.1'; then
	echo "run_ir0_pt_interp.sh: $DMAIN missing PT_INTERP /lib/ld64.so.1" >&2
	exit 1
fi

DISK="$(mktemp /tmp/ir0-pt-interp.XXXXXX.img)"
truncate -s 200M "$DISK"
python3 "$ROOT/scripts/inject_init_minix.py" --format-large "$DISK"
python3 "$ROOT/scripts/inject_init_minix.py" "$DISK" "$PROBE" sbin/init
python3 "$ROOT/scripts/inject_init_minix.py" "$DISK" "$DMAIN" sbin/dmain
python3 "$ROOT/scripts/inject_init_minix.py" "$DISK" "$LDSO" lib/ld64.so.1
python3 "$ROOT/scripts/verify_minix_rootfs.py" "$DISK" /sbin/init /sbin/dmain /lib/ld64.so.1

rm -f "$LOG"
echo "  QEMU  PT_INTERP (interp present)"
set +e
bash "$SMOKE" --log "$LOG" --timeout 90 --stale-sec 12 --done 'PT_INTERP_OK' --done '[PTINTERPOK]' -- \
	"$QEMU" -cdrom "$ISO" \
	-drive file="$DISK",format=raw,if=ide,index=0 \
	-serial stdio -display none -m 256M -no-reboot -net none
rc=$?
set -e
rm -f "$DISK"

if [[ $rc -ne 0 ]]; then
	echo "✗ IR0 PT_INTERP QEMU failed (see $LOG)" >&2
	exit 1
fi
if ! grep -q 'PT_INTERP_OK' "$LOG"; then
	echo "✗ missing PT_INTERP_OK in $LOG" >&2
	exit 1
fi
if grep -q 'MAIN_NO_INTERP' "$LOG"; then
	echo "✗ jumped to main e_entry instead of interp (see $LOG)" >&2
	exit 1
fi

# Missing interpreter: jump to main e_entry (tcc hello local-reloc belt).
DISK="$(mktemp /tmp/ir0-pt-interp-fb.XXXXXX.img)"
truncate -s 200M "$DISK"
python3 "$ROOT/scripts/inject_init_minix.py" --format-large "$DISK"
python3 "$ROOT/scripts/inject_init_minix.py" "$DISK" "$DMAIN" sbin/init
python3 "$ROOT/scripts/verify_minix_rootfs.py" "$DISK" /sbin/init
FBLOG="$OUT/qemu_fallback.log"
rm -f "$FBLOG"
echo "  QEMU  PT_INTERP (interp missing → main fallback)"
set +e
bash "$SMOKE" --log "$FBLOG" --timeout 90 --stale-sec 12 --done 'MAIN_NO_INTERP' -- \
	"$QEMU" -cdrom "$ISO" \
	-drive file="$DISK",format=raw,if=ide,index=0 \
	-serial stdio -display none -m 256M -no-reboot -net none
fbrc=$?
set -e
rm -f "$DISK"
if [[ $fbrc -ne 0 ]] || ! grep -q 'MAIN_NO_INTERP' "$FBLOG"; then
	echo "✗ IR0 PT_INTERP fallback QEMU failed (see $FBLOG)" >&2
	exit 1
fi

echo "✓ IR0 PT_INTERP QEMU -> $OUT"
