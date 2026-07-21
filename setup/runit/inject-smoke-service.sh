#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Add a runit supervised service to an existing runit rootfs disk image.
#
# Usage:
#   inject-smoke-service.sh DISK SERVICE RUN_STUB_HOST HARNESS_HOST HARNESS_DISK_PATH
#   inject-smoke-service.sh --run-only DISK SERVICE RUN_STUB_HOST
#
# Example:
#   inject-smoke-service.sh disk.img fase52 stage-bin/runit_fase52_run \
#       setup/pid1/fase52_harness bin/fase52-harness
#   inject-smoke-service.sh --run-only disk.img xfbdev \
#       setup/runit/stage-bin/runit_hostshare_payload_run

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
RUN_ONLY=0
if [ "${1:-}" = "--run-only" ]; then
	RUN_ONLY=1
	shift
fi

DISK="${1:?disk image}"
SERVICE="${2:?service name}"
RUN_STUB="${3:?run stub path on host}"
INJECT="python3 ${ROOT}/scripts/inject_init_minix.py"

if [ "$RUN_ONLY" -eq 0 ]; then
	HARNESS_HOST="${4:?harness binary on host}"
	HARNESS_DISK="${5:?harness path on disk (e.g. bin/fase52-harness)}"
fi

if [ ! -f "$DISK" ]; then
	echo "✗ missing disk: $DISK" >&2
	exit 1
fi
if [ ! -f "$RUN_STUB" ]; then
	echo "✗ missing run stub: $RUN_STUB (run make build-runit)" >&2
	exit 1
fi
if [ "$RUN_ONLY" -eq 0 ] && [ ! -f "$HARNESS_HOST" ]; then
	echo "✗ missing harness: $HARNESS_HOST" >&2
	exit 1
fi

RUN_DISK="etc/runit/sv/${SERVICE}/run"

if [ "$RUN_ONLY" -eq 1 ]; then
	echo "  RUNIT   smoke service ${SERVICE} (run-only)"
	$INJECT "$DISK" "$RUN_STUB" "$RUN_DISK"
	python3 "${ROOT}/scripts/verify_minix_rootfs.py" "$DISK" "/${RUN_DISK}"
else
	echo "  RUNIT   smoke service ${SERVICE} -> ${HARNESS_DISK}"
	$INJECT "$DISK" "$RUN_STUB" "$RUN_DISK"
	$INJECT "$DISK" "$HARNESS_HOST" "$HARNESS_DISK"
	python3 "${ROOT}/scripts/verify_minix_rootfs.py" "$DISK" "/${RUN_DISK}" "/${HARNESS_DISK}"
fi
echo "✓ smoke service ${SERVICE} installed"
