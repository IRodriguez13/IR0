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

// DRIVER LANGUAGE (matching ir0_driver_lang_t in C)

#[repr(u32)]
#[derive(Copy, Clone, Debug)]
pub enum DriverLang {
    C = 0,
    Cpp = 1,
    Rust = 2,
}

// DRIVER STATE (matching ir0_driver_state_t in C)

#[repr(u32)]
#[derive(Copy, Clone, Debug)]
pub enum DriverState {
    Unregistered = 0,
    Registered = 1,
    Initialized = 2,
    Active = 3,
    Failed = 4,
}

// DRIVER RETURN CODES

pub const IR0_DRIVER_OK: i32 = 0;
pub const IR0_DRIVER_ERR: i32 = -1;
pub const IR0_DRIVER_ERR_NOMEM: i32 = -2;
pub const IR0_DRIVER_ERR_INVAL: i32 = -3;
pub const IR0_DRIVER_ERR_EXISTS: i32 = -4;
pub const IR0_DRIVER_ERR_NOTFOUND: i32 = -5;

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
    
        // String operations
    pub fn strlen(s: *const u8) -> usize;
    pub fn strcmp(s1: *const u8, s2: *const u8) -> i32;
    pub fn strcpy(dest: *mut u8, src: *const u8) -> *mut u8;
    pub fn memcpy(dest: *mut c_void, src: *const c_void, n: usize) -> *mut c_void;
    pub fn memset(s: *mut c_void, c: i32, n: usize) -> *mut c_void;
    
    // Enhanced memory management
    pub fn kcalloc(n: usize, size: usize) -> *mut c_void;
    pub fn kvmalloc(size: usize) -> *mut c_void;
    
    // I/O Port operations (x86/x64)
    pub fn outb(port: u16, value: u8);
    pub fn inb(port: u16) -> u8;
    pub fn outw(port: u16, value: u16);
    pub fn inw(port: u16) -> u16;
    pub fn outl(port: u16, value: u32);
    pub fn inl(port: u16) -> u32;
    
    // Interrupt management
    pub fn register_irq_handler(irq: u32, handler: extern "C" fn()) -> i32;
    pub fn unregister_irq_handler(irq: u32) -> i32;
    pub fn disable_interrupts();
    pub fn enable_interrupts();
    pub fn are_interrupts_enabled() -> i32;
    
    // Enhanced logging (printk-style)
    pub fn printk(level: u32, fmt: *const u8, ...);
    pub fn log_info(msg: *const u8);
    pub fn log_warning(msg: *const u8);
    pub fn log_error(msg: *const u8);
    pub fn log_success(msg: *const u8);
    pub fn log_debug(msg: *const u8);
    
    // VFS operations
    pub fn vfs_open(path: *const u8, flags: u32) -> i32;
    pub fn vfs_read(fd: i32, buf: *mut c_void, count: usize) -> i32;
    pub fn vfs_write(fd: i32, buf: *const c_void, count: usize) -> i32;
    pub fn vfs_close(fd: i32) -> i32;
    
    // Driver registration (new enhanced API)
    pub fn ir0_register_driver(
        info: *const DriverInfo,
        ops: *const DriverOps
    ) -> *mut c_void;  // Returns ir0_driver_t*
    
    pub fn ir0_unregister_driver(driver: *mut c_void) -> i32;
    pub fn ir0_find_driver(name: *const u8) -> *mut c_void;
}

// DRIVER INTERFACE

/// Driver metadata (matching ir0_driver_info_t in C)
#[repr(C)]
pub struct DriverInfo {
    pub name: *const u8,        // Driver name (null-terminated)
    pub version: *const u8,     // Version string
    pub author: *const u8,      // Author name
    pub description: *const u8, // Brief description
    pub language: u32,          // DriverLang as u32
}

/// Driver operations (matching ir0_driver_ops_t in C)
#[repr(C)]
pub struct DriverOps {
    // Lifecycle
    pub init: Option<extern "C" fn() -> i32>,
    pub probe: Option<extern "C" fn(*mut c_void) -> i32>,
    pub remove: Option<extern "C" fn(*mut c_void)>,
    pub shutdown: Option<extern "C" fn()>,
    
    // I/O operations
    pub read: Option<extern "C" fn(*mut c_void, usize) -> i32>,
    pub write: Option<extern "C" fn(*const c_void, usize) -> i32>,
    pub ioctl: Option<extern "C" fn(u32, *mut c_void) -> i32>,
    
