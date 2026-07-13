#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
#
# Install a frozen BusyBox .config for IR0 smokes/builds.
#
# Do NOT use script(1) or unbounded "yes | oldconfig" here — that pattern OOM'd
# a host (yes → script → oldconfig buffers without bound). Regenerate the frozen
# file offline: scripts/busybox_regenerate_defconfig.sh

set -euo pipefail

if [[ $# -ne 2 ]]; then
	echo "usage: $0 BUSYBOX_SRC CONFIG_FRAGMENT" >&2
	exit 2
fi

BUSYBOX_SRC="$1"
FRAG="$2"

if [[ ! -d "$BUSYBOX_SRC" ]] || [[ ! -f "$BUSYBOX_SRC/Makefile" ]]; then
	echo "busybox_apply_fragment: missing tree: $BUSYBOX_SRC" >&2
	exit 1
fi
if [[ ! -f "$FRAG" ]]; then
	echo "busybox_apply_fragment: missing fragment: $FRAG" >&2
	exit 1
fi

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
FRAG_ABS="$(cd "$(dirname "$FRAG")" && pwd)/$(basename "$FRAG")"
DEFCONFIG="${FRAG_ABS%.config}_defconfig"

if [[ ! -f "$DEFCONFIG" ]]; then
	echo "busybox_apply_fragment: missing frozen defconfig: $DEFCONFIG" >&2
	echo "  Run: scripts/busybox_regenerate_defconfig.sh $(realpath --relative-to="$ROOT" "$FRAG_ABS")" >&2
	exit 1
fi

cp -f "$DEFCONFIG" "${BUSYBOX_SRC}/.config"
