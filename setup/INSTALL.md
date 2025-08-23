# IR0 Kernel - Installation Guide

This guide provides step-by-step instructions for setting up and building the IR0 kernel on different systems.

---

## 🚀 Quick Installation

### Ubuntu/Debian
```bash
# Install dependencies
sudo apt-get update
sudo apt-get install build-essential nasm grub-pc-bin xorriso qemu-system-x86

# Clone repository
git clone https://github.com/your-repo/ir0-kernel.git
cd ir0-kernel

# Build and run
make
make run
```

### Fedora/RHEL/CentOS
```bash
# Install dependencies
sudo dnf install gcc nasm grub2-tools xorriso qemu-system-x86

# Clone and build
git clone https://github.com/your-repo/ir0-kernel.git
cd ir0-kernel
make
make run
```

### Arch Linux
```bash
# Install dependencies
sudo pacman -S base-devel nasm grub xorriso qemu-arch-extra

# Clone and build
git clone https://github.com/your-repo/ir0-kernel.git
cd ir0-kernel
make
make run
```

---

## 📋 System Requirements

### Minimum Requirements
- **CPU**: x86-64 compatible processor
- **RAM**: 512MB available memory
- **Storage**: 1GB free space
- **OS**: Linux (Ubuntu 18.04+, Fedora 28+, Arch Linux)

### Recommended Requirements
- **CPU**: Multi-core x86-64 processor
- **RAM**: 2GB+ available memory
- **Storage**: 2GB+ free space
- **OS**: Ubuntu 20.04+ or equivalent

### Optional Requirements
- **ARM Support**: Cross-compilation tools for ARM development
- **GUI**: X11 or Wayland for QEMU display
- **Network**: Internet connection for dependency installation

---

## 🔧 Dependencies

### Required Dependencies

#### Build Tools
- **GCC**: GNU Compiler Collection (version 7.0+)
- **Make**: Build automation tool
- **NASM**: Netwide Assembler (version 2.13+)
- **LD**: GNU Linker

#### Boot Tools
- **GRUB**: GRand Unified Bootloader tools
- **Xorriso**: ISO creation tool

#### Emulation
- **QEMU**: Quick EMUlator for testing

### Optional Dependencies

#### ARM Cross-Compilation
- **gcc-aarch64-linux-gnu**: ARM64 cross-compiler
- **gcc-arm-linux-gnueabi**: ARM32 cross-compiler
- **qemu-system-arm**: ARM emulation

#### Development Tools
- **Git**: Version control
- **Valgrind**: Memory debugging (optional)
- **GDB**: Debugger (optional)

---

## 🛠️ Installation by Distribution

### Ubuntu 20.04+ / Debian 11+

```bash
# Update package list
sudo apt-get update

# Install build dependencies
sudo apt-get install -y \
    build-essential \
    nasm \
    grub-pc-bin \
    xorriso \
    qemu-system-x86 \
    git

# Install ARM support (optional)
sudo apt-get install -y \
    gcc-aarch64-linux-gnu \
    gcc-arm-linux-gnueabi \
    qemu-system-arm

# Verify installation
gcc --version
nasm --version
qemu-system-x86_64 --version
```

### Fedora 33+ / RHEL 8+ / CentOS 8+

```bash
# Install build dependencies
sudo dnf install -y \
    gcc \
    gcc-c++ \
    make \
    nasm \
    grub2-tools \
    xorriso \
    qemu-system-x86 \
    git

# Install ARM support (optional)
sudo dnf install -y \
    gcc-aarch64-linux-gnu \
    gcc-arm-linux-gnu \
    qemu-system-arm

# Verify installation
gcc --version
nasm --version
qemu-system-x86_64 --version
```

### Arch Linux

```bash
# Install build dependencies
sudo pacman -S --noconfirm \
    base-devel \
    nasm \
    grub \
    xorriso \
    qemu-arch-extra \
    git

# Install ARM support (optional)
sudo pacman -S --noconfirm \
    aarch64-linux-gnu-gcc \
    arm-linux-gnueabi-gcc

# Verify installation
gcc --version
nasm --version
qemu-system-x86_64 --version
```

### macOS (with Homebrew)

```bash
# Install Homebrew (if not installed)
/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"

# Install dependencies
brew install \
    gcc \
    nasm \
    xorriso \
    qemu

# Note: GRUB and ARM cross-compilation may not be available on macOS
# Consider using a Linux virtual machine for full functionality
```

---

## 🚀 Building the Kernel

### Basic Build

```bash
# Clone repository
git clone https://github.com/your-repo/ir0-kernel.git
cd ir0-kernel

# Build for default architecture (x86-32 desktop)
make

# Run in QEMU
make run
```

### Multi-Architecture Build

```bash
# Build for specific architecture and target
make ARCH=x86-64 BUILD_TARGET=desktop
make ARCH=x86-32 BUILD_TARGET=server
make ARCH=arm64 BUILD_TARGET=iot

# Build all combinations
./scripts/build_all.sh -c

# Build all architectures
./scripts/build_all.sh -a

# Build all targets
./scripts/build_all.sh -t
```

