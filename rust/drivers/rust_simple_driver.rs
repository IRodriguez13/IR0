// SPDX-License-Identifier: GPL-3.0-only
// IR0 Kernel — Simple Rust Driver (Test)
// Copyright (C) 2025  Iván Rodriguez
//
// This file is part of the IR0 Operating System.
// Distributed under the terms of the GNU General Public License v3.0.
// See the LICENSE file in the project root for full license information.
//
// File: rust_simple_driver.rs
// Description: Minimal Rust driver for testing multi-language support

#![no_std]
#![no_main]

use core::ffi::c_void;

// External kernel functions (must be available at link time)
extern "C" {
    fn serial_print(msg: *const u8);
    fn ir0_register_driver(info: *const DriverInfo, ops: *const DriverOps) -> *mut c_void;
}

// Driver return codes
pub const IR0_DRIVER_OK: i32 = 0;
pub const IR0_DRIVER_ERR: i32 = -1;

// Driver state
static mut INITIALIZED: bool = false;

// DRIVER OPERATIONS

/// Initialize the driver
#[no_mangle]
pub extern "C" fn rust_simple_init() -> i32 {
    unsafe {
        serial_print(b"[Rust Simple] Initializing driver...\n\0".as_ptr());
        
        INITIALIZED = true;
        
        serial_print(b"[Rust Simple] Driver initialized successfully!\n\0".as_ptr());
        serial_print(b"[Rust Simple] This is a test driver written in Rust\n\0".as_ptr());
        serial_print(b"[Rust Simple] Multi-language support is working!\n\0".as_ptr());
        IR0_DRIVER_OK
    }
}

/// Shutdown driver
#[no_mangle]
pub extern "C" fn rust_simple_shutdown() {
    unsafe {
        serial_print(b"[Rust Simple] Shutting down driver...\n\0".as_ptr());
        INITIALIZED = false;
        serial_print(b"[Rust Simple] Driver shutdown complete\n\0".as_ptr());
    }
}

// DRIVER REGISTRATION

#[repr(C)]
struct DriverInfo {
    name: *const u8,
    version: *const u8,
    author: *const u8,
    description: *const u8,
    language: u32,
}

#[repr(C)]
struct DriverOps {
    init: Option<extern "C" fn() -> i32>,
    probe: Option<extern "C" fn(*mut c_void) -> i32>,
    remove: Option<extern "C" fn(*mut c_void)>,
    shutdown: Option<extern "C" fn()>,
    read: Option<extern "C" fn(*mut c_void, usize) -> i32>,
    write: Option<extern "C" fn(*const c_void, usize) -> i32>,
    ioctl: Option<extern "C" fn(u32, *mut c_void) -> i32>,
    suspend: Option<extern "C" fn() -> i32>,
    resume: Option<extern "C" fn() -> i32>,
}

static DRIVER_INFO: DriverInfo = DriverInfo {
    name: b"rust_simple\0".as_ptr(),
    version: b"1.0.0\0".as_ptr(),
    author: b"IR0 Kernel Team\0".as_ptr(),
    description: b"Simple Rust driver for testing multi-language support\0".as_ptr(),
    language: 2,  // IR0_DRIVER_LANG_RUST
};

unsafe impl Sync for DriverInfo {}

static DRIVER_OPS: DriverOps = DriverOps {
    init: Some(rust_simple_init),
    probe: None,
    remove: None,
    shutdown: Some(rust_simple_shutdown),
    read: None,
    write: None,
    ioctl: None,
    suspend: None,
    resume: None,
};

unsafe impl Sync for DriverOps {}

/// Register this driver with the kernel
#[no_mangle]
pub extern "C" fn register_rust_simple_driver() -> *mut c_void {
    unsafe {
        ir0_register_driver(&DRIVER_INFO as *const DriverInfo, &DRIVER_OPS as *const DriverOps)
    }
}

// PANIC HANDLER (required for no_std)

#[panic_handler]
fn panic(_info: &core::panic::PanicInfo) -> ! {
    unsafe {
        serial_print(b"[Rust Simple] PANIC: Driver panic occurred!\n\0".as_ptr());
        loop {
            core::hint::spin_loop();
        }
    }
}
