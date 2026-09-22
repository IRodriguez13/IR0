#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
#
# Staged local CI — run before push to avoid remote build mail storms.
#
# Taxonomy (frozen):
#   FAST  — tooling-check (~1 min): unit, arch-guard, contracts, truth
#   BOOT  — FAST + release-check-boot on host tree: ISO, kmang, QEMU, guest
#   CLEAN — Docker first-time pack of THIS tree (copy IR0+ISD, strip out/)
#   RC    — Docker git clone from GitHub (published trees only)
#   all   — FAST + CLEAN (recommended pre-push)
#
# Usage:
#   make ci-local              # all = FAST + CLEAN
#   make ci-local-fast         # FAST
#   make ci-local-boot         # FAST + BOOT
#   make ci-local-docker       # CLEAN (new machine of the working tree)
#   make ci-local-rc           # RC (new machine of origin/dev)
#
# Env:
#   CI_LOCAL_STAGE   fast|boot|docker|rc|all
#   PROFILE          ISD profile (default: minimal)
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

STAGE="${CI_LOCAL_STAGE:-all}"
PROFILE="${PROFILE:-minimal}"

run_stage() {
	local name="$1"
	shift
	echo ""
	echo "########################################"
	echo "# ci-local stage: ${name}"
	echo "########################################"
	if "$@"; then
		echo "✓ ci-local stage ${name} OK"
	else
		echo "✗ ci-local stage ${name} FAILED"
		return 1
	fi
}

case "$STAGE" in
fast)
	run_stage FAST make -s tooling-check PROFILE="$PROFILE"
	;;
boot)
	run_stage FAST make -s tooling-check PROFILE="$PROFILE"
	run_stage BOOT make -s release-check-boot PROFILE="$PROFILE"
	;;
docker|docker-wip)
	run_stage CLEAN make -s release-check-boot-container-local PROFILE="$PROFILE"
	;;
rc)
	run_stage RC make -s fresh-clone-check PROFILE="$PROFILE"
	;;
all)
	run_stage FAST make -s tooling-check PROFILE="$PROFILE"
	run_stage CLEAN make -s release-check-boot-container-local PROFILE="$PROFILE"
	;;
*)
	echo "✗ unknown CI_LOCAL_STAGE=${STAGE} (use fast|boot|docker|rc|all)" >&2
	exit 2
	;;
esac

echo ""
echo "✓ ci-local OK stage=${STAGE} PROFILE=${PROFILE}"
