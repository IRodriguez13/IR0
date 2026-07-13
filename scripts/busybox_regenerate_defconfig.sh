#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
#
# Maintainer-only: rebuild setup/busybox/*_defconfig from a fragment.
# Bounded, no script(1). Run manually after editing *.config fragments.

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUSYBOX_SRC="${BUSYBOX_SRC:-$ROOT/setup/third-party/busybox-1.36.1}"
FRAG="${1:-$ROOT/setup/busybox/fase58_busybox.config}"
OLDCONFIG_TIMEOUT="${OLDCONFIG_TIMEOUT:-120}"

if [[ ! -f "$FRAG" ]]; then
	echo "usage: $0 [CONFIG_FRAGMENT]" >&2
	exit 2
fi
if [[ ! -d "$BUSYBOX_SRC" ]]; then
	echo "missing BUSYBOX_SRC=$BUSYBOX_SRC" >&2
	exit 1
fi

FRAG_ABS="$(cd "$(dirname "$FRAG")" && pwd)/$(basename "$FRAG")"
OUT="${FRAG_ABS%.config}_defconfig"

echo "  BUSYBOX regenerate $OUT from $(basename "$FRAG_ABS")"

make -C "$BUSYBOX_SRC" allnoconfig </dev/null >/dev/null 2>&1
CFG="${BUSYBOX_SRC}/.config"

while IFS= read -r line; do
	case "$line" in
	""|\#*) continue ;;
	esac
	sym="${line%%=*}"
	val="${line#*=}"
	case "$val" in
	y)
		sed -i "s/^# ${sym} is not set/${sym}=y/" "$CFG"
		if ! grep -q "^${sym}=y\$" "$CFG"; then
			if grep -q "^${sym}=" "$CFG"; then
				sed -i "s/^${sym}=.*/${sym}=y/" "$CFG"
			else
				echo "${sym}=y" >> "$CFG"
			fi
		fi
		;;
	n)
		sed -i "s/^${sym}=.*/# ${sym} is not set/" "$CFG"
		if ! grep -q "^# ${sym} is not set\$" "$CFG"; then
			echo "# ${sym} is not set" >> "$CFG"
		fi
		;;
	*)
		if grep -q "^${sym}=" "$CFG"; then
			sed -i "s|^${sym}=.*|${sym}=${val}|" "$CFG"
		else
			echo "${sym}=${val}" >> "$CFG"
		fi
		;;
	esac
done < "$FRAG_ABS"

timeout "$OLDCONFIG_TIMEOUT" bash -c 'yes "" | make -C "'"$BUSYBOX_SRC"'" oldconfig' \
	</dev/null >/dev/null 2>&1

cp -f "$CFG" "$OUT"
echo "✓ wrote $OUT ($(wc -l < "$OUT") lines)"
