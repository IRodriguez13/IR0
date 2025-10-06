# IR0 Kernel Product Overview

IR0 is a monolithic hobby operating system kernel for x86-64, focused on learning, experimentation, and pushing the boundaries of what a homebrew OS can achieve.

## Core Purpose
- Educational OS development platform
- Real-world userland software support (Doom, GCC, Bash)
- External TCP/IP networking capabilities
- Robust, modular, and extensible kernel base

## Current Status
- **Architecture**: x86-64 primary (x86-32 experimental, ARM in progress)
- **License**: GNU GPL v3.0
- **Version**: 1.0.0 pre-rc1
- **Status**: Active development, functional multitasking environment

## Key Features
- Memory management with paging and heap allocation
- Multiple scheduler algorithms (CFS, Priority, Round-Robin)
- Complete interrupt handling system
- VFS with basic filesystem support
- Process management with POSIX-like syscalls
- Hardware drivers (PS/2, ATA, VGA, timers)
- Interactive shell with authentication

## Build Targets
- **Desktop**: Full-featured with GUI support
- **Server**: High-performance networking focus
- **IoT**: Lightweight with power management
- **Embedded**: Minimal resource usage

## Ultimate Goals
- TCP/IP networking with ping utility
- Native Doom execution in userland
- Working GCC toolchain and Bash shell