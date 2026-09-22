#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Tier 1.5 release-check: ISO integrity, kmang catalog, QEMU boot, guest probes.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

PROFILE="${PROFILE:-minimal}"
ARCH="${ISD_ARCH:-x86_64}"
RELEASE_CHECK_GUEST="${RELEASE_CHECK_GUEST:-1}"
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

echo "== release-check-boot (Tier 1.5) PROFILE=${PROFILE} GUEST=${RELEASE_CHECK_GUEST} =="

run_step "kernel-x64-userspace.iso" make -s kernel-x64-userspace.iso

echo ""
echo "== ISO embedded release =="
want="$(MAKEFLAGS= make -s -f Makefile -pn 2>/dev/null | sed -n 's/^IR0_VERSION_STRING := //p' | head -1)"
if [ -z "$want" ]; then
	step_fail "could not resolve IR0_VERSION_STRING from Makefile"
elif python3 - "$ROOT/kernel-x64-userspace.iso" "$want" <<'PY'
import subprocess
import sys
import tempfile
from pathlib import Path

iso = Path(sys.argv[1])
want = sys.argv[2]
with tempfile.TemporaryDirectory(prefix="ir0-iso-probe.") as tmp:
    kernel = Path(tmp) / "kernel.bin"
    r = subprocess.run(
        ["xorriso", "-osirrox", "on", "-indev", str(iso),
         "-extract", "/boot/kernel-x64.bin", str(kernel)],
        capture_output=True,
        text=True,
    )
    if r.returncode != 0:
        sys.exit(1)
    data = kernel.read_bytes()
    if f"IR0VER:{want}".encode() not in data:
        sys.exit(2)
PY
then
	step_ok "ISO kernel IR0VER:${want}"
else
	step_fail "ISO kernel missing IR0VER:${want}"
fi

KMANG_DIR="$(mktemp -d /tmp/ir0-kmang-release.XXXXXX)"
trap 'rm -rf "$KMANG_DIR"' EXIT

run_step "kmang CI pipeline" \
	env KMANG_DIR="$KMANG_DIR" PROFILE="$PROFILE" ISD_ARCH="$ARCH" \
		scripts/release_check_kmang.sh

run_step "kmang/usmang TUI (PTY)" \
	env IR0_ISD_ROOT="$ISD_ROOT" scripts/release_check_tui.sh

run_step "machine-create (empty IR0-machines)" \
	env IR0_MACHINE_ROOT="$(mktemp -d /tmp/ir0-machines-ci.XXXXXX)" \
		IR0_DEPS_INSTALL=never \
		make -s machine-create PROFILE="$PROFILE" ARCH="$ARCH" ISD_ARCH="$ARCH"

run_step "smoke-runit-boot" \
	make -s smoke-runit-boot-isd PROFILE="$PROFILE" ARCH="$ARCH" ISD_ARCH="$ARCH"

if [ "$RELEASE_CHECK_GUEST" = "1" ]; then
	run_step "release-check-guest-probes" \
		make -s release-check-guest-probes IR0_PRODUCT_PROFILE="$PROFILE" ARCH="$ARCH"
fi

echo ""
if [ "$FAIL" -ne 0 ]; then
	echo "✗ release-check-boot FAILED"
	exit 1
fi
echo "✓ release-check-boot OK PROFILE=${PROFILE}"
