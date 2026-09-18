#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Atomically add/update the ISD desktop payload without replacing guest state.
set -euo pipefail

TREE="${IR0_ISD_ROOTFS:?IR0_ISD_ROOTFS is required}"
DISK="${IR0_MACHINE_DISK:?IR0_MACHINE_DISK is required}"
INJECT="${IR0_INJECT_TOOL:?IR0_INJECT_TOOL is required}"

if [ ! -f "$DISK" ]; then
	echo "✗ missing machine disk: $DISK" >&2
	exit 2
fi
if [ ! -d "$TREE" ]; then
	echo "✗ missing staged desktop rootfs: $TREE" >&2
	exit 2
fi

temporary="${DISK}.desktop-new.$$"
previous="${DISK}.previous"
trap 'rm -f "$temporary"' EXIT
cp --reflink=auto --sparse=always "$DISK" "$temporary"

inject_file()
{
	local path="$1"
	local source="${TREE}/${path}"
	local mode
	if [ ! -f "$source" ]; then
		echo "✗ desktop payload missing: $source" >&2
		exit 2
	fi
	mode=$(stat -c '%a' "$source")
	python3 "$INJECT" --mode "$mode" "$temporary" "$source" "$path"
}

inject_tree_files()
{
	local base="$1"
	local f rel
	[ -d "${TREE}/${base}" ] || return 0
	while IFS= read -r f; do
		rel="${f#${TREE}/}"
		inject_file "$rel"
	done < <(find "${TREE}/${base}" -type f | LC_ALL=C sort)
}

for path in \
	usr/bin/Xfbdev usr/bin/X usr/bin/xinit usr/bin/startx usr/bin/xauth \
	usr/bin/twm usr/bin/xterm usr/bin/xsetroot \
	usr/bin/xclock usr/bin/xeyes usr/bin/xlogo usr/bin/xcalc usr/bin/xmessage \
	etc/X11/xinit/xinitrc \
	usr/share/fonts/X11/misc/6x13.bdf \
	usr/share/fonts/X11/misc/cursor.bdf \
	usr/share/fonts/X11/misc/fonts.alias \
	usr/share/fonts/X11/misc/fonts.dir
do
	if [ "$path" = usr/bin/X ]; then
		# startx resolves /usr/bin/X; it must retain the same setuid-root
		# entry semantics as Xfbdev, not become a plain copied executable.
		python3 "$INJECT" --mode 04755 "$temporary" \
			"${TREE}/usr/bin/Xfbdev" "$path"
	else
		inject_file "$path"
	fi
done

# Session assets referenced by /etc/X11/xinit/xinitrc and twm defaults.
inject_tree_files etc/X11/twm
inject_tree_files usr/share/backgrounds
inject_tree_files usr/share/X11/app-defaults
for path in etc/profile etc/ashrc; do
	inject_file "$path"
done

# X11 uses well-known lock files here; this is a Unix ABI requirement, not an
# optional desktop preference.
python3 "$INJECT" --owner 0:0 --mode 01777 --chown "$temporary" tmp

# Keep one complete rootfs rollback. HOME and managed kernels are separate.
cp --reflink=auto --sparse=always "$DISK" "$previous"
mv "$temporary" "$DISK"
trap - EXIT
echo "✓ desktop userspace updated atomically: $DISK"
echo "  ROLLBACK $previous"
