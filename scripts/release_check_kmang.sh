#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Headless kmang CI pipeline (Docker-safe — no TUI).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

PROFILE="${PROFILE:-minimal}"
ARCH="${ISD_ARCH:-x86_64}"
KMANG_DIR="${KMANG_DIR:-}"
ISO="${KMANG_ISO:-$ROOT/kernel-x64-userspace.iso}"
FAIL=0

step_ok() { echo "[OK]  kmang $*"; }
step_fail() { echo "[FAIL] kmang $*"; FAIL=1; }

if [ -z "$KMANG_DIR" ]; then
	echo "✗ KMANG_DIR required" >&2
	exit 2
fi

want="$(MAKEFLAGS= make -s -f Makefile -pn 2>/dev/null | sed -n 's/^IR0_VERSION_STRING := //p' | head -1)"
if [ -z "$want" ]; then
	echo "✗ could not resolve IR0_VERSION_STRING" >&2
	exit 2
fi
if [ ! -f "$ISO" ]; then
	echo "✗ missing ISO: $ISO" >&2
	exit 2
fi

kmang() {
	python3 scripts/kernel_manager.py \
		--machine-dir "$KMANG_DIR" \
		--arch "$ARCH" \
		--profile "$PROFILE" \
		--machine ci \
		--kernel-root "$ROOT" \
		--source "$ISO" \
		--version "$want" \
		"$@"
}

echo "== release-check-kmang PROFILE=${PROFILE} store=${KMANG_DIR} =="

echo ""
echo "== kmang install-workspace =="
if kmang install-workspace; then
	step_ok "install-workspace"
else
	step_fail "install-workspace"
fi

echo ""
echo "== kmang workspace =="
if kmang workspace | grep -q 'installed'; then
	step_ok "workspace (installed)"
else
	step_fail "workspace (expected installed state)"
fi

echo ""
echo "== kmang compare (workspace vs catalog) =="
if kmang compare >/tmp/ir0-kmang-compare.$$.json 2>&1; then
	step_ok "compare (needs_install=0)"
else
	step_fail "compare (workspace not enrolled or drift)"
	cat /tmp/ir0-kmang-compare.$$.json 2>/dev/null || true
fi
rm -f /tmp/ir0-kmang-compare.$$.json

echo ""
echo "== kmang list =="
if kmang list --json | python3 -c "import json,sys; d=json.load(sys.stdin); sys.exit(0 if d else 1)"; then
	step_ok "list --json"
else
	step_fail "list --json"
fi

echo ""
echo "== kmang verify (all installed) =="
if kmang verify; then
	step_ok "verify"
else
	step_fail "verify"
fi

echo ""
echo "== kmang resolve (poweron boot path) =="
resolved="$(kmang resolve 2>/dev/null || true)"
if [ -n "$resolved" ] && [ -f "$resolved" ]; then
	step_ok "resolve → $resolved"
else
	step_fail "resolve (missing or not a file: ${resolved:-empty})"
fi

echo ""
echo "== kmang info (default kernel) =="
if [ -n "$resolved" ] && kmang info --json >/tmp/ir0-kmang-info.$$.json 2>&1; then
	step_ok "info --json"
else
	step_fail "info --json"
	cat /tmp/ir0-kmang-info.$$.json 2>/dev/null || true
fi
rm -f /tmp/ir0-kmang-info.$$.json

if [ "$FAIL" -ne 0 ]; then
	echo "✗ release-check-kmang FAILED"
	exit 1
fi
echo "✓ release-check-kmang OK"
