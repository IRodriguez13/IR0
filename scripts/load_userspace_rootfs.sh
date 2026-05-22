#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-only
#
# Inject /sbin/init and /bin/sh into MINIX disk.img without mounting.
# Usage: load_userspace_rootfs.sh [disk_image] [init_binary] [sh_binary]

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
KERNEL_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

DISK_IMAGE="${1:-disk.img}"
INIT_BINARY="${2:-$KERNEL_ROOT/setup/pid1/init}"
SH_BINARY="${3:-$KERNEL_ROOT/setup/pid1/sh_smoke}"

if [ ! -f "$DISK_IMAGE" ]; then
	echo "❌ Disk image not found: $DISK_IMAGE"
	exit 1
fi
if [ ! -f "$INIT_BINARY" ]; then
	echo "❌ Init binary not found: $INIT_BINARY"
	exit 1
fi
if [ ! -f "$SH_BINARY" ]; then
	echo "❌ Shell binary not found: $SH_BINARY"
	exit 1
fi

echo "📦 Injecting userspace rootfs into MINIX image (no mount)..."
python3 "$SCRIPT_DIR/inject_init_minix.py" "$DISK_IMAGE" "$INIT_BINARY" sbin/init
python3 "$SCRIPT_DIR/inject_init_minix.py" "$DISK_IMAGE" "$SH_BINARY" bin/sh
echo "✅ Userspace rootfs ready: /sbin/init + /bin/sh"
