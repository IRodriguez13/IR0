#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Product-wide 0.0.1 gate: every shipped profile, every interactive manager,
# and the architecture isolation boundary.  Deliberately heavier than CI fast.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

echo "== product gate: ISA isolation =="
make -s arch-guard isa-security-guard build-matrix-min

echo "== product gate: TUI truth =="
scripts/release_check_tui.sh

echo "== product gate: every shipped ISD profile boots =="
python3 scripts/init_boot_capture.py --matrix --arch "${ISD_ARCH:-x86_64}" \
	--smoke-only --timeout "${PRODUCT_BOOT_TIMEOUT:-90}" \
	--stale-sec "${PRODUCT_BOOT_STALE_SEC:-25}"

echo "✓ IR0/ISD product gate OK"
