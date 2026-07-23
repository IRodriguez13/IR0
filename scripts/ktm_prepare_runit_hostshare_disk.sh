#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Build a temporary MINIX disk with product runit PID1 + hostshare payload service.
#
# Usage:
#   ktm_prepare_runit_hostshare_disk.sh [--quiet-console] DISK_OUT
#
# Prints absolute DISK_OUT path on stdout (last line). Caller owns cleanup.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
QUIET=0
if [ "${1:-}" = "--quiet-console" ]; then
	QUIET=1
	shift
fi
DISK_OUT="${1:?disk output path}"

cd "$ROOT"
if [ ! -x setup/runit/stage-bin/runit_hostshare_payload_run ]; then
	echo "✗ missing runit_hostshare_payload_run — run make build-runit" >&2
	exit 1
fi
if [ ! -x setup/runit/install-to-disk.sh ]; then
	echo "✗ missing setup/runit/install-to-disk.sh" >&2
	exit 1
fi

dd if=/dev/zero of="$DISK_OUT" bs=1M count=200 status=none
python3 scripts/inject_init_minix.py --format-large "$DISK_OUT"
FASE50_BUSYBOX_BIN="${FASE50_BUSYBOX_BIN:-setup/pid1/fase50_busybox_real}" \
	./setup/runit/install-to-disk.sh "$DISK_OUT"
chmod +x setup/runit/inject-smoke-service.sh
./setup/runit/inject-smoke-service.sh --run-only "$DISK_OUT" ktm \
	setup/runit/stage-bin/runit_hostshare_payload_run

if [ "$QUIET" = 1 ]; then
	if [ ! -x setup/runit/stage-bin/runit_pause_run ]; then
		echo "✗ missing runit_pause_run — run make build-runit" >&2
		exit 1
	fi
	python3 scripts/inject_init_minix.py "$DISK_OUT" \
		setup/runit/stage-bin/runit_pause_run etc/runit/sv/console/run
	python3 scripts/inject_init_minix.py "$DISK_OUT" \
		setup/runit/stage-bin/runit_pause_run etc/runit/sv/logger/run
fi

python3 scripts/verify_minix_rootfs.py "$DISK_OUT" /sbin/init /etc/runit/sv/ktm/run
echo "$DISK_OUT"
