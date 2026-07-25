#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# QA / smoke / test / CI gates — targets live in scripts/make/qa.mk (not default make help).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

export IR0_INCLUDE_QA=1

qa_make() {
	# shellcheck disable=SC2068
	make IR0_INCLUDE_QA=1 "$@"
}

show_help() {
	cat <<'EOF'
IR0 QA / smoke / CI — scripts/ir0-qa.sh

Usage:
  scripts/ir0-qa.sh <make-target> [make-args...]
  IR0_LEGACY_SMOKE=1 scripts/ir0-qa.sh smoke-fase50-busybox

Common gates:
  make test             Host tests + kernel-tests (fast)
  make qa               Stable QA (matrix + kill_sigterm ABI audit)
  make release          release-0.0.1 gate
  ctr                  kernel + arch-guard + build-matrix-min + tests/host
  test-fast            arch-guard + tests/host only
  health               analyze + text budget + memsafe + kernel-tests
  smoke-tier1          runit boot + ash interactive
  smoke-release-0.0.1  phase1 + linux-abi-audit + ash + FAT16
  release-0.0.1        health + smoke-release-0.0.1
  kernel-tests         in-kernel ktest suite (QEMU)
  linux-abi-audit-kill-sigterm  Isolated SIGTERM/wait4 probe (fresh ISO)
  linux-abi-audit      Linux↔IR0 ABI contract audit (all enabled)

Userspace / GUI (via qa.mk + legacy-smokes.mk):
  run-fase58e-ash-gui          runit + BusyBox ash (GTK)
  run-fase55d-doomgeneric-gui  Doom interactive (set REAL_WAD_PATH=...)
  smoke-runit-boot             runit PID1 boot smoke

List all qa targets:
  scripts/ir0-qa.sh targets

Kernel build/run stay on plain make — see: make help
EOF
}

if [[ $# -eq 0 ]] || [[ "${1:-}" == "help" ]] || [[ "${1:-}" == "-h" ]] || [[ "${1:-}" == "--help" ]]; then
	show_help
	exit 0
fi

if [[ "${1:-}" == "targets" ]]; then
	make IR0_INCLUDE_QA=1 -pR 2>/dev/null \
		| awk -F: '/^[a-zA-Z0-9_.-]+:/ {print $1}' \
		| sort -u \
		| grep -Ev '^(Makefile|\.|%)' || true
	exit 0
fi

qa_make "$@"
