# IR0 Kernel - Rust FFI Bindings

This directory contains Rust Foreign Function Interface (FFI) bindings for the IR0 kernel.

## Files

- `kernel.rs` - Core kernel API bindings for Rust drivers

## Usage

Include this in your Rust driver:

```rust
use ir0::ffi::kernel::*;
```

## Building

Rust drivers are compiled with:

```bash
make unibuild -rust drivers/your_driver.rs
```

See `CONTRIBUTING.md` for full development guide.
