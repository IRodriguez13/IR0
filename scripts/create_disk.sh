#!/bin/bash

# create_disk.sh - Create virtual disk for IR0 MINIX filesystem

DISK_SIZE_MB=200
DISK_FILE="disk.img"

echo "🔧 Creating virtual disk for MINIX filesystem..."

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
    echo "📝 Note: The kernel will automatically format this as MINIX filesystem on first boot"
    echo ""
    echo "🚀 Ready to run: make run"
else
    echo "❌ Failed to create disk image"
    exit 1
fi