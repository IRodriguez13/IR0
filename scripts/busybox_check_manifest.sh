#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
#
# Verify a BusyBox multicall binary provides every applet in
# setup/busybox/required_applets.txt (BUSY-1).

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUSYBOX_BIN="${1:-${FASE50_BUSYBOX_BIN:-$ROOT/setup/pid1/fase50_busybox_real}}"
MANIFEST="${2:-$ROOT/setup/busybox/required_applets.txt}"

if [[ ! -f "$BUSYBOX_BIN" ]]; then
	echo "✗ missing BusyBox binary: $BUSYBOX_BIN" >&2
	exit 1
fi
if [[ ! -f "$MANIFEST" ]]; then
	echo "✗ missing manifest: $MANIFEST" >&2
	exit 1
fi

TMPDIR="$(mktemp -d /tmp/ir0-bb-manifest.XXXXXX)"
cleanup() { rm -rf "$TMPDIR"; }
trap cleanup EXIT

ln -sf "$(realpath "$BUSYBOX_BIN")" "$TMPDIR/busybox"
LIST="$("$TMPDIR/busybox" --list 2>/dev/null | tr ' ,\t' '\n\n\n' | sed '/^$/d' | sort -u)"
if [[ -z "$LIST" ]]; then
	echo "✗ busybox --list empty (wrong binary or applet-as-argv0?)" >&2
	exit 1
fi

missing=0
while IFS= read -r line || [[ -n "$line" ]]; do
	applet="${line%%#*}"
	applet="$(echo "$applet" | sed 's/^[[:space:]]*//;s/[[:space:]]*$//')"
	[[ -z "$applet" ]] && continue
	if ! printf '%s\n' "$LIST" | grep -qx "$applet"; then
		echo "✗ manifest applet missing from --list: $applet" >&2
		missing=1
	fi
done < "$MANIFEST"

if [[ "$missing" -ne 0 ]]; then
	echo "✗ busybox_check_manifest FAILED" >&2
	echo "--- busybox --list ---" >&2
	printf '%s\n' "$LIST" >&2
	exit 1
fi

echo "✓ busybox_check_manifest OK ($(printf '%s\n' "$LIST" | wc -l) applets in binary; manifest covered)"
