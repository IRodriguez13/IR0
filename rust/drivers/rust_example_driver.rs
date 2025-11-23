// SPDX-License-Identifier: GPL-3.0-only

#![no_std]
#![no_main]

/**
 * IR0 Kernel — Example Rust Driver
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: rust_example_driver.rs
 * Description: Reference implementation of a Rust driver for IR0 kernel
 */

use core::ffi::c_void;

// Import kernel FFI bindings
// Note: In actual use, this would be: extern crate ir0_ffi;
// For this example, we inline the necessary definitions

// Type definitions
pub type u8 = core::primitive::u8;
pub type u16 = core::primitive::u16;
pub type u32 = core::primitive::u32;
pub type i32 = core::primitive::i32;
pub type usize = core::primitive::usize;

// Driver return codes
pub const IR0_DRIVER_OK: i32 = 0;
pub const IR0_DRIVER_ERR: i32 = -1;

// External kernel functions
extern "C" {
    fn log_info(msg: *const u8);
    fn log_success(msg: *const u8);
    fn log_error(msg: *const u8);
    fn kmalloc(size: usize) -> *mut c_void;
    fn kfree(ptr: *mut c_void);
    fn outb(port: u16, value: u8);
    fn inb(port: u16) -> u8;
}

// Driver state
struct ExampleDriver {
    initialized: bool,
    device_port: u16,
    buffer: *mut u8,
}

impl ExampleDriver {
    const fn new() -> Self {
        Self {
            initialized: false,
            device_port: 0x3F8,  // Example: COM1 port
            buffer: core::ptr::null_mut(),
        }
    }
}

static mut DRIVER_STATE: ExampleDriver = ExampleDriver::new();

// DRIVER OPERATIONS

/// Initialize the driver
#[no_mangle]
pub extern "C" fn rust_example_init() -> i32 {
    unsafe {
        log_info(b"[Rust] Example driver initializing...\0".as_ptr());
        
        // Allocate driver buffer
        let buffer = kmalloc(4096) as *mut u8;
        if buffer.is_null() {
            log_error(b"[Rust] Failed to allocate driver buffer\0".as_ptr());
            return IR0_DRIVER_ERR;
        }
        
        DRIVER_STATE.buffer = buffer;
        DRIVER_STATE.initialized = true;
        
        // Initialize hardware (example: test I/O port)
        outb(DRIVER_STATE.device_port, 0x00);
        let test_value = inb(DRIVER_STATE.device_port);
        
        log_success(b"[Rust] Example driver initialized successfully\0".as_ptr());
        log_info(b"[Rust] Driver demonstrates:\0".as_ptr());
        log_info(b"  - Memory allocation via kmalloc()\0".as_ptr());
        log_info(b"  - I/O port operations (inb/outb)\0".as_ptr());
        log_info(b"  - Kernel logging functions\0".as_ptr());
        log_info(b"  - Driver state management\0".as_ptr());
        
        IR0_DRIVER_OK
    }
}

/// Probe for device
#[no_mangle]
pub extern "C" fn rust_example_probe(device: *mut c_void) -> i32 {
    unsafe {
        if device.is_null() {
            log_error(b"[Rust] Probe called with NULL device\0".as_ptr());
            return IR0_DRIVER_ERR;
        }
        
        log_info(b"[Rust] Probing device...\0".as_ptr());
        
        // Probing logic would go here
        // For this example, we just succeed
        
        log_success(b"[Rust] Device probe successful\0".as_ptr());
        IR0_DRIVER_OK
    }
}

/// Remove device
#[no_mangle]
pub extern "C" fn rust_example_remove(device: *mut c_void) {
    unsafe {
        log_info(b"[Rust] Removing device...\0".as_ptr());
        
        // Cleanup device-specific resources
        
        log_success(b"[Rust] Device removed\0".as_ptr());
    }
}

/// Shutdown driver
#[no_mangle]
pub extern "C" fn rust_example_shutdown() {
    unsafe {
        log_info(b"[Rust] Shutting down driver...\0".as_ptr());
        
        // Free driver buffer
        if !DRIVER_STATE.buffer.is_null() {
            kfree(DRIVER_STATE.buffer as *mut c_void);
            DRIVER_STATE.buffer = core::ptr::null_mut();
        }
        
        DRIVER_STATE.initialized = false;
        
        log_success(b"[Rust] Driver shutdown complete\0".as_ptr());
    }
}

/// Read from device
#[no_mangle]
pub extern "C" fn rust_example_read(buf: *mut c_void, len: usize) -> i32 {
    unsafe {
        if buf.is_null() {
            log_error(b"[Rust] Read called with NULL buffer\0".as_ptr());
            return IR0_DRIVER_ERR;
        }
        
        if !DRIVER_STATE.initialized {
            log_error(b"[Rust] Read called on uninitialized driver\0".as_ptr());
            return IR0_DRIVER_ERR;
        }
        
        // Read logic would go here
        // For this example, we just return success
        
        log_info(b"[Rust] Read operation completed\0".as_ptr());
        len as i32
    }
}

/// Write to device
#[no_mangle]
pub extern "C" fn rust_example_write(buf: *const c_void, len: usize) -> i32 {
    unsafe {
        if buf.is_null() {
            log_error(b"[Rust] Write called with NULL buffer\0".as_ptr());
            return IR0_DRIVER_ERR;
        }
        
        if !DRIVER_STATE.initialized {
            log_error(b"[Rust] Write called on uninitialized driver\0".as_ptr());
            return IR0_DRIVER_ERR;
        }
        
        // Write logic would go here
        // For this example, we output to I/O port
        let data = buf as *const u8;
        for i in 0..len.min(64) {
            outb(DRIVER_STATE.device_port, *data.add(i));
        }
        
        log_info(b"[Rust] Write operation completed\0".as_ptr());
        len as i32
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

extern "C" {
    fn ir0_register_driver(info: *const DriverInfo, ops: *const DriverOps) -> *mut c_void;
}


static DRIVER_INFO: DriverInfo = DriverInfo {
    name: b"rust_example\0".as_ptr(),
    version: b"1.0.0\0".as_ptr(),
    author: b"IR0 Kernel Team\0".as_ptr(),
    description: b"Example Rust driver demonstrating kernel integration\0".as_ptr(),
    language: 2,  // IR0_DRIVER_LANG_RUST
};

// Safety: We're in a single-threaded kernel environment
unsafe impl Sync for DriverInfo {}

static DRIVER_OPS: DriverOps = DriverOps {
    init: Some(rust_example_init),
    probe: Some(rust_example_probe),
    remove: Some(rust_example_remove),
    shutdown: Some(rust_example_shutdown),
    read: Some(rust_example_read),
    write: Some(rust_example_write),
    ioctl: None,
    suspend: None,
    resume: None,
};

// Safety: Function pointers are safe to share across threads
unsafe impl Sync for DriverOps {}

/// Register this driver with the kernel
#[no_mangle]
pub extern "C" fn register_rust_example_driver() -> *mut c_void {
    unsafe {
        ir0_register_driver(&DRIVER_INFO as *const DriverInfo, &DRIVER_OPS as *const DriverOps)
    }
}

// PANIC HANDLER (required for no_std)

#[panic_handler]
fn panic(_info: &core::panic::PanicInfo) -> ! {
    unsafe {
        log_error(b"[Rust] Driver panic!\0".as_ptr());
        loop {
            core::hint::spin_loop();
        }
    }
}
