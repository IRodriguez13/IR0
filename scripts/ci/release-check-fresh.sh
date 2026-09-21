#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Fresh-clone Tier-1 release-check (IR0 + ISD sibling layout, no QEMU).
set -euo pipefail

IR0_URL="${IR0_URL:-https://github.com/IRodriguez13/IR0.git}"
ISD_URL="${ISD_URL:-https://github.com/IRodriguez13/ISD.git}"
IR0_REF="${IR0_REF:-master}"
ISD_REF="${ISD_REF:-master}"
PROFILE="${PROFILE:-minimal}"
WORK="${WORK:-/tmp/ir0-release-check.$$}"

cleanup() {
	rm -rf "$WORK"
}
trap cleanup EXIT

mkdir -p "$WORK"
echo "== fresh-clone release-check WORK=$WORK PROFILE=$PROFILE =="

git clone --depth 1 --branch "$IR0_REF" "$IR0_URL" "$WORK/IR0"
git clone --depth 1 --branch "$ISD_REF" "$ISD_URL" "$WORK/ISD"

cd "$WORK/IR0"
export IR0_ISD_ROOT="$WORK/ISD"
export IR0_DEPS_INSTALL=never
export RELEASE_CHECK_FRESH=1

make -s release-check-clean PROFILE="$PROFILE"

echo "✓ fresh-clone release-check OK"
