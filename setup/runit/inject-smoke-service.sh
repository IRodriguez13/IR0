#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Add a runit supervised service to an existing runit rootfs disk image.
#
# Usage:
#   inject-smoke-service.sh DISK SERVICE RUN_STUB_HOST HARNESS_HOST HARNESS_DISK_PATH
#
# Example:
#   inject-smoke-service.sh disk.img fase52 stage-bin/runit_fase52_run \
#       setup/pid1/fase52_harness bin/fase52-harness

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
DISK="${1:?disk image}"
SERVICE="${2:?service name}"
RUN_STUB="${3:?run stub path on host}"
HARNESS_HOST="${4:?harness binary on host}"
HARNESS_DISK="${5:?harness path on disk (e.g. bin/fase52-harness)}"
INJECT="python3 ${ROOT}/scripts/inject_init_minix.py"

if [ ! -f "$DISK" ]; then
	echo "✗ missing disk: $DISK" >&2
	exit 1
fi
if [ ! -f "$RUN_STUB" ]; then
	echo "✗ missing run stub: $RUN_STUB (run make build-runit)" >&2
	exit 1
fi
if [ ! -f "$HARNESS_HOST" ]; then
	echo "✗ missing harness: $HARNESS_HOST" >&2
	exit 1
fi

RUN_DISK="etc/runit/sv/${SERVICE}/run"

echo "  RUNIT   smoke service ${SERVICE} -> ${HARNESS_DISK}"
$INJECT "$DISK" "$RUN_STUB" "$RUN_DISK"
$INJECT "$DISK" "$HARNESS_HOST" "$HARNESS_DISK"

python3 "${ROOT}/scripts/verify_minix_rootfs.py" "$DISK" "/${RUN_DISK}" "/${HARNESS_DISK}"
echo "✓ smoke service ${SERVICE} installed"
