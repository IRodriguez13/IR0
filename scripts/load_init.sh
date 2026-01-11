#!/bin/bash

# load_init.sh - Load Init binary into virtual disk image
# Usage: load_init.sh [filesystem] [disk_image] [init_binary]
# Defaults: filesystem=auto-detect, disk_image=disk.img, init_binary=setup/pid1/init
# Supported filesystems: minix, fat32, ext4

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
KERNEL_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

# Source helper functions
source "$SCRIPT_DIR/disk_utils.sh" 2>/dev/null || {
    echo "❌ Error: Cannot load disk_utils.sh"
    exit 1
}

# Parse arguments
FS_TYPE=""
DISK_IMAGE=""
INIT_BINARY=""

# Check for help flag
if [ "$1" = "-h" ] || [ "$1" = "--help" ] || [ "$1" = "help" ]; then
    echo "Usage: load_init.sh [FILESYSTEM] [DISK_IMAGE] [INIT_BINARY]"
    echo ""
    echo "Load Init binary into virtual disk image for IR0 kernel."
    echo ""
    echo "Positional arguments:"
    echo "  FILESYSTEM             Filesystem type (minix, fat32, ext4) [default: auto-detect from filename]"
    echo "  DISK_IMAGE             Disk image filename [default: disk.img]"
    echo "  INIT_BINARY            Path to Init binary [default: setup/pid1/init]"
    echo ""
    echo "Options:"
    echo "  -h, --help             Show this help message"
    echo ""
    echo "Supported filesystems:"
    echo "  minix                  MINIX filesystem (default for disk.img)"
    echo "  fat32                  FAT32 filesystem (uses vfat mount type)"
    echo "  ext4                   ext4 filesystem"
    echo ""
    echo "Examples:"
    echo "  ./scripts/load_init.sh                                    # Auto-detect, use disk.img"
    echo "  ./scripts/load_init.sh fat32                              # Use fat32.img"
    echo "  ./scripts/load_init.sh disk.img                           # Use disk.img, auto-detect FS"
    echo "  ./scripts/load_init.sh fat32 fat32.img                    # Explicit filesystem and disk"
    echo "  ./scripts/load_init.sh fat32 fat32.img /path/to/init      # All parameters"
    echo ""
    echo "Note: This script requires root privileges (sudo) to mount filesystems."
    echo "      The disk image must be formatted before loading Init."
    exit 0
fi

