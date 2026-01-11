#!/bin/bash

# create_disk.sh - Create virtual disk for IR0 filesystems
# Usage: create_disk.sh [OPTIONS]
# Options:
#   -f, --filesystem FS    Filesystem type (minix, fat32, ext4, etc.) [default: minix]
#   -s, --size SIZE        Disk size in MB [default: 200]
#   -o, --output FILE      Output disk image filename [default: disk.img]
#   -h, --help             Show this help message

# Source helper functions
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/disk_utils.sh" 2>/dev/null || {
    echo "❌ Error: Cannot load disk_utils.sh"
    exit 1
}

# Default values
FS_TYPE="minix"
DISK_SIZE_MB=200
DISK_FILE=""

# Parse command line arguments
# Support both positional arguments and flags for flexibility
POSITIONAL_ARGS=()

while [[ $# -gt 0 ]]; do
    case $1 in
        -f|--filesystem)
            FS_TYPE="$2"
            shift 2
            ;;
        -s|--size)
            DISK_SIZE_MB="$2"
            shift 2
            ;;
        -o|--output)
            DISK_FILE="$2"
            shift 2
            ;;
        -h|--help)
            echo "Usage: create_disk.sh [FILESYSTEM] [SIZE] [OPTIONS]"
            echo ""
            echo "Create a virtual disk image for IR0 kernel filesystems."
            echo ""
            echo "Positional arguments:"
            echo "  FILESYSTEM             Filesystem type (minix, fat32, ext4, etc.) [default: minix]"
            echo "  SIZE                   Disk size in MB [default: 200, or FS-specific default]"
            echo ""
            echo "Options:"
            echo "  -f, --filesystem FS    Filesystem type (alternative to positional)"
            echo "  -s, --size SIZE        Disk size in MB (alternative to positional)"
            echo "  -o, --output FILE      Output disk image filename [default: disk.img]"
            echo "  -h, --help             Show this help message"
            echo ""
            echo "Examples:"
            echo "  ./scripts/create_disk.sh                          # Create 200MB MINIX disk (default)"
            echo "  ./scripts/create_disk.sh minix 500                # Create 500MB MINIX disk"
            echo "  ./scripts/create_disk.sh fat32                    # Create FAT32 disk (default size)"
            echo "  ./scripts/create_disk.sh fat32 500                # Create 500MB FAT32 disk"
            echo "  ./scripts/create_disk.sh ext4 1000                # Create 1GB ext4 disk"
            echo "  ./scripts/create_disk.sh -f fat32 -s 500 -o fat.img  # Using flags"
            exit 0
            ;;
        -*)
            echo "❌ Unknown option: $1"
            echo "Use -h or --help for usage information"
            exit 1
            ;;
        *)
            POSITIONAL_ARGS+=("$1")
            shift
            ;;
    esac
done

# Process positional arguments (filesystem and optionally size)
if [ ${#POSITIONAL_ARGS[@]} -gt 0 ]; then
    FS_TYPE="${POSITIONAL_ARGS[0]}"
    if [ ${#POSITIONAL_ARGS[@]} -gt 1 ]; then
        DISK_SIZE_MB="${POSITIONAL_ARGS[1]}"
    fi
fi

# Set default disk filename if not explicitly specified
if [ -z "$DISK_FILE" ]; then
    DISK_FILE=$(get_disk_filename "$FS_TYPE")
fi

# Use filesystem-specific default size if size not explicitly specified and default exists
if [ "$DISK_SIZE_MB" = "200" ] && [ -n "${FS_DEFAULTS[$FS_TYPE]}" ]; then
    DISK_SIZE_MB="${FS_DEFAULTS[$FS_TYPE]}"
fi

# Validate disk filename for security
if ! validate_disk_filename "$DISK_FILE"; then
    exit 1
fi

echo "🔧 Creating virtual disk for $FS_TYPE filesystem..."

# Check if disk already exists
if [ -f "$DISK_FILE" ]; then
    echo "⚠️  Disk $DISK_FILE already exists."
    echo "   It will be backed up and recreated."
    BACKUP_FILE="${DISK_FILE}.backup.$(date +%s)"
    mv "$DISK_FILE" "$BACKUP_FILE"
    echo "   Backed up to: $BACKUP_FILE"
fi

# Create disk image using dd with status
echo "📦 Creating ${DISK_SIZE_MB}MB disk image..."
if command -v pv > /dev/null 2>&1; then
    # Use pv for progress bar if available
    dd if=/dev/zero bs=1M count=$DISK_SIZE_MB 2>/dev/null | pv -s ${DISK_SIZE_MB}M > "$DISK_FILE"
else
    # Fallback to dd without progress
    dd if=/dev/zero of="$DISK_FILE" bs=1M count=$DISK_SIZE_MB status=progress 2>&1
fi

if [ $? -eq 0 ] && [ -f "$DISK_FILE" ]; then
    ACTUAL_SIZE=$(du -h "$DISK_FILE" | cut -f1)
    echo "✅ Virtual disk created: $DISK_FILE (${DISK_SIZE_MB}MB, ${ACTUAL_SIZE})"
    echo "📝 Filesystem: $FS_TYPE"
    echo "   Note: The kernel will automatically format this as $FS_TYPE filesystem on first boot"
    echo ""
    echo "🚀 Ready to run: make run"
else
    echo "❌ Failed to create disk image"
    exit 1
fi