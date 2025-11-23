// SPDX-License-Identifier: GPL-3.0-only
/**
 * IR0 Kernel — Rust FFI Bindings
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: kernel.rs
 * Description: Rust Foreign Function Interface bindings for IR0 kernel API
 */

// IR0 Kernel - Rust FFI Bindings
// This file provides Rust bindings for kernel functions
// Use: include this in Rust driver modules

#![no_std]
#![allow(non_camel_case_types)]
#![allow(dead_code)]

use core::ffi::c_void;

// TYPE DEFINITIONS (matching C types)

pub type u8 = core::primitive::u8;
pub type u16 = core::primitive::u16;
pub type u32 = core::primitive::u32;
pub type u64 = core::primitive::u64;
pub type i8 = core::primitive::i8;
pub type i16 = core::primitive::i16;
pub type i32 = core::primitive::i32;
pub type i64 = core::primitive::i64;
pub type usize = core::primitive::usize;
pub type isize = core::primitive::isize;

// PANIC LEVELS (matching panic_level_t in C)

#[repr(u32)]
#[derive(Copy, Clone, Debug)]
pub enum PanicLevel {
    KernelBug = 0,
    HardwareFault = 1,
    OutOfMemory = 2,
    StackOverflow = 3,
    AssertFailed = 4,
    MemError = 5,
    Testing = 6,
    RunningOutProcess = 7,
}

// KERNEL FUNCTIONS (extern "C" from kernel)

extern "C" {
    // Memory management
    pub fn kmalloc(size: usize) -> *mut c_void;
    pub fn kfree(ptr: *mut c_void);
    pub fn krealloc(ptr: *mut c_void, new_size: usize) -> *mut c_void;
    
    // Panic/Error handling
    pub fn panic(message: *const u8);
    pub fn panicex(
        message: *const u8,
        level: u32,  // PanicLevel as u32
        file: *const u8,
        line: i32,
        caller: *const u8
    );
    
    // Printing
    pub fn print(s: *const u8);
    pub fn print_error(s: *const u8);
    pub fn print_warning(s: *const u8);
    pub fn print_success(s: *const u8);
    
    // Driver registration
    pub fn ir0_register_driver(name: *const u8, ops: *const DriverOps) -> i32;
}

// DRIVER INTERFACE

#[repr(C)]
pub struct DriverOps {
    pub init: Option<extern "C" fn() -> i32>,
    pub probe: Option<extern "C" fn(*mut c_void) -> i32>,
    pub remove: Option<extern "C" fn(*mut c_void)>,
    pub read: Option<extern "C" fn(*mut c_void, usize) -> i32>,
    pub write: Option<extern "C" fn(*const c_void, usize) -> i32>,
    pub ioctl: Option<extern "C" fn(u32, *mut c_void) -> i32>,
}

// SAFE WRAPPERS

/// Safe wrapper for kernel panic
pub fn kernel_panic(msg: &str) -> ! {
    unsafe {
        // Convert Rust string to C string
        let c_msg = msg.as_ptr();
        panic(c_msg);
        core::hint::unreachable_unchecked()
    }
}

/// Safe wrapper for kernel print
pub fn kernel_print(msg: &str) {
    unsafe {
        print(msg.as_ptr());
    }
}

/// Safe memory allocation
pub fn kernel_alloc(size: usize) -> Option<*mut c_void> {
    unsafe {
        let ptr = kmalloc(size);
        if ptr.is_null() {
            None
        } else {
            Some(ptr)
        }
    }
}

/// Safe memory deallocation
pub fn kernel_free(ptr: *mut c_void) {
    unsafe {
        kfree(ptr);
    }
}

// MACRO HELPERS

/// Rust equivalent of CHECK_PTR
#[macro_export]
macro_rules! check_ptr {
    ($ptr:expr, $context:expr) => {
        if $ptr.is_null() {
            kernel_panic(&format!("NULL pointer in {}", $context));
        }
    };
}

/// Rust equivalent of ASSERT
#[macro_export]
macro_rules! kernel_assert {
    ($condition:expr, $msg:expr) => {
        if !$condition {
            kernel_panic($msg);
        }
    };
}

/// Convert C string to Rust &str (unsafe)
pub unsafe fn c_str_to_rust(c_str: *const u8) -> &'static str {
    let mut len = 0;
    while *c_str.add(len) != 0 {
        len += 1;
    }
    core::str::from_utf8_unchecked(core::slice::from_raw_parts(c_str, len))
}

// EXAMPLE DRIVER (commented out - reference implementation)

/*
// Example Rust driver using these bindings

#[no_mangle]
pub extern "C" fn rust_driver_init() -> i32 {
    kernel_print("Rust driver initializing\n");
    
    // Allocate memory
    if let Some(ptr) = kernel_alloc(1024) {
        kernel_print("Memory allocated successfully\n");
        kernel_free(ptr);
        return 0;  // IR0_OK
    }
    
    kernel_panic("Failed to allocate memory");
}

#[no_mangle]
pub extern "C" fn rust_driver_probe(device: *mut c_void) -> i32 {
    check_ptr!(device, "rust_driver_probe");
    // Probe logic here
    0
}

#[no_mangle]
pub extern "C" fn rust_driver_remove(device: *mut c_void) {
    // Remove logic here
}

static RUST_DRIVER_OPS: DriverOps = DriverOps {
    init: Some(rust_driver_init),
    probe: Some(rust_driver_probe),
    remove: Some(rust_driver_remove),
    read: None,
    write: None,
    ioctl: None,
};

#[no_mangle]
pub extern "C" fn register_rust_driver() -> i32 {
    unsafe {
        ir0_register_driver(
            b"rust_example\0".as_ptr(),
            &RUST_DRIVER_OPS as *const DriverOps
        )
    }
}
*/

// PANIC HANDLER (required for no_std)

#[panic_handler]
fn panic(info: &core::panic::PanicInfo) -> ! {
    let msg = if let Some(location) = info.location() {
        // Safe static string allocation
        b"Rust driver panic\0"
    } else {
        b"Rust driver panic (no location)\0"
    };
    
    unsafe {
        panic(msg.as_ptr());
        core::hint::unreachable_unchecked()
    }
}