### Interactive Build

```bash
# Use interactive menu
./scripts/menu_builder.sh
```

---

## 🧪 Testing the Installation

### Quick Test

```bash
# Build and test basic functionality
make ARCH=x86-32 BUILD_TARGET=desktop
make ARCH=x86-32 BUILD_TARGET=desktop run
```

### Comprehensive Test

```bash
# Test all build combinations
./scripts/build_all.sh -c

# Test with debug output
make ARCH=x86-64 BUILD_TARGET=desktop debug

# Check generated files
ls -la kernel-*.bin kernel-*.iso
```

### ARM Testing (if installed)

```bash
# Install ARM dependencies
./scripts/install_arm_deps.sh

# Test ARM builds
make ARCH=arm32 BUILD_TARGET=iot
make ARCH=arm64 BUILD_TARGET=embedded

# Run ARM in QEMU
make ARCH=arm32 run
make ARCH=arm64 run
```

---

## 🔧 Troubleshooting

### Common Issues

#### 1. "Command not found" errors
```bash
# Check if dependencies are installed
which gcc
which nasm
which grub-mkrescue
which qemu-system-x86_64

# Reinstall if missing
sudo apt-get install build-essential nasm grub-pc-bin xorriso qemu-system-x86
```

#### 2. Build failures
```bash
# Clean and rebuild
make clean-all
make

# Check for specific errors
make ARCH=x86-64 BUILD_TARGET=desktop V=1
```

#### 3. QEMU display issues
```bash
# Try different display options
make ARCH=x86-64 BUILD_TARGET=desktop run

# Or run QEMU manually
qemu-system-x86_64 -cdrom kernel-x86-64-desktop.iso -m 512M -display gtk
```

#### 4. Permission issues
```bash
# Fix script permissions
chmod +x scripts/*.sh

# Run with proper permissions
./scripts/build_all.sh -c
```

### Debug Mode

```bash
# Enable verbose output
make ARCH=x86-64 BUILD_TARGET=desktop V=1

# Run with debug logging
make ARCH=x86-64 BUILD_TARGET=desktop debug

# Check debug logs
cat qemu_debug.log
```

### Getting Help

1. **Check Documentation**:
   - [README.md](README.md) - Main documentation
   - [BUILD_SYSTEM.md](BUILD_SYSTEM.md) - Build system details
   - [DEVELOPER_GUIDE.md](DEVELOPER_GUIDE.md) - Development guide

2. **Verify System**:
   ```bash
   # Check system information
   uname -a
   gcc --version
   nasm --version
   ```

3. **Test Dependencies**:
   ```bash
   # Test basic compilation
   echo 'int main() { return 0; }' > test.c
   gcc -o test test.c
   ./test
   ```

---

## 📦 Package Management

### Creating Packages

#### Debian Package
```bash
# Install packaging tools
sudo apt-get install devscripts debhelper

# Create package
debuild -us -uc
```

#### RPM Package
```bash
# Install packaging tools
sudo dnf install rpm-build rpmdevtools

# Create package
rpmbuild -ba ir0-kernel.spec
```

### Docker Support

```dockerfile
# Dockerfile for IR0 Kernel development
FROM ubuntu:20.04

RUN apt-get update && apt-get install -y \
    build-essential \
    nasm \
    grub-pc-bin \
    xorriso \
    qemu-system-x86 \
    git

WORKDIR /ir0-kernel
COPY . .

RUN make ARCH=x86-64 BUILD_TARGET=desktop

CMD ["make", "run"]
```

---

## 🔄 Updating

### Update Dependencies
```bash
# Ubuntu/Debian
sudo apt-get update && sudo apt-get upgrade

# Fedora/RHEL/CentOS
sudo dnf update

# Arch Linux
sudo pacman -Syu
```

### Update Kernel Source
```bash
# Pull latest changes
git pull origin main

# Clean and rebuild
make clean-all
make
```

---

## 📚 Next Steps

After successful installation:

1. **Read Documentation**:
   - [README.md](README.md) - Overview and features
   - [DEVELOPER_GUIDE.md](DEVELOPER_GUIDE.md) - Development guide
   - [BUILD_SYSTEM.md](BUILD_SYSTEM.md) - Build system details

2. **Explore Examples**:
   ```bash
   # Try different build targets
   make ARCH=x86-64 BUILD_TARGET=server
   make ARCH=x86-32 BUILD_TARGET=iot
   make ARCH=x86-64 BUILD_TARGET=embedded
   ```

3. **Start Development**:
   - Modify kernel code
   - Add new features
   - Test changes with different architectures

4. **Join Community**:
   - Report issues
   - Contribute code
   - Share improvements

---

## 🙏 Support

If you encounter issues:

1. **Check this guide** for common solutions
2. **Review documentation** in the project
3. **Search existing issues** on GitHub
4. **Create a new issue** with detailed information

### Issue Template
When reporting issues, include:
- Operating system and version
- Dependency versions (gcc, nasm, qemu)
- Complete error messages
- Steps to reproduce
- Expected vs actual behavior

---

*Happy kernel development! 🚀*
