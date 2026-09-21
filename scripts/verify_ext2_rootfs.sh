#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Offline EXT2 rootfs path checks (debugfs + e2fsck). STO-1 verification gate.
set -euo pipefail

if [ "$#" -lt 2 ]; then
	echo "usage: verify_ext2_rootfs.sh DISK.ext2 [PATH ...]" >&2
	exit 2
fi

disk="$1"
shift

if [ ! -f "$disk" ]; then
	echo "✗ missing disk: $disk" >&2
	exit 2
fi

command -v debugfs >/dev/null 2>&1 || {
	echo "✗ install e2fsprogs (debugfs)" >&2
	exit 1
}
command -v e2fsck >/dev/null 2>&1 || {
	echo "✗ install e2fsprogs (e2fsck)" >&2
	exit 1
}

for path in "$@"; do
	case "$path" in
	/*) ;;
	*) path="/${path}" ;;
	esac
	if ! debugfs -R "stat ${path}" "$disk" 2>/dev/null | grep -q 'Type:'; then
		echo "✗ EXT2 missing path: ${path} on ${disk}" >&2
		exit 1
	fi
done

if ! e2fsck -fn "$disk" >/dev/null 2>&1; then
	echo "✗ EXT2 e2fsck failed: $disk" >&2
	exit 1
fi

echo "✓ verify_ext2_rootfs OK ($# paths)"
