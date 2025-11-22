#!/bin/sh

# deptest.sh - Dependency test for IR0 Kernel
# Checks all required tools and dependencies based on OS

ERRORS=0
WARNINGS=0

# Detect OS
if [ "$OS" = "Windows_NT" ] || [ -n "$WINDIR" ]; then
    OS_TYPE="windows"
else
    OS_TYPE="linux"
fi

echo "=========================================="
echo "IR0 Kernel - Dependency Test"
echo "OS: $OS_TYPE"
echo "=========================================="
echo ""

# Check NASM (Sorriso)
check_nasm() {
    if command -v nasm >/dev/null 2>&1; then
        VERSION=$(nasm -v 2>&1 | head -1)
        echo "✓ NASM found: $VERSION"
        return 0
    else
        echo "✗ NASM not found"
        echo "  Install: apt-get install nasm (Linux) or download from nasm.us (Windows)"
        return 1
    fi
}

# Check GCC compiler
check_gcc() {
    if command -v gcc >/dev/null 2>&1; then
        VERSION=$(gcc --version 2>&1 | head -1)
        echo "✓ GCC found: $VERSION"
        return 0
    else
        echo "✗ GCC not found"
        if [ "$OS_TYPE" = "windows" ]; then
            echo "  Install: MSYS2/MinGW-w64 or download from mingw-w64.org"
        else
            echo "  Install: apt-get install build-essential (Linux)"
        fi
        return 1
    fi
}

# Check linker (ld)
check_ld() {
    if command -v ld >/dev/null 2>&1; then
        VERSION=$(ld --version 2>&1 | head -1)
        echo "✓ Linker (ld) found: $VERSION"
        
        # Check if it supports ELF format
        if ld --help 2>&1 | grep -q "elf_x86_64\|elf64-x86-64"; then
            echo "  ELF format support: Yes"
            return 0
        else
            echo "  ⚠ ELF format support: Unknown (may need binutils)"
            WARNINGS=$((WARNINGS + 1))
            return 0
        fi
    else
        echo "✗ Linker (ld) not found"
        if [ "$OS_TYPE" = "windows" ]; then
            echo "  Install: MSYS2/MinGW-w64 binutils package"
            echo "  Paths checked: /usr/bin/ld.exe, /mingw64/bin/ld.exe"
        else
            echo "  Install: apt-get install binutils (Linux)"
        fi
        return 1
    fi
}

# Check QEMU
check_qemu() {
    if command -v qemu-system-x86_64 >/dev/null 2>&1; then
        VERSION=$(qemu-system-x86_64 --version 2>&1 | head -1)
        echo "✓ QEMU found: $VERSION"
        return 0
    else
        echo "✗ QEMU not found"
        if [ "$OS_TYPE" = "windows" ]; then
            echo "  Install: Download from qemu.org or use: pacman -S qemu (MSYS2)"
        else
            echo "  Install: apt-get install qemu-system-x86 (Linux)"
        fi
        return 1
    fi
}

# Check grub-mkrescue (for ISO creation)
check_grub() {
    if command -v grub-mkrescue >/dev/null 2>&1; then
        echo "✓ grub-mkrescue found"
        return 0
    else
        echo "⚠ grub-mkrescue not found (optional for ISO creation)"
        if [ "$OS_TYPE" = "windows" ]; then
            echo "  Install: pacman -S grub (MSYS2) or use WSL"
        else
            echo "  Install: apt-get install grub-pc-bin (Linux)"
        fi
        WARNINGS=$((WARNINGS + 1))
        return 0
    fi
}

# Check Python (for kconfig)
check_python() {
    if command -v python3 >/dev/null 2>&1; then
        VERSION=$(python3 --version 2>&1)
        echo "✓ Python3 found: $VERSION"
        return 0
    elif command -v python >/dev/null 2>&1; then
        VERSION=$(python --version 2>&1)
        echo "✓ Python found: $VERSION"
        return 0
    else
        echo "⚠ Python not found (optional for kconfig)"
        WARNINGS=$((WARNINGS + 1))
        return 0
    fi
}

# Windows-specific checks
check_windows_deps() {
    echo "Windows-specific dependencies:"
    
    # Check for MSYS2/MinGW paths
    if [ -f "/usr/bin/ld.exe" ] || [ -f "/mingw64/bin/ld.exe" ]; then
        echo "✓ MSYS2/MinGW environment detected"
        if [ -f "/usr/bin/ld.exe" ]; then
            echo "  ELF linker found at: /usr/bin/ld.exe"
        fi
        if [ -f "/mingw64/bin/ld.exe" ]; then
            echo "  ELF linker found at: /mingw64/bin/ld.exe"
        fi
    else
        echo "⚠ MSYS2/MinGW paths not found"
        echo "  Consider installing MSYS2 for full build support"
        WARNINGS=$((WARNINGS + 1))
    fi
    
    # Check for PowerShell (for date commands)
    if command -v powershell >/dev/null 2>&1; then
        echo "✓ PowerShell found"
    else
        echo "⚠ PowerShell not found (used for build timestamps)"
        WARNINGS=$((WARNINGS + 1))
    fi
}

# Linux-specific checks
check_linux_deps() {
    echo "Linux-specific dependencies:"
    
    # Check for make
    if command -v make >/dev/null 2>&1; then
        VERSION=$(make --version 2>&1 | head -1)
        echo "✓ Make found: $VERSION"
    else
        echo "✗ Make not found"
        echo "  Install: apt-get install build-essential"
        ERRORS=$((ERRORS + 1))
    fi
    
    # Check for dd (for disk creation)
    if command -v dd >/dev/null 2>&1; then
        echo "✓ dd found (for disk image creation)"
    else
        echo "✗ dd not found"
        echo "  Install: Should be included in coreutils"
        ERRORS=$((ERRORS + 1))
    fi
}

# Run checks
echo "Essential build tools:"
echo "----------------------"
check_nasm || ERRORS=$((ERRORS + 1))
check_gcc || ERRORS=$((ERRORS + 1))
check_ld || ERRORS=$((ERRORS + 1))
echo ""

echo "Runtime tools:"
echo "--------------"
check_qemu || ERRORS=$((ERRORS + 1))
check_grub
check_python
echo ""

if [ "$OS_TYPE" = "windows" ]; then
    check_windows_deps
else
    check_linux_deps
fi

echo ""
echo "=========================================="
if [ $ERRORS -eq 0 ]; then
    if [ $WARNINGS -eq 0 ]; then
        echo "✓ All essential dependencies found!"
        echo "  You can build the kernel with: make ir0"
        exit 0
    else
        echo "✓ Essential dependencies found ($WARNINGS warnings)"
        echo "  You can build the kernel, but some optional features may be unavailable"
        exit 0
    fi
else
    echo "✗ Missing $ERRORS essential dependency/dependencies"
    echo "  Please install the missing tools before building"
    exit 1
fi

