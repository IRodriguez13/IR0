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
    if command -v python3 > /dev/null 2>&1; then
        VERSION=$(python3 --version 2>&1)
        echo "✓ Python3 found: $VERSION"
        
        # Check tkinter
        if python3 -c "import tkinter" 2>/dev/null; then
            echo "  ✓ tkinter library found"
        else
            echo "  ⚠ tkinter library not found (required for menuconfig)"
            if [ "$OS_TYPE" = "linux" ]; then
                echo "    Install: apt-get install python3-tk"
            else
                echo "    Usually included with Python on Windows"
            fi
            WARNINGS=$((WARNINGS + 1))
        fi
        
        # Check PIL/Pillow (optional but recommended)
        if python3 -c "from PIL import Image" 2>/dev/null; then
            echo "  ✓ PIL/Pillow library found"
        else
            echo "  ⚠ PIL/Pillow not found (optional, for menuconfig images)"
            echo "    Install: pip3 install pillow"
            WARNINGS=$((WARNINGS + 1))
        fi
        
        return 0
    elif command -v python > /dev/null 2>&1; then
        VERSION=$(python --version 2>&1)
        echo "✓ Python found: $VERSION"
        WARNINGS=$((WARNINGS + 1))
        return 0
    else
        echo "⚠ Python not found (optional for menuconfig)"
        if [ "$OS_TYPE" = "linux" ]; then
            echo "  Install: apt-get install python3 python3-tk"
        else
            echo "  Install: Download from python.org"
        fi
        WARNINGS=$((WARNINGS + 1))
        return 0
    fi
}

# Check C++ compiler (REQUIRED for kernel components)
check_cpp() {
    if command -v g++ > /dev/null 2>&1; then
        VERSION=$(g++ --version 2>&1 | head -1)
        echo "✓ G++ (C++) found: $VERSION"
        return 0
    elif command -v clang++ > /dev/null 2>&1; then
        VERSION=$(clang++ --version 2>&1 | head -1)
        echo "✓ Clang++ (C++) found: $VERSION"
        return 0
    else
        echo "✗ C++ compiler not found (REQUIRED for kernel C++ components)"
        if [ "$OS_TYPE" = "windows" ]; then
            echo "  Install: MSYS2/MinGW-w64 (g++) or LLVM (clang++)"
            echo "  MSYS2: pacman -S mingw-w64-x86_64-gcc"
        else
            echo "  Install: apt-get install g++"
        fi
        return 1
    fi
}

# Check Rust compiler (REQUIRED for drivers)
# Check Rust compiler (REQUIRED for drivers)
check_rust() {
    if command -v rustc > /dev/null 2>&1; then
        VERSION=$(rustc --version 2>&1)
        echo "✓ Rust compiler found: $VERSION"
        
        # Check cargo
        if command -v cargo > /dev/null 2>&1; then
            CARGO_VERSION=$(cargo --version 2>&1)
            echo "  ✓ Cargo found: $CARGO_VERSION"
        else
            echo "  ✗ Cargo not found (package manager for Rust)"
            return 1
        fi
        
        # Check for nightly toolchain (REQUIRED for build-std)
        if rustup toolchain list 2>&1 | grep -q "nightly"; then
            echo "  ✓ Nightly toolchain found"
        else
            echo "  ✗ Nightly toolchain not found (REQUIRED for build-std)"
            echo "    Install: rustup toolchain install nightly"
            return 1
        fi

        # Check rust-src component
        if rustup component list --toolchain nightly 2>&1 | grep -q "rust-src (installed)"; then
            echo "  ✓ rust-src component installed (nightly)"
        else
            echo "  ✗ rust-src component not installed for nightly (needed for no_std)"
            echo "    Install: rustup component add rust-src --toolchain nightly"
            return 1
        fi
        
        return 0
    else
        echo "✗ Rust compiler not found (REQUIRED for Rust drivers)"
        echo "  Install: curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh"
        echo "  Or visit: https://www.rust-lang.org/tools/install"
        echo "  After install, run: rustup toolchain install nightly"
        echo "  And: rustup component add rust-src --toolchain nightly"
        return 1
    fi
}

# Check MinGW cross-compiler (Linux only)
check_mingw_cross() {
    if [ "$OS_TYPE" = "linux" ]; then
        echo ""
        echo "Cross-compilation tools (Windows targets):"
        echo "-------------------------------------------"
        echo "ℹ For cross-platform development, install MinGW to compile for Windows"
        echo ""
        
        # Check MinGW GCC
        if command -v x86_64-w64-mingw32-gcc > /dev/null 2>&1; then
            VERSION=$(x86_64-w64-mingw32-gcc --version 2>&1 | head -1)
            echo "✓ MinGW GCC found: $VERSION"
        else
            echo "⚠ MinGW GCC not found (for Windows cross-compilation)"
            echo "  Install: apt-get install mingw-w64"
            echo "  Enables: make unibuild -win <file>"
            WARNINGS=$((WARNINGS + 1))
        fi
        
        # Check MinGW G++
        if command -v x86_64-w64-mingw32-g++ > /dev/null 2>&1; then
            VERSION=$(x86_64-w64-mingw32-g++ --version 2>&1 | head -1)
            echo "✓ MinGW G++ found: $VERSION"
        else
            echo "⚠ MinGW G++ not found (for Windows C++ cross-compilation)"
            echo "  Install: apt-get install mingw-w64"
            echo "  Enables: make unibuild -win -cpp <file>"
            WARNINGS=$((WARNINGS + 1))
        fi
        
        echo ""
        echo "ℹ Note: On Linux, you can compile native binaries AND cross-compile for Windows"
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

echo "Multi-language compilers (REQUIRED):"
echo "------------------------------------"
check_cpp || ERRORS=$((ERRORS + 1))
check_rust || ERRORS=$((ERRORS + 1))
check_mingw_cross
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

