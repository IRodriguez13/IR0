#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Tier 1.5 release-check: QEMU guest boot smoke (minimal profile).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

PROFILE="${PROFILE:-minimal}"
ARCH="${ISD_ARCH:-x86_64}"
FAIL=0

step_ok() { echo "[OK]  $*"; }
step_fail() { echo "[FAIL] $*"; FAIL=1; }

run_step() {
	local label="$1"
	shift
	echo ""
	echo "== ${label} =="
	if "$@"; then
		step_ok "$label"
	else
		step_fail "$label"
	fi
}

ISD_ROOT="$(bash scripts/resolve_isd_root.sh "$ROOT")"
export IR0_ISD_ROOT="$ISD_ROOT"

echo "== release-check-boot (Tier 1.5 QEMU smoke) PROFILE=${PROFILE} =="

run_step "kernel-x64-userspace.iso" make -s kernel-x64-userspace.iso
run_step "smoke-runit-boot" \
	make -s smoke-runit-boot IR0_PRODUCT_PROFILE="$PROFILE" ARCH="$ARCH"

echo ""
if [ "$FAIL" -ne 0 ]; then
	echo "✗ release-check-boot FAILED"
	exit 1
fi
echo "✓ release-check-boot OK PROFILE=${PROFILE}"
