// SPDX-License-Identifier: GPL-3.0-only
/**
 * IR0 Kernel — C++ Runtime Support
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: compat.cpp
 * Description: C++ runtime implementation for freestanding kernel environment
 */

/**
 * IR0 Kernel - C++ Support Implementation
 * 
 * Provides necessary C++ runtime support for kernel components.
 * This file implements operators and functions required for freestanding C++.
 */

#include <ir0/compat.h>
#include <ir0/kmem.h>
#include <ir0/oops.h>

#ifdef __cplusplus

namespace ir0 {

// MEMORY OPERATORS

/**
 * Global new operator using kernel allocator
 */
void* operator_new(size_t size) {
    if (size == 0) return nullptr;
    void* ptr = kmalloc(size);
    if (!ptr) 
    {
        panicex("operator new failed: out of memory", 
               PANIC_OUT_OF_MEMORY, __FILE__, __LINE__, "operator new");
    }
    return ptr;
}

/**
 * Global delete operator using kernel allocator
 */
void operator_delete(void* ptr) noexcept {
    if (ptr) 
    {
        kfree(ptr);
    }
}

/**
 * Sized delete operator (C++14)
 */
void operator_delete(void* ptr, size_t size) noexcept {
    (void)size;  // Size not used in our allocator
    if (ptr) 
    {
        kfree(ptr);
    }
}

// ARRAY NEW/DELETE

/**
 * Array new operator
 */
void* operator_new_array(size_t size) 
{
    return operator_new(size);
}

/**
 * Array delete operator
 */
void operator_delete_array(void* ptr) noexcept 
{
    operator_delete(ptr);
}

/**
 * Sized array delete operator (C++14)
 */
void operator_delete_array(void* ptr, size_t size) noexcept {
    operator_delete(ptr, size);
}

// C++ RUNTIME SUPPORT

/**
 * Pure virtual function call handler
 * Called when pure virtual function is invoked
 */
extern "C" void __cxa_pure_virtual() {
    panicex("Pure virtual function called", 
           PANIC_KERNEL_BUG, "unknown", 0, "__cxa_pure_virtual");
}

/**
 * Guard for static initialization
 * Ensures static variables are initialized only once
 */
extern "C" int __cxa_guard_acquire(uint64_t* guard_object) {
    // Simple implementation: return 1 if not initialized
    return (*guard_object == 0) ? 1 : 0;
}

extern "C" void __cxa_guard_release(uint64_t* guard_object) {
    *guard_object = 1;
}

extern "C" void __cxa_guard_abort(uint64_t* guard_object) {
    (void)guard_object;
    // Nothing to do for abort in kernel
}

// FREESTANDING C++ HELPERS

/**
 * Minimal type_info for RTTI (even though we disable RTTI)
 * Some compilers still need this symbol
 */
namespace __cxxabiv1 {
    class __class_type_info {
    public:
        virtual ~__class_type_info() {}
    };
} // namespace __cxxabiv1

} // namespace ir0

// GLOBAL OPERATORS (Must be in global namespace)

/**
 * Note: The actual operators are declared inline in compat.h
 * to avoid multiple definition errors
 */

#endif // __cplusplus
