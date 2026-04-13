#!/bin/bash

# remove_init.sh - Remove Init binary from virtual disk image
# Usage: remove_init.sh [filesystem] [disk_image]
# Defaults: filesystem=auto-detect, disk_image=disk.img

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
KERNEL_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

source "$SCRIPT_DIR/disk_utils.sh" 2>/dev/null || {
    echo "Error: Cannot load disk_utils.sh"
    exit 1
}

if [ "$1" = "-h" ] || [ "$1" = "--help" ] || [ "$1" = "help" ]; then
    echo "Usage: remove_init.sh [FILESYSTEM] [DISK_IMAGE]"
    echo ""
    echo "Remove Init binary from virtual disk image for IR0 kernel."
    echo ""
    echo "Positional arguments:"
    echo "  FILESYSTEM             Filesystem type (minix, fat32, ext4) [default: auto-detect]"
    echo "  DISK_IMAGE             Disk image filename [default: disk.img]"
    echo ""
    echo "Examples:"
    echo "  ./scripts/remove_init.sh                    # Auto-detect, use disk.img"
    echo "  ./scripts/remove_init.sh fat32              # Use fat32.img"
    echo "  ./scripts/remove_init.sh fat32 fat32.img    # Explicit filesystem and disk"
    echo ""
    echo "Note: This script requires root privileges (sudo) to mount filesystems."
    exit 0
fi

FS_TYPE=""
DISK_IMAGE=""

if [ $# -eq 0 ]; then
    DISK_IMAGE="disk.img"
elif [ $# -eq 1 ]; then
    if [[ "$1" == *.img ]]; then
        DISK_IMAGE="$1"
    else
        FS_TYPE="$1"
        DISK_IMAGE=$(get_disk_filename "$FS_TYPE")
    fi
elif [ $# -eq 2 ]; then
    FS_TYPE="$1"
    DISK_IMAGE="$2"
else
    echo "Error: Too many arguments"
    echo "Usage: remove_init.sh [filesystem] [disk_image]"
    exit 1
fi

if [ -z "$DISK_IMAGE" ]; then
    DISK_IMAGE="disk.img"
fi

if [ -z "$FS_TYPE" ]; then
    FS_TYPE=$(detect_filesystem_from_filename "$DISK_IMAGE")
    if [ -z "$FS_TYPE" ]; then
        echo "Error: Cannot auto-detect filesystem from filename: $DISK_IMAGE"
        echo "   Please specify filesystem explicitly: remove_init.sh <filesystem> $DISK_IMAGE"
        exit 1
    fi
fi

MOUNT_POINT="/mnt/ir0_init"

if ! validate_disk_filename "$DISK_IMAGE"; then
    exit 1
fi

if [ ! -f "$DISK_IMAGE" ]; then
    echo "Error: Disk image not found: $DISK_IMAGE"
    exit 1
fi

if [ "$EUID" -ne 0 ]; then
    echo "Error: This script requires root privileges"
    echo "   Run with: sudo $0 $*"
    exit 1
fi

MOUNT_TYPE=$(get_mount_type "$FS_TYPE")
if [ -z "$MOUNT_TYPE" ]; then
    echo "Error: Invalid filesystem type: $FS_TYPE"
    exit 1
fi

mkdir -p "$MOUNT_POINT" || {
    echo "Error: Cannot create mount point: $MOUNT_POINT"
    exit 1
}

LOOP_DEV=""
cleanup() {
    if mountpoint -q "$MOUNT_POINT" 2>/dev/null; then
        umount "$MOUNT_POINT" 2>/dev/null || true
    fi
    if [ -n "$LOOP_DEV" ] && [ -b "$LOOP_DEV" ]; then
        losetup -d "$LOOP_DEV" 2>/dev/null || true
    fi
    rmdir "$MOUNT_POINT" 2>/dev/null || true
}

trap cleanup EXIT

echo "Mounting $FS_TYPE filesystem: $DISK_IMAGE"
if ! mount -o loop -t "$MOUNT_TYPE" "$DISK_IMAGE" "$MOUNT_POINT" 2>/dev/null; then
    LOOP_DEV=$(losetup --find --show "$DISK_IMAGE" 2>/dev/null)
    if [ -z "$LOOP_DEV" ]; then
        echo "Error: Cannot setup loop device for $DISK_IMAGE"
        exit 1
    fi
    if ! mount -t "$MOUNT_TYPE" "$LOOP_DEV" "$MOUNT_POINT" 2>/dev/null; then
        echo "Error: Cannot mount $FS_TYPE filesystem"
        exit 1
    fi
fi

if [ -f "$MOUNT_POINT/sbin/init" ]; then
    rm -f "$MOUNT_POINT/sbin/init" || {
        echo "Error: Cannot remove /sbin/init"
        exit 1
    }
    echo "Init binary removed from $DISK_IMAGE:/sbin/init"
else
    echo "No Init binary found at /sbin/init in $DISK_IMAGE"
fi
