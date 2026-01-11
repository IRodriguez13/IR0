#!/bin/bash

# delete_disk.sh - Delete virtual disk for IR0 filesystems
# Usage: delete_disk.sh [filesystem] [filename]
# Examples:
#   delete_disk.sh                    # Delete disk.img (default)
#   delete_disk.sh minix              # Delete disk.img
#   delete_disk.sh fat32              # Delete fat32.img
#   delete_disk.sh minix disk.img     # Delete disk.img explicitly
#   delete_disk.sh fat32 fat32.img    # Delete fat32.img explicitly

# Source helper functions
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/disk_utils.sh" 2>/dev/null || {
    echo "❌ Error: Cannot load disk_utils.sh"
    exit 1
}

# Parse arguments
FS_TYPE=""
DISK_FILE=""

if [ $# -eq 0 ]; then
    # No arguments: delete default disk.img
    DISK_FILE="disk.img"
elif [ $# -eq 1 ]; then
    # One argument: could be filesystem or filename
    if [[ "$1" == *.img ]] || [[ "$1" == *.* ]]; then
        # Looks like a filename
        DISK_FILE="$1"
    else
        # Assume it's a filesystem type
        FS_TYPE="$1"
        DISK_FILE=$(get_disk_filename "$FS_TYPE")
    fi
elif [ $# -eq 2 ]; then
    # Two arguments: filesystem and filename
    FS_TYPE="$1"
    DISK_FILE="$2"
else
    echo "❌ Error: Too many arguments"
    echo "Usage: delete_disk.sh [filesystem] [filename]"
    echo "Examples:"
    echo "  delete_disk.sh                    # Delete disk.img"
    echo "  delete_disk.sh fat32              # Delete fat32.img"
    echo "  delete_disk.sh fat32 fat32.img    # Delete fat32.img explicitly"
    exit 1
fi

# Validate disk filename for security
if ! validate_disk_filename "$DISK_FILE"; then
    exit 1
fi

# Delete the disk file
if [ -f "$DISK_FILE" ]; then
    rm -f "$DISK_FILE"
    echo "✓ Disk image removed: $DISK_FILE"
    exit 0
else
    echo "  Disk image not found: $DISK_FILE"
    exit 0
fi

