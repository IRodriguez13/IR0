// SPDX-License-Identifier: GPL-3.0-only
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * File: kernel_utils.h
 * Description: Common kernel utility functions and macros
 */

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// ===============================================================================
// COMMON MACROS
// ===============================================================================

// Alignment macros
#define ALIGN_UP(addr, align) (((addr) + (align) - 1) & ~((align) - 1))
#define ALIGN_DOWN(addr, align) ((addr) & ~((align) - 1))
#define IS_ALIGNED(addr, align) (((addr) & ((align) - 1)) == 0)

// Size macros
#define KB(x) ((x) * 1024)
#define MB(x) ((x) * 1024 * 1024)
#define GB(x) ((x) * 1024 * 1024 * 1024)

// Array size macro
#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

// Min/Max macros
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))

// Bit manipulation macros
#define BIT(n) (1UL << (n))
#define SET_BIT(var, bit) ((var) |= BIT(bit))
#define CLEAR_BIT(var, bit) ((var) &= ~BIT(bit))
#define TOGGLE_BIT(var, bit) ((var) ^= BIT(bit))
#define TEST_BIT(var, bit) (((var) & BIT(bit)) != 0)

// ===============================================================================
// COMMON UTILITY FUNCTIONS
// ===============================================================================

// String utilities
static inline size_t kernel_strlen(const char *str) {
    size_t len = 0;
    while (str[len]) len++;
    return len;
}

static inline int kernel_strcmp(const char *s1, const char *s2) {
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(unsigned char*)s1 - *(unsigned char*)s2;
}

static inline char *kernel_strcpy(char *dest, const char *src) {
    char *orig_dest = dest;
    while ((*dest++ = *src++));
    return orig_dest;
}

// Memory utilities
static inline void *kernel_memset(void *s, int c, size_t n) {
    unsigned char *p = s;
    while (n--) *p++ = c;
    return s;
}

static inline void *kernel_memcpy(void *dest, const void *src, size_t n) {
    unsigned char *d = dest;
    const unsigned char *s = src;
    while (n--) *d++ = *s++;
    return dest;
}

static inline int kernel_memcmp(const void *s1, const void *s2, size_t n) {
    const unsigned char *p1 = s1, *p2 = s2;
    while (n--) {
        if (*p1 != *p2) return *p1 - *p2;
        p1++;
        p2++;
    }
    return 0;
}

// Number conversion utilities
static inline uint32_t str_to_uint32(const char *str) {
    uint32_t result = 0;
    while (*str >= '0' && *str <= '9') {
        result = result * 10 + (*str - '0');
        str++;
    }
    return result;
}

static inline uint64_t str_to_uint64_hex(const char *str) {
    uint64_t result = 0;
    if (str[0] == '0' && str[1] == 'x') str += 2;
    
    while (*str) {
        char c = *str++;
        if (c >= '0' && c <= '9') {
            result = result * 16 + (c - '0');
        } else if (c >= 'a' && c <= 'f') {
            result = result * 16 + (c - 'a' + 10);
        } else if (c >= 'A' && c <= 'F') {
            result = result * 16 + (c - 'A' + 10);
        } else {
            break;
        }
    }
    return result;
}

// ===============================================================================
// ERROR HANDLING MACROS
// ===============================================================================

#define KERNEL_ASSERT(condition) \
    do { \
        if (!(condition)) { \
            panic("Assertion failed: " #condition " at " __FILE__ ":" STRINGIFY(__LINE__)); \
        } \
    } while (0)

#define STRINGIFY(x) #x
#define TOSTRING(x) STRINGIFY(x)

// ===============================================================================
// DEBUGGING MACROS
// ===============================================================================

#ifdef KERNEL_DEBUG
#define KDEBUG(fmt, ...) serial_printf("[DEBUG] " fmt "\n", ##__VA_ARGS__)
#else
#define KDEBUG(fmt, ...) do {} while (0)
#endif

#ifdef VERBOSE_LOGGING
#define KVERBOSE(fmt, ...) serial_printf("[VERBOSE] " fmt "\n", ##__VA_ARGS__)
#else
#define KVERBOSE(fmt, ...) do {} while (0)
#endif

// ===============================================================================
// COMPILER ATTRIBUTES
// ===============================================================================

#define PACKED __attribute__((packed))
#define ALIGNED(x) __attribute__((aligned(x)))
#define NORETURN __attribute__((noreturn))
#define UNUSED __attribute__((unused))
#define LIKELY(x) __builtin_expect(!!(x), 1)
#define UNLIKELY(x) __builtin_expect(!!(x), 0)

// ===============================================================================
// FORWARD DECLARATIONS
// ===============================================================================

// Panic function
extern void panic(const char *message) NORETURN;

// Serial printf function
extern void serial_printf(const char *fmt, ...);