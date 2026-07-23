#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Run a KTM userdev case under product runit PID1 + virtio-9p payload.
#
# Usage:
#   ktm_userdev_runit_run.sh --init ELF --log PATH --done TAG [--require TAG]...
#                            [--timeout SEC] [--inject SRC:DEST]...
#                            [--host-file NAME] [--host-grep SUBSTR]
#                            [--qemu-arg ARG]...
#
# Prepares a temp MINIX disk (runit + hostshare service), runs ktm_userdev_runner.py
# with --disk, then deletes the disk. Exit status is the runner's.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
INIT=""
LOG="/tmp/ktm-userdev-runit.log"
DONE="KTM_USERDEV_OK"
TIMEOUT=180
REQUIRES=()
INJECTS=()
QEMU_ARGS=()
HOST_FILE=""
HOST_GREP=""

while [ $# -gt 0 ]; do
	case "$1" in
	--init)
		INIT="${2:?}"
		shift 2
		;;
	--log)
		LOG="${2:?}"
		shift 2
		;;
	--done)
		DONE="${2:?}"
		shift 2
		;;
	--require)
		REQUIRES+=(--require "${2:?}")
		shift 2
		;;
	--timeout)
		TIMEOUT="${2:?}"
		shift 2
		;;
	--inject)
		INJECTS+=(--inject "${2:?}")
		shift 2
		;;
	--host-file)
		HOST_FILE="${2:?}"
		shift 2
		;;
	--host-grep)
		HOST_GREP="${2:?}"
		shift 2
		;;
	--qemu-arg)
		QEMU_ARGS+=(--qemu-arg "${2:?}")
		shift 2
		;;
	-h|--help)
		sed -n '2,14p' "$0"
		exit 0
		;;
	*)
		echo "✗ unknown arg: $1" >&2
		exit 2
		;;
	esac
done

if [ -z "$INIT" ]; then
	echo "✗ --init ELF required" >&2
	exit 2
fi
if [ ! -f "$INIT" ]; then
	if [ -f "$ROOT/$INIT" ]; then
		INIT="$ROOT/$INIT"
	else
		echo "✗ missing --init $INIT" >&2
		exit 2
	fi
fi

cd "$ROOT"
chmod +x scripts/ktm_prepare_runit_hostshare_disk.sh
DISK="$(mktemp /tmp/ir0-ktm-runit.XXXXXX.img)"
cleanup() {
	rm -f "$DISK"
}
trap cleanup EXIT

scripts/ktm_prepare_runit_hostshare_disk.sh --quiet-console "$DISK" >/dev/null

ARGS=(
	python3 scripts/ktm_userdev_runner.py
	--disk "$DISK"
	--init "$INIT"
	--log "$LOG"
	--timeout "$TIMEOUT"
	--done "$DONE"
	--require RUNIT_STAGE2_OK
	--require RUNSV_HOSTSHARE_START
	--require HOSTSHARE_EXEC_MOUNT_OK
)
ARGS+=("${REQUIRES[@]}")
ARGS+=("${INJECTS[@]}")
if [ -n "$HOST_FILE" ]; then
	ARGS+=(--host-file "$HOST_FILE")
fi
if [ -n "$HOST_GREP" ]; then
	ARGS+=(--host-grep "$HOST_GREP")
fi
ARGS+=("${QEMU_ARGS[@]}")

"${ARGS[@]}"
