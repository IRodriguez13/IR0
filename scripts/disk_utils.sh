#!/bin/bash

# disk_utils.sh - Helper functions for disk image management
# This file provides common functions used by create_disk.sh and delete_disk.sh

# Filesystem-specific defaults for disk sizes
declare -A FS_DEFAULTS
FS_DEFAULTS["minix"]="200"
FS_DEFAULTS["fat32"]="500"
FS_DEFAULTS["ext4"]="1000"

# Get default disk filename for a filesystem
# Usage: get_disk_filename [filesystem]
# Returns: filename (e.g., "disk.img" for minix, "fat32.img" for fat32)
get_disk_filename() {
    local fs_type="${1:-minix}"
    
    # Default is disk.img for minix (backward compatibility)
    if [ "$fs_type" = "minix" ]; then
        echo "disk.img"
    else
        # For other filesystems, use filesystem.img
        echo "${fs_type}.img"
    fi
}

# Detect filesystem type from disk filename
# Usage: detect_filesystem_from_filename filename
# Returns: filesystem type (minix, fat32, ext4) or empty string
detect_filesystem_from_filename() {
    local filename="$1"
    local basename=$(basename "$filename")
    
    # Check for explicit filesystem in filename
    if [[ "$basename" == fat32*.img ]] || [[ "$basename" == *fat32*.img ]]; then
        echo "fat32"
        return 0
    elif [[ "$basename" == ext4*.img ]] || [[ "$basename" == *ext4*.img ]]; then
        echo "ext4"
        return 0
    elif [[ "$basename" == minix*.img ]] || [[ "$basename" == *minix*.img ]]; then
        echo "minix"
        return 0
    elif [[ "$basename" == disk.img ]]; then
        # Default is minix for backward compatibility
        echo "minix"
        return 0
    fi
    
    # Could not detect, return empty
    echo ""
    return 1
}

# Check if filesystem support is available in kernel
# Usage: check_filesystem_support filesystem_type
# Returns: 0 if supported, 1 if not
check_filesystem_support() {
    local fs_type="$1"
    local mount_type=""
    
    # Map filesystem types to mount types
    case "$fs_type" in
        minix)
            mount_type="minix"
            ;;
        fat32)
            mount_type="vfat"  # FAT32 is mounted as vfat in Linux
            ;;
        ext4)
            mount_type="ext4"
            ;;
        *)
            echo "❌ Error: Unknown filesystem type: $fs_type"
            return 1
            ;;
    esac
    
    # Check if filesystem is available
    if ! grep -q "$mount_type" /proc/filesystems 2>/dev/null; then
        echo "❌ Error: $fs_type filesystem support not available in kernel"
        echo "   Load module with: sudo modprobe $mount_type"
        return 1
    fi
    
    return 0
}

# Get mount type for filesystem
# Usage: get_mount_type filesystem_type
# Returns: mount type (minix, vfat, ext4)
get_mount_type() {
    local fs_type="$1"
    
    case "$fs_type" in
        minix)
            echo "minix"
            ;;
        fat32)
            echo "vfat"  # FAT32 is mounted as vfat in Linux
            ;;
        ext4)
            echo "ext4"
            ;;
        *)
            echo ""
            return 1
            ;;
    esac
    
    return 0
}

# Format disk image if not formatted
# Usage: format_disk_if_needed disk_image filesystem_type mount_type
# Returns: 0 if formatted or already formatted, 1 on error
format_disk_if_needed() {
    local disk_image="$1"
    local fs_type="$2"
    local mount_type="$3"
    local temp_mount="/tmp/ir0_format_check_$$"
    
    # Create temporary mount point
    mkdir -p "$temp_mount" 2>/dev/null || {
        echo "❌ Error: Cannot create temporary mount point"
        return 1
    }
    
    # Try to mount (read-only to check if formatted)
    local mount_ok=0
    if mount -o loop,ro -t "$mount_type" "$disk_image" "$temp_mount" 2>/dev/null; then
        # Successfully mounted - filesystem is formatted
        umount "$temp_mount" 2>/dev/null
        mount_ok=1
    fi
    
    # Cleanup temp mount point
    rmdir "$temp_mount" 2>/dev/null || true
    
    if [ "$mount_ok" -eq 1 ]; then
        # Already formatted, nothing to do
        return 0
    fi
    
    # Not formatted, need to format
    echo "📝 Disk not formatted. Formatting $fs_type filesystem..."
    
    local mkfs_cmd=""
    case "$fs_type" in
        minix)
            mkfs_cmd="mkfs.minix"
            if ! command -v "$mkfs_cmd" >/dev/null 2>&1; then
                echo "❌ Error: $mkfs_cmd not found. Install util-linux package."
                return 1
            fi
            # mkfs.minix requires -1 flag for v1 format, or -2 for v2
            # Use v1 for compatibility
            if ! "$mkfs_cmd" -1 "$disk_image" 2>/dev/null; then
                echo "❌ Error: Failed to format MINIX filesystem"
                return 1
            fi
            ;;
        fat32)
            mkfs_cmd="mkfs.vfat"
            if ! command -v "$mkfs_cmd" >/dev/null 2>&1; then
                echo "❌ Error: $mkfs_cmd not found. Install dosfstools package."
                return 1
            fi
            # Use -F 32 for FAT32, -n for label
            if ! "$mkfs_cmd" -F 32 -n "IR0" "$disk_image" 2>/dev/null; then
                echo "❌ Error: Failed to format FAT32 filesystem"
                return 1
            fi
            ;;
        ext4)
            mkfs_cmd="mkfs.ext4"
            if ! command -v "$mkfs_cmd" >/dev/null 2>&1; then
                echo "❌ Error: $mkfs_cmd not found. Install e2fsprogs package."
                return 1
            fi
            # Use -F to force, -q for quiet
            if ! "$mkfs_cmd" -F -q "$disk_image" 2>/dev/null; then
                echo "❌ Error: Failed to format ext4 filesystem"
                return 1
            fi
            ;;
        *)
            echo "❌ Error: Unsupported filesystem type for formatting: $fs_type"
            return 1
            ;;
    esac
    
    echo "✅ Disk formatted successfully"
    return 0
}

# Validate disk filename to prevent dangerous operations
# Usage: validate_disk_filename filename
# Returns: 0 if valid, 1 if dangerous
validate_disk_filename() {
    local filename="$1"
    
    # Must not be empty
    if [ -z "$filename" ]; then
        echo "❌ Error: Disk filename cannot be empty"
        return 1
    fi
    
    # Must not be a device file (block device)
    if [ -b "$filename" ]; then
        echo "❌ Error: '$filename' is a block device. Refusing to operate on device files."
        return 1
    fi
    
    # Must not start with /dev/ (even if not a block device yet)
    if [[ "$filename" == /dev/* ]]; then
        echo "❌ Error: '$filename' is in /dev/. Refusing to operate on device files."
        return 1
    fi
    
    # Must not contain path traversal (..)
    if [[ "$filename" == *".."* ]]; then
        echo "❌ Error: '$filename' contains '..'. Path traversal not allowed."
        return 1
    fi
    
    # Must not be an absolute path to system directories
    local dangerous_paths=("/etc" "/boot" "/usr" "/bin" "/sbin" "/lib" "/sys" "/proc" "/root")
    for path in "${dangerous_paths[@]}"; do
        if [[ "$filename" == "$path"/* ]] || [[ "$filename" == "$path" ]]; then
            echo "❌ Error: '$filename' is in system directory '$path'. Refusing to operate."
            return 1
        fi
    done
    
    return 0
}

