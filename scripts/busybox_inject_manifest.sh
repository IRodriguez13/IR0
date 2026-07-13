#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
#
# Inject BusyBox multicall once as /bin/busybox, then hardlink each
# required_applets.txt name to the same inode (BUSY-1).

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DISK="${1:?usage: busybox_inject_manifest.sh DISK [BUSYBOX_BIN] [MANIFEST]}"
BUSYBOX_BIN="${2:-${FASE50_BUSYBOX_BIN:-$ROOT/setup/pid1/fase50_busybox_real}}"
MANIFEST="${3:-$ROOT/setup/busybox/required_applets.txt}"
INJECT="python3 ${ROOT}/scripts/inject_init_minix.py"

if [[ ! -f "$DISK" ]]; then
	echo "✗ missing disk: $DISK" >&2
	exit 1
fi
if [[ ! -f "$BUSYBOX_BIN" ]]; then
	echo "✗ missing BusyBox: $BUSYBOX_BIN" >&2
	exit 1
fi
if [[ ! -f "$MANIFEST" ]]; then
	echo "✗ missing manifest: $MANIFEST" >&2
	exit 1
fi

chmod +x "${ROOT}/scripts/busybox_check_manifest.sh"
"${ROOT}/scripts/busybox_check_manifest.sh" "$BUSYBOX_BIN" "$MANIFEST"

echo "  BUSYBOX Injecting multicall → /bin/busybox + hardlink applets..."
$INJECT "$DISK" "$BUSYBOX_BIN" bin/busybox

paths=("/bin/busybox")
while IFS= read -r line || [[ -n "$line" ]]; do
	applet="${line%%#*}"
	applet="$(echo "$applet" | sed 's/^[[:space:]]*//;s/[[:space:]]*$//')"
	[[ -z "$applet" ]] && continue
	if [[ "$applet" == "busybox" ]]; then
		continue
	fi
	$INJECT --hardlink "$DISK" bin/busybox "bin/$applet"
	paths+=("/bin/$applet")
done < "$MANIFEST"

python3 "${ROOT}/scripts/verify_minix_rootfs.py" --gate "$DISK" "${paths[@]}"
echo "✓ busybox_inject_manifest OK (${#paths[@]} paths verified)"
