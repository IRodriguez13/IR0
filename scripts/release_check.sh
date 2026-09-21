#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# IR0 release-check orchestrator (Tier 1, no QEMU guest boot).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

PROFILE="${PROFILE:-minimal}"
ARCH="${ISD_ARCH:-x86_64}"
RELEASE_CHECK_FRESH="${RELEASE_CHECK_FRESH:-0}"
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

if [ "$RELEASE_CHECK_FRESH" = "1" ]; then
	echo "== release-check-clean (RELEASE_CHECK_FRESH=1) =="
	isd_root="${IR0_ISD_ROOT:-$ROOT/../ISD}"
	if [ -d "${isd_root}/out" ]; then
		step_fail "ISD out/ must not exist for fresh check (found ${isd_root}/out)"
		exit 1
	fi
	if [ -f "$ROOT/kernel-x64-userspace.iso" ]; then
		step_fail "remove kernel-x64-userspace.iso before release-check-clean"
		exit 1
	fi
fi

if [ ! -f "$ROOT/.config" ]; then
	echo "== bootstrap .config from setup/defconfig =="
	make -s defconfig
fi

run_step "deptest PROFILE=${PROFILE}" make -s deptest PROFILE="$PROFILE"

run_step "repo-hygiene-guard" make -s repo-hygiene-guard
run_step "kernel-x64.bin" make -s kernel-x64.bin

echo ""
echo "== embedded kernel release =="
want="$(MAKEFLAGS= make -s -f Makefile -pn 2>/dev/null | sed -n 's/^IR0_VERSION_STRING := //p' | head -1)"
if [ -z "$want" ]; then
	step_fail "could not resolve IR0_VERSION_STRING from Makefile"
elif grep -a -Fq "IR0VER:${want}" "$ROOT/kernel-x64.bin"; then
	step_ok "embedded release IR0VER:${want}"
else
	step_fail "kernel missing IR0VER:${want}"
fi

run_step "arch-guard" make -s arch-guard
run_step "tests/host" make -s -C tests/host run
run_step "kmang tests" python3 scripts/test_kernel_manager.py
run_step "truth tests" python3 scripts/test_truth_tooling.py
run_step "isd-contracts" make -s isd-contracts
run_step "check-isd" make -s check-isd

ISD_ROOT="$(bash scripts/resolve_isd_root.sh "$ROOT")"
export IR0_ISD_ROOT="$ISD_ROOT"

if [ ! -f "$ISD_ROOT/packages/busybox/src/Makefile" ]; then
	run_step "ISD fetch PROFILE=${PROFILE}" \
		make -s -C "$ISD_ROOT" fetch PROFILE="$PROFILE" ARCH="$ARCH"
fi

run_step "ISD release-check PROFILE=${PROFILE}" \
	make -s -C "$ISD_ROOT" IR0_ROOT="$ROOT" ARCH="$ARCH" PROFILE="$PROFILE" release-check

run_step "usmang verify PROFILE=${PROFILE}" \
	make -s usmang-verify PROFILE="$PROFILE" ARCH="$ARCH" IR0_ISD_ROOT="$ISD_ROOT"

echo ""
if [ "$FAIL" -ne 0 ]; then
	echo "✗ release-check FAILED"
	exit 1
fi
echo "✓ release-check OK PROFILE=${PROFILE}"
