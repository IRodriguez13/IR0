#!/bin/bash

# IR0 Kernel - Userspace Testing Script

set -e

echo "🔧 Building userspace programs..."
cd userspace/programs
make clean
make all
cd ../..

echo "📦 Building kernel..."
make clean
make ir0

echo "🚀 Testing kernel with userspace programs..."
echo "Starting QEMU - press Ctrl+C to stop"
echo "In the shell, try: exec /bin/hello"

make run-debug