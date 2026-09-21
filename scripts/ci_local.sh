#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
#
# Staged local CI — run before push to avoid remote build mail storms.
#
# Stages (in increasing cost):
#   fast   — tooling-check (~1 min): unit tests, arch-guard, contracts
#   boot   — fast + release-check-boot on host tree (~3–5 min): ISO, kmang, QEMU, guest
#   docker — release-check-boot-container-local (~3 min): full E2E in Ubuntu container
#            with your working tree (catches uncommitted WIP before push)
#   rc     — fresh-clone-check (~3 min): git clone from GitHub (post-push / RC gate)
#   all    — fast + docker (recommended pre-push default)
#
# Usage:
#   make ci-local              # all = fast + docker
#   make ci-local-fast
#   make ci-local-boot
#   make ci-local-docker
#   make ci-local-rc           # after push; simulates third-party + CI
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
	run_stage fast make -s tooling-check PROFILE="$PROFILE"
	;;
boot)
	run_stage fast make -s tooling-check PROFILE="$PROFILE"
	run_stage boot make -s release-check-boot PROFILE="$PROFILE"
	;;
docker)
	run_stage docker make -s release-check-boot-container-local PROFILE="$PROFILE"
	;;
rc)
	run_stage rc make -s fresh-clone-check PROFILE="$PROFILE"
	;;
all)
	run_stage fast make -s tooling-check PROFILE="$PROFILE"
	run_stage docker make -s release-check-boot-container-local PROFILE="$PROFILE"
	;;
*)
	echo "✗ unknown CI_LOCAL_STAGE=${STAGE} (use fast|boot|docker|rc|all)" >&2
	exit 2
	;;
esac

echo ""
echo "✓ ci-local OK stage=${STAGE} PROFILE=${PROFILE}"
