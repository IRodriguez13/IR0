#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# QA / test / CI helper — wraps the current kernel make targets.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

show_help() {
	cat <<'EOF'
IR0 QA / test / CI — scripts/ir0-qa.sh

Usage:
  scripts/ir0-qa.sh <make-target> [make-args...]

Interactive testing:
  make test                  TUI suite picker (scripts/test_catalog.yaml)
  make test SUITE=list       List suites
  make test SUITE=host,ktest

Common gates:
  make ctr                   kernel + arch-guard + matrix-min + tests/host
  make test-fast             kernel + arch-guard + host + ktm-check
  make ktest                 in-kernel ktest suite (QEMU)
  make build-matrix-full     daily matrix plus CI cases

The legacy QA smoke catalog was removed from the Makefile. `make run` on x86
attaches the ISD profile disk (`PROFILE`, default minimal). Build that image
in the ISD tree.

List all make targets:
  scripts/ir0-qa.sh targets

Kernel build/run: make help
EOF
}

if [[ $# -eq 0 ]] || [[ "${1:-}" == "help" ]] || [[ "${1:-}" == "-h" ]] || [[ "${1:-}" == "--help" ]]; then
	show_help
	exit 0
fi

if [[ "${1:-}" == "targets" ]]; then
	make -pR 2>/dev/null \
		| awk -F: '/^[a-zA-Z0-9_.-]+:/ {print $1}' \
		| sort -u \
		| grep -Ev '^(Makefile|\.|%)' || true
	exit 0
fi

make "$@"
