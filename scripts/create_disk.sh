#!/bin/bash

# create_disk.sh - Create virtual disk for IR0 MINIX filesystem

DISK_SIZE="100M"
DISK_FILE="disk.img"

echo "🔧 Creating virtual disk for MINIX filesystem..."

# Create empty disk image
if [ -f "$DISK_FILE" ]; then
    echo "⚠️  Disk $DISK_FILE already exists. Backing up..."
    mv "$DISK_FILE" "${DISK_FILE}.backup.$(date +%s)"
fi

# Create 100MB disk
dd if=/dev/zero of="$DISK_FILE" bs=1M count=100 2>/dev/null

if [ $? -eq 0 ]; then
    echo "✅ Virtual disk created: $DISK_FILE ($DISK_SIZE)"
    echo "📝 Note: The kernel will automatically format this as MINIX filesystem on first boot"
    echo ""
    echo "🚀 Ready to run: make run"
else
    echo "❌ Failed to create disk image"
    exit 1
fi