if [ $# -eq 0 ]; then
    # No arguments: use defaults
    DISK_IMAGE="disk.img"
    INIT_BINARY="$KERNEL_ROOT/setup/pid1/init"
elif [ $# -eq 1 ]; then
    # One argument: could be filesystem or disk_image
    if [[ "$1" == *.img ]]; then
        # Looks like a disk image filename
        DISK_IMAGE="$1"
        INIT_BINARY="$KERNEL_ROOT/setup/pid1/init"
    else
        # Assume it's a filesystem type
        FS_TYPE="$1"
        DISK_IMAGE=$(get_disk_filename "$FS_TYPE")
        INIT_BINARY="$KERNEL_ROOT/setup/pid1/init"
    fi
elif [ $# -eq 2 ]; then
    # Two arguments: filesystem and disk_image, or disk_image and init_binary
    if [[ "$1" == *.img ]] || [[ "$2" == *.img ]]; then
        # One is disk_image, other is init_binary
        DISK_IMAGE="$1"
        INIT_BINARY="$2"
    else
        # filesystem and disk_image
        FS_TYPE="$1"
        DISK_IMAGE="$2"
        INIT_BINARY="$KERNEL_ROOT/setup/pid1/init"
    fi
elif [ $# -eq 3 ]; then
    # Three arguments: filesystem, disk_image, init_binary
    FS_TYPE="$1"
    DISK_IMAGE="$2"
    INIT_BINARY="$3"
else
    echo "❌ Error: Too many arguments"
    echo "Usage: load_init.sh [filesystem] [disk_image] [init_binary]"
    echo "Examples:"
    echo "  load_init.sh                           # Auto-detect, use disk.img"
    echo "  load_init.sh fat32                     # Use fat32.img"
    echo "  load_init.sh disk.img                  # Use disk.img, auto-detect FS"
    echo "  load_init.sh fat32 fat32.img           # Explicit filesystem and disk"
    echo "  load_init.sh fat32 fat32.img /path/init  # All parameters"
    exit 1
fi

# Set defaults if not provided
if [ -z "$DISK_IMAGE" ]; then
    DISK_IMAGE="disk.img"
fi
if [ -z "$INIT_BINARY" ]; then
    INIT_BINARY="$KERNEL_ROOT/setup/pid1/init"
fi

# Auto-detect filesystem if not provided
if [ -z "$FS_TYPE" ]; then
    FS_TYPE=$(detect_filesystem_from_filename "$DISK_IMAGE")
    if [ -z "$FS_TYPE" ]; then
        echo "❌ Error: Cannot auto-detect filesystem from filename: $DISK_IMAGE"
        echo "   Please specify filesystem explicitly: load_init.sh <filesystem> $DISK_IMAGE"
        echo "   Supported filesystems: minix, fat32, ext4"
        exit 1
    fi
fi

MOUNT_POINT="/mnt/ir0_init"

# Validate disk filename for security
if ! validate_disk_filename "$DISK_IMAGE"; then
    exit 1
fi

# Check if disk image exists
if [ ! -f "$DISK_IMAGE" ]; then
    echo "❌ Error: Disk image not found: $DISK_IMAGE"
    echo "   Create it first with: make create-disk $FS_TYPE"
    exit 1
fi

# Check if init binary exists
if [ ! -f "$INIT_BINARY" ]; then
    echo "❌ Error: Init binary not found: $INIT_BINARY"
    echo "   Expected location: $KERNEL_ROOT/setup/pid1/init"
    echo "   Compile Init in its repository and place the binary at: setup/pid1/init"
    exit 1
fi

# Check if init binary is executable
if [ ! -x "$INIT_BINARY" ]; then
    echo "⚠️  Warning: Init binary is not executable. Attempting to chmod +x..."
    chmod +x "$INIT_BINARY" || {
        echo "❌ Error: Cannot make init binary executable"
        exit 1
    }
fi

# Check if running as root (required for mount)
if [ "$EUID" -ne 0 ]; then
    echo "❌ Error: This script requires root privileges to mount filesystems"
    echo "   Run with: sudo $0 $*"
    exit 1
fi

# Check filesystem support
if ! check_filesystem_support "$FS_TYPE"; then
    exit 1
fi

# Get mount type
MOUNT_TYPE=$(get_mount_type "$FS_TYPE")
if [ -z "$MOUNT_TYPE" ]; then
    echo "❌ Error: Invalid filesystem type: $FS_TYPE"
    exit 1
fi

# Create mount point
mkdir -p "$MOUNT_POINT" || {
    echo "❌ Error: Cannot create mount point: $MOUNT_POINT"
    exit 1
}

# Cleanup function
LOOP_DEV=""
cleanup() {
    # Unmount if mounted
    if mountpoint -q "$MOUNT_POINT" 2>/dev/null; then
        umount "$MOUNT_POINT" 2>/dev/null || true
    fi
    # Cleanup loop device if used
    if [ -n "$LOOP_DEV" ] && [ -b "$LOOP_DEV" ]; then
        losetup -d "$LOOP_DEV" 2>/dev/null || true
    fi
    # Remove mount point (only if we created it)
    rmdir "$MOUNT_POINT" 2>/dev/null || true
}

# Set trap for cleanup on exit
trap cleanup EXIT

# Format disk if not formatted
if ! format_disk_if_needed "$DISK_IMAGE" "$FS_TYPE" "$MOUNT_TYPE"; then
    echo "❌ Error: Failed to format or verify disk filesystem"
    exit 1
fi

# Mount the disk image
echo "🔧 Mounting $FS_TYPE filesystem: $DISK_IMAGE"
if ! mount -o loop -t "$MOUNT_TYPE" "$DISK_IMAGE" "$MOUNT_POINT" 2>/dev/null; then
    # Try with explicit loop device
    LOOP_DEV=$(losetup --find --show "$DISK_IMAGE" 2>/dev/null)
    if [ -z "$LOOP_DEV" ]; then
        echo "❌ Error: Cannot setup loop device for $DISK_IMAGE"
        exit 1
    fi
    if ! mount -t "$MOUNT_TYPE" "$LOOP_DEV" "$MOUNT_POINT" 2>/dev/null; then
        echo "❌ Error: Cannot mount $FS_TYPE filesystem"
        exit 1
    fi
fi

# Create /sbin directory if it doesn't exist
if [ ! -d "$MOUNT_POINT/sbin" ]; then
    echo "📁 Creating /sbin directory..."
    mkdir -p "$MOUNT_POINT/sbin" || {
        echo "❌ Error: Cannot create /sbin directory"
        exit 1
    }
fi

# Copy init binary
echo "📦 Copying Init binary to /sbin/init..."
cp "$INIT_BINARY" "$MOUNT_POINT/sbin/init" || {
    echo "❌ Error: Cannot copy init binary"
    exit 1
}

# Make sure it's executable
chmod +x "$MOUNT_POINT/sbin/init" || {
    echo "⚠️  Warning: Cannot set executable permissions on /sbin/init"
}

# Verify the file was copied
if [ -f "$MOUNT_POINT/sbin/init" ]; then
    FILE_SIZE=$(stat -c%s "$MOUNT_POINT/sbin/init" 2>/dev/null || stat -f%z "$MOUNT_POINT/sbin/init" 2>/dev/null)
    echo "✅ Init binary loaded successfully"
    echo "   Filesystem: $FS_TYPE"
    echo "   Location: /sbin/init"
    echo "   Size: $FILE_SIZE bytes"
    echo ""
    echo "🚀 Kernel can now execute: execve(\"/sbin/init\", argv, envp)"
else
    echo "❌ Error: Init binary verification failed"
    exit 1
fi