    // Power management
    pub suspend: Option<extern "C" fn() -> i32>,
    pub resume: Option<extern "C" fn() -> i32>,
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

/// Safe calloc (zero-initialized allocation)
pub fn kernel_calloc(n: usize, size: usize) -> Option<*mut c_void> {
    unsafe {
        let ptr = kcalloc(n, size);
        if ptr.is_null() {
            None
        } else {
            Some(ptr)
        }
    }
}

// ============================================================================
// I/O PORT OPERATIONS
// ============================================================================

/// Read byte from I/O port
pub fn io_inb(port: u16) -> u8 {
    unsafe { inb(port) }
}

/// Write byte to I/O port
pub fn io_outb(port: u16, value: u8) {
    unsafe { outb(port, value); }
}

/// Read word from I/O port
pub fn io_inw(port: u16) -> u16 {
    unsafe { inw(port) }
}

/// Write word to I/O port
pub fn io_outw(port: u16, value: u16) {
    unsafe { outw(port, value); }
}

/// Read dword from I/O port
pub fn io_inl(port: u16) -> u32 {
    unsafe { inl(port) }
}

/// Write dword to I/O port
pub fn io_outl(port: u16, value: u32) {
    unsafe { outl(port, value); }
}

// ============================================================================
// ENHANCED LOGGING
// ============================================================================

/// Log info message
pub fn log_info_msg(msg: &str) {
    unsafe {
        log_info(msg.as_ptr());
    }
}

/// Log warning message
pub fn log_warn_msg(msg: &str) {
    unsafe {
        log_warning(msg.as_ptr());
    }
}

/// Log error message
pub fn log_error_msg(msg: &str) {
    unsafe {
        log_error(msg.as_ptr());
    }
}

/// Log success message
pub fn log_success_msg(msg: &str) {
    unsafe {
        log_success(msg.as_ptr());
    }
}

/// Log debug message
pub fn log_debug_msg(msg: &str) {
    unsafe {
        log_debug(msg.as_ptr());
    }
}

// ============================================================================
// INTERRUPT MANAGEMENT
// ============================================================================

/// Disable interrupts
pub fn irq_disable() {
    unsafe { disable_interrupts(); }
}

/// Enable interrupts
pub fn irq_enable() {
    unsafe { enable_interrupts(); }
}

/// Check if interrupts are enabled
pub fn irq_enabled() -> bool {
    unsafe { are_interrupts_enabled() != 0 }
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
// Example Rust driver using enhanced bindings

#[no_mangle]
pub extern "C" fn rust_driver_init() -> i32 {
    log_info_msg("Rust driver initializing\n");
    
    // Allocate memory
    if let Some(ptr) = kernel_alloc(1024) {
        log_success_msg("Memory allocated successfully\n");
        kernel_free(ptr);
        return IR0_DRIVER_OK;
    }
    
    log_error_msg("Failed to allocate memory\n");
    IR0_DRIVER_ERR_NOMEM
}

#[no_mangle]
pub extern "C" fn rust_driver_probe(device: *mut c_void) -> i32 {
    check_ptr!(device, "rust_driver_probe");
    log_info_msg("Probing device\n");
    IR0_DRIVER_OK
}

#[no_mangle]
pub extern "C" fn rust_driver_remove(device: *mut c_void) {
    log_info_msg("Removing device\n");
}

#[no_mangle]
pub extern "C" fn rust_driver_shutdown() {
    log_info_msg("Shutting down driver\n");
}

static RUST_DRIVER_INFO: DriverInfo = DriverInfo {
    name: b"rust_example\0".as_ptr(),
    version: b"1.0.0\0".as_ptr(),
    author: b"IR0 Team\0".as_ptr(),
    description: b"Example Rust driver\0".as_ptr(),
    language: DriverLang::Rust as u32,
};

static RUST_DRIVER_OPS: DriverOps = DriverOps {
    init: Some(rust_driver_init),
    probe: Some(rust_driver_probe),
    remove: Some(rust_driver_remove),
    shutdown: Some(rust_driver_shutdown),
    read: None,
    write: None,
    ioctl: None,
    suspend: None,
    resume: None,
};

#[no_mangle]
pub extern "C" fn register_rust_driver() -> *mut c_void {
    unsafe {
        ir0_register_driver(
            &RUST_DRIVER_INFO as *const DriverInfo,
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
