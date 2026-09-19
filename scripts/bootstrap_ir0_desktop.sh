#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Ensure ../IR0-desktop exists (local bootstrap; no remote required).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DESKTOP="${IR0_DESKTOP_ROOT:-$ROOT/../IR0-desktop}"

if [ -f "$DESKTOP/smoke/run-xfbdev-smoke.sh" ]; then
	echo "✓ IR0-desktop already present at $DESKTOP"
	exit 0
fi

if [ -d "$DESKTOP" ] && [ ! -f "$DESKTOP/smoke/run-xfbdev-smoke.sh" ]; then
	echo "✗ $DESKTOP exists but is not a valid IR0-desktop tree" >&2
	exit 2
fi

echo "✗ IR0-desktop missing at $DESKTOP" >&2
echo "  Expected sibling checkout. If you have a remote, clone it there." >&2
echo "  Otherwise copy or symlink this bootstrap tree to $DESKTOP" >&2
exit 2
