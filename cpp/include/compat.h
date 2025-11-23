// SPDX-License-Identifier: GPL-3.0-only
/**
 * IR0 Kernel — C++/Rust Interoperability Header  
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: compat.h
 * Description: Compatibility layer for multi-language kernel development (C/C++/Rust)
 */

/**
 * IR0 Kernel - C++/Rust Interoperability Header
 * 
 * Provides compatibility layer for multi-language kernel development.
 * This header ensures C code can be called from C++ and Rust, and vice versa.
 */

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// LANGUAGE DETECTION

#if defined(__cplusplus)
    #define IR0_LANG_CPP 1
    #define IR0_EXTERN_C_BEGIN extern "C" {
    #define IR0_EXTERN_C_END }
#else
    #define IR0_LANG_C 1
    #define IR0_EXTERN_C_BEGIN
    #define IR0_EXTERN_C_END
#endif

// Rust detection (when building .rs files, they'll define this)
#if defined(__IR0_RUST__)
    #define IR0_LANG_RUST 1
#endif
// ABI COMPATIBILITY

/**
 * IR0_API - Marks functions as part of stable kernel API
 * Ensures C calling convention for cross-language calls
 */
#ifdef __cplusplus
    #define IR0_API extern "C"
#else
    #define IR0_API
#endif

/**
 * IR0_EXPORT - Marks symbols for export to modules/drivers
 */
#define IR0_EXPORT __attribute__((visibility("default")))

/**
 * IR0_NO_MANGLE - Prevents C++ name mangling
 */
#ifdef __cplusplus
    #define IR0_NO_MANGLE extern "C"
#else
    #define IR0_NO_MANGLE
#endif

// PLATFORM-SPECIFIC ATTRIBUTES

/**
 * Calling conventions
 */
#if defined(__x86_64__) || defined(_M_X64)
    #define IR0_FASTCALL  __attribute__((fastcall))
    #define IR0_STDCALL   __attribute__((stdcall))
#else
    #define IR0_FASTCALL
    #define IR0_STDCALL
#endif

/**
 * Alignment
 */
#define IR0_ALIGNED(n) __attribute__((aligned(n)))
#define IR0_PACKED __attribute__((packed))

/**
 * Optimization hints
 */
#define IR0_INLINE static inline __attribute__((always_inline))
#define IR0_NOINLINE __attribute__((noinline))
#define IR0_PURE __attribute__((pure))
#define IR0_CONST __attribute__((const))

// TYPE DEFINITIONS FOR CROSS-LANGUAGE USE

/**
 * Standard integer types (guaranteed size across C/C++/Rust)
 */
typedef uint8_t   u8;
typedef uint16_t  u16;
typedef uint32_t  u32;
typedef uint64_t  u64;
typedef int8_t    i8;
typedef int16_t   i16;
typedef int32_t   i32;
typedef int64_t   i64;
typedef uintptr_t usize;
typedef intptr_t  isize;

/**
 * Boolean type compatible with Rust
 */
#ifndef __cplusplus
typedef _Bool ir0_bool;
#else
typedef bool ir0_bool;
#endif

/**
 * Result type for error handling (Rust-style)
 */
typedef enum {
    IR0_OK = 0,
    IR0_ERR = -1
} ir0_result_t;

/**
 * Option type (Rust-style)
 */
#define IR0_NONE NULL
#define IR0_SOME(x) (x)

// RUST FFI COMPATIBILITY

/**
 * When calling Rust from C, use these types for compatibility:
 * 
 * C Type          Rust Type
 * --------------------------------
 * u8/u16/u32/u64  u8/u16/u32/u64
 * i8/i16/i32/i64  i8/i16/i32/i64
 * usize/isize     usize/isize
 * void*           *const c_void / *mut c_void
 * bool            bool
 * 
 * Example Rust driver function:
 * 
 * #[no_mangle]
 * pub extern "C" fn driver_init() -> i32 {
 *     // ...
 *     0  // Return OK
 * }
 */

/**
 * Panic handler for Rust drivers
 * Rust panics will call into this C function
 */
IR0_API void ir0_rust_panic_handler(const char *file, u32 line, const char *msg);

// C++ COMPATIBILITY

#ifdef __cplusplus

/**
 * C++ kernel components must follow these rules:
 * 1. No exceptions (compile with -fno-exceptions)
 * 2. No RTTI (compile with -fno-rtti)
 * 3. Custom new/delete using kmalloc/kfree
 * 4. Freestanding environment (no stdlib)
 */

namespace ir0 {

// Kernel new/delete operators (forward declarations)
void* operator_new(size_t size);
void operator_delete(void* ptr);
void operator_delete(void* ptr, size_t size);

} // namespace ir0

// Override global new/delete
inline void* operator new(size_t size) { return ir0::operator_new(size); }
inline void operator delete(void* ptr) noexcept { ir0::operator_delete(ptr); }
inline void operator delete(void* ptr, size_t size) noexcept { ir0::operator_delete(ptr, size); }

// Placement new (allowed in kernel)
inline void* operator new(size_t, void* ptr) { return ptr; }
inline void operator delete(void*, void*) noexcept {}

#endif // __cplusplus

// DRIVER INTERFACE (Multi-language)

// Include the canonical driver interface
// This provides ir0_driver_ops_t, ir0_driver_info_t, and registration functions
#ifdef __cplusplus
extern "C" {
#endif
    #include <ir0/driver.h>
#ifdef __cplusplus
}
#endif

// USAGE EXAMPLES

/*
 * C EXAMPLE:
 * 
 * #include <ir0/compat.h>
 * 
 * IR0_API i32 my_driver_init(void) {
 *     // C code
 *     return IR0_OK;
 * }
 *
 *
 * C++ EXAMPLE:
 * 
 * #include <ir0/compat.h>
 * 
 * extern "C" i32 my_driver_init() {
 *     // C++ code with classes, templates, etc.
 *     auto* obj = new MyClass();
 *     delete obj;
 *     return IR0_OK;
 * }
 *
 *
 * RUST EXAMPLE:
 * 
 * use core::ffi::c_void;
 * 
 * #[no_mangle]
 * pub extern "C" fn my_driver_init() -> i32 {
 *     // Rust code
 *     0  // IR0_OK
 * }
 */
