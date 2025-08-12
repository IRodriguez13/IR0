#!/bin/bash
# IR0 Multi-Architecture Build System

clear
echo "╔══════════════════════════════════════╗"
echo "║      IR0 Kernel Build Strategies     ║"
echo "╚══════════════════════════════════════╝"
echo ""
echo "Architecture:"
echo "1) x86 32-bit (i386)"
echo "2) x86 64-bit (x86_64)"
echo ""
echo "Target Configuration:"
echo "3) Desktop 32-bit"
echo "4) Desktop 64-bit"
echo "5) Server 32-bit"
echo "6) Server 64-bit"
echo "7) IoT 32-bit (ARM)"
echo ""

read -p "Choose option [1-7]: " option

case $option in
    1)
        echo "Building 32-bit kernel..."
        make ARCH=i386 all
        ;;
    2)
        echo "Building 64-bit kernel..."
        make ARCH=x86_64 all
        ;;
    3)
        echo "Building Desktop 32-bit..."
        make ARCH=i386 CONFIG=desktop all
        ;;
    4)
        echo "Building Desktop 64-bit..."
        make ARCH=x86_64 CONFIG=desktop all
        ;;
    5)
        echo "Building Server 32-bit..."
        make ARCH=i386 CONFIG=server all
        ;;
    6)
        echo "Building Server 64-bit..."
        make ARCH=x86_64 CONFIG=server all
        ;;
    7)
        echo "Building IoT 32-bit..."
        make ARCH=i386 CONFIG=iot all
        ;;
    *)
        echo "Invalid option"
        ;;
esac