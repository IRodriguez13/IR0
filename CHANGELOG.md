# Changelog

All notable changes to the IR0 Kernel project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [0.0.1-pre.1] - 2025-11-23

### Added

#### Multi-Language Development Support
- **Rust support for device drivers**
  - Complete FFI bindings in `rust/ffi/kernel.rs`
  - Safe wrappers for kernel functions (kmalloc, kfree, print, panic)
  - Driver interface with `DriverOps` structure
  - Support for `#![no_std]` freestanding drivers
  - Rust macros: `check_ptr!`, `kernel_assert!`
  
- **C++ support for kernel components**
  - Compatibility layer in `cpp/include/compat.h`
  - Custom C++ runtime in `cpp/runtime/compat.cpp`
  - Freestanding C++ with custom new/delete operators
  - Support for templates, RAII, namespaces
  - No exceptions, no RTTI (kernel-safe C++)

- **Critical debugging macros system** (`includes/ir0/critical.h`)
  - Unified debugging macros with precise error location
  - General macros: `CHECK_PTR`, `CHECK_RANGE`, `CHECK_BOUNDS`, `CHECK_SIZE`, `CHECK_ALIGN`
  - Runtime assertions: `VERIFY`, `NOT_REACHED`, `TODO_IMPLEMENT`
  - Subsystem-specific macros: `FS_CHECK_*`, `MEM_CHECK_*`, `SCHED_CHECK_*`, `DRV_CHECK_*`
  - Compatible with C, C++, and Rust

#### Build System Enhancements
- **Unibuild multi-language flags** 
  - `-cpp` flag for C++ compilation
  - `-rust` flag for Rust compilation  
  - `-win` flag for Windows cross-compilation
  - Combined flags support: `-win -cpp`, `-win -rust`
  - Automatic compiler selection based on flags

- **Enhanced dependency testing** (`scripts/deptest.sh`)
  - **MANDATORY compiler checks**: C++, Rust now required
  - Platform-specific detection (Linux/Windows)
  - Cross-compilation tools verification (MinGW on Linux)
  - Rust components check (rust-src, cargo)
  - Python dependencies (tkinter, PIL/Pillow)
  - Detailed installation instructions per platform
  - Cross-platform development guidance

#### Documentation
- **Comprehensive CONTRIBUTING.md** (300+ lines)
  - Complete Rust driver development guide
  - Detailed C++ kernel components guide
  - Practical code examples (functional drivers/components)
  - Setup instructions for all languages
  - Code style guidelines (C/Rust/C++)
  - Testing and debugging guidelines

- **Language Support Roadmap** (`docs/LANGUAGE_SUPPORT_ROADMAP.md`)
  - Multi-language architecture diagram
  - Usage guidelines per language
  - Implementation phases
  - FFI examples
  - Best practices

- **Wiki Update Document** (`docs/WIKI_UPDATE.md`)
  - Complete feature documentation for wiki migration
  - Quick start guides
  - Code examples
  - Migration notes

#### Menuconfig Improvements
- **About IR0 dialog**
  - Integrated kernel logo (black_hole_logo.png)
  - Complete kernel information display
  - Clickable links to GitHub repository and documentation wiki
  - Image support (PNG, JPG, GIF, BMP)
  - Automatic image resizing

#### Project Structure
- New `rust/` directory for Rust infrastructure
  - `rust/ffi/kernel.rs` - FFI bindings
  - `rust/ffi/README.md` - Documentation
  
- New `cpp/` directory for C++ infrastructure
  - `cpp/include/compat.h` - Compatibility header
  - `cpp/runtime/compat.cpp` - C++ runtime
  - `cpp/README.md` - Documentation

- GPL-3.0 license headers on all infrastructure files

### Changed

- **deptest.sh**: C++ and Rust compilers now mandatory (were optional)
- **deptest.sh**: Enhanced cross-compilation messaging
- **deptest.sh**: Improved error vs warning classification
- **Menuconfig**: About dialog now shows repository/wiki links in text
- **Build system**: Better support for multi-language compilation

### Fixed

- MinGW version display fix in deptest (was showing `$CARGO_VERSION` instead of actual version)
- Improved platform detection reliability
- Cross-compilation tool detection accuracy

### Documentation

- All new infrastructure files include GPL-3.0 headers
- Comprehensive developer guides for Rust and C++
- Updated README examples
- Cross-platform development instructions

### Developer Experience

- Simplified multi-language workflow
- Clear dependency verification
- Detailed error messages with solutions
- Platform-specific guidance
- Professional documentation

---

## Previous Commits Context

### Recent Development (before v0.0.1-pre.1)

- `d7b1736` - Better debug wrapping with macros
- `8405c1b` - Fixes en comandos básicos
- `6df24f6` - Enhanced unibuild system and redesigned menuconfig
- `297689b` - Merge from mainline to stabilization branch
- `b8ce406` - Added Windows support, unibuild system
- `a210b60` - MINIX filesystem layout and path parsing fixes
- `9f0a0a0` - Keyboard CTRL+L clear screen shortcut
- `f311821` - Improved error handling and process scheduling
- `4aa5147` - VGA and panic subsystems reorganization
- `d32c819` - Real lsblk command with ATA device detection

---

## Upgrade Notes

### For Existing Developers

**New Requirements:**
```bash
# Install Rust (required)
curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh
rustup component add rust-src

# Install C++ compiler (required)
sudo apt-get install g++

# Verify all dependencies
make deptest
```

**No Breaking Changes:**
- Existing C code continues to work unchanged
- Build system is backward compatible
- New languages are additive, not replacements

### For New Developers

Start with:
```bash
git clone https://github.com/IRodriguez13/IR0
cd IR0
make deptest
```

Read:
- `CONTRIBUTING.md` - Complete development guide
- `docs/LANGUAGE_SUPPORT_ROADMAP.md` - Architecture overview
- `docs/WIKI_UPDATE.md` - Feature documentation

---

## Links

- **Repository**: https://github.com/IRodriguez13/IR0
- **Wiki**: https://ir0-wiki.netlify.app/
- **License**: GPL-3.0-only

---

*This release marks the foundation for multi-language kernel development in IR0.*
