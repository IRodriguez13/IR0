#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Inject runit rootfs layout + binaries into a MINIX disk image.

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
DISK="${1:?usage: install-to-disk.sh DISK_IMAGE}"
RUNIT_BIN="${ROOT}/setup/runit/bin"
STAGE_BIN="${ROOT}/setup/runit/stage-bin"
BUSYBOX="${FASE50_BUSYBOX_BIN:-${ROOT}/setup/pid1/fase50_busybox_real}"
INJECT="python3 ${ROOT}/scripts/inject_init_minix.py"
MANIFEST="${BUSYBOX_MANIFEST:-${ROOT}/setup/busybox/required_applets.txt}"

if [ ! -f "$DISK" ]; then
	echo "✗ missing disk: $DISK" >&2
	exit 1
fi

need_bin() {
	if [ ! -f "$1" ]; then
		echo "✗ missing $1 (run make build-runit / build-busybox-fase50-min)" >&2
		exit 1
	fi
}

need_bin "$RUNIT_BIN/runit"
need_bin "$RUNIT_BIN/runit-init"
need_bin "$RUNIT_BIN/runsvdir"
need_bin "$RUNIT_BIN/runsv"
need_bin "$STAGE_BIN/runit_stage1"
need_bin "$STAGE_BIN/runit_stage2"
need_bin "$STAGE_BIN/runit_stage3"
need_bin "$STAGE_BIN/runit_console_run"
need_bin "$STAGE_BIN/runit_logger_run"
need_bin "$BUSYBOX"

echo "  RUNIT   Injecting binaries..."
$INJECT "$DISK" "$RUNIT_BIN/runit-init" sbin/init
$INJECT "$DISK" "$RUNIT_BIN/runit" sbin/runit
$INJECT "$DISK" "$RUNIT_BIN/runit-init" bin/runit-init
$INJECT "$DISK" "$RUNIT_BIN/runsvdir" bin/runsvdir
$INJECT "$DISK" "$RUNIT_BIN/runsv" bin/runsv
$INJECT "$DISK" "$RUNIT_BIN/sv" bin/sv

chmod +x "${ROOT}/scripts/busybox_inject_manifest.sh"
FASE50_BUSYBOX_BIN="$BUSYBOX" "${ROOT}/scripts/busybox_inject_manifest.sh" "$DISK" "$BUSYBOX" "$MANIFEST"

echo "  RUNIT   Injecting stage ELF stubs (exec path has no shebang)..."
$INJECT "$DISK" "$STAGE_BIN/runit_stage1" etc/runit/1
$INJECT "$DISK" "$STAGE_BIN/runit_stage2" etc/runit/2
$INJECT "$DISK" "$STAGE_BIN/runit_stage3" etc/runit/3
$INJECT "$DISK" "$STAGE_BIN/runit_console_run" etc/runit/sv/console/run
$INJECT "$DISK" "$STAGE_BIN/runit_logger_run" etc/runit/sv/logger/run

python3 "${ROOT}/scripts/verify_minix_rootfs.py" --gate "$DISK" \
	/sbin/init /sbin/runit /bin/runsvdir /bin/sh /bin/busybox \
	/etc/runit/1 /etc/runit/2 /etc/runit/3 \
	/etc/runit/sv/console/run /etc/runit/sv/logger/run

echo "✓ runit rootfs installed on $DISK"
