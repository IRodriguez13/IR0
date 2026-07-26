#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Run IR0 with userspace-init kernel and a custom PID1 on MINIX disk.

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
INIT="${INIT:-setup/pid1/sbin/init}"
DISK="${IR0_DISK:-disk.img}"
ISO="${KERNEL_USERSPACE_ISO:-$ROOT/kernel-x64-userspace.iso}"
SERIAL_LOG="${IR0_SERIAL_LOG:-/tmp/ir0-run.log}"
QEMU="${QEMU:-qemu-system-x86_64}"
QEMU_EXTRA="${IR0_QEMU_ARGS:-}"
FRESH_DISK="${IR0_FRESH_DISK:-0}"
DEBUG="${IR0_DEBUG:-0}"
WORK_DISK=""
INJECT="${ROOT}/scripts/inject_init_minix.py"

resolve_init() {
	local candidate
	for candidate in "$INIT" \
		"${IR0_USERSPACE_ROOT:-$ROOT/../IR0-userspace}/out/bin/runit-init" \
		setup/pid1/sbin/init \
		setup/pid1/init; do
		if [[ -f "$candidate" ]]; then
			echo "$(cd "$(dirname "$candidate")" && pwd)/$(basename "$candidate")"
			return 0
		fi
		if [[ -f "$ROOT/$candidate" ]]; then
			echo "$(cd "$(dirname "$ROOT/$candidate")" && pwd)/$(basename "$candidate")"
			return 0
		fi
	done
	return 1
}

cleanup() {
	if [[ -n "$WORK_DISK" && -f "$WORK_DISK" ]]; then
		rm -f "$WORK_DISK"
	fi
}

INIT_BIN="$(resolve_init)" || {
	echo "run_pid1.sh: no PID1 binary found (tried INIT=$INIT and fallbacks)" >&2
	echo "  Build one of: make build-runit, make build-init-smoke" >&2
	exit 1
}

if [[ ! -f "$ISO" ]]; then
	echo "run_pid1.sh: missing $ISO (make kernel-x64-userspace.iso)" >&2
	exit 1
fi

BASE_DISK="$DISK"
if [[ "$DISK" != /* ]]; then
	BASE_DISK="$ROOT/$DISK"
fi

if [[ ! -f "$BASE_DISK" ]]; then
	echo "  DISK    $BASE_DISK missing — creating default MINIX disk"
	make -s -C "$ROOT" disk.img
	BASE_DISK="$ROOT/disk.img"
fi

if [[ "$FRESH_DISK" == "1" ]]; then
	WORK_DISK="$(mktemp /tmp/ir0-run-pid1.XXXXXX.img)"
	cp -f "$BASE_DISK" "$WORK_DISK"
	trap cleanup EXIT
	RUN_DISK="$WORK_DISK"
else
	RUN_DISK="$BASE_DISK"
fi

echo "  PID1    $INIT_BIN → /sbin/init on $RUN_DISK"
python3 "$INJECT" "$RUN_DISK" "$INIT_BIN" sbin/init

echo "  RUN     kernel-x64-userspace.iso + $RUN_DISK"
if [[ "$DEBUG" == "1" ]]; then
	exec "$QEMU" -cdrom "$ISO" \
		-drive file="$RUN_DISK",format=raw,if=ide,index=0 \
		-m 256M -no-reboot -net none \
		-serial stdio -display none \
		$QEMU_EXTRA
fi

rm -f "$SERIAL_LOG"
exec "$QEMU" -cdrom "$ISO" \
	-drive file="$RUN_DISK",format=raw,if=ide,index=0 \
	-m 256M -no-reboot -net none \
	-display gtk \
	-serial "file:$SERIAL_LOG" \
	$QEMU_EXTRA
