/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: string.h
 * Description: IR0 kernel source/header file
 */

#ifndef STRING_H
#define STRING_H

#include <stddef.h>
#include <stdint.h>
#include "stdarg.h"

// ===============================================================================
// STRING FUNCTIONS
// ===============================================================================

size_t strlen(const char *str);
int strcmp(const char *s1, const char *s2);
int strncmp(const char *s1, const char *s2, size_t n);
char *strcpy(char *dest, const char *src);
char *strncpy(char *dest, const char *src, size_t n);
char *strncat(char *dest, const char *src, size_t n);

// String search functions
char *strchr(const char *str, int c);
char *strrchr(const char *str, int c);
char *strstr(const char *haystack, const char *needle);

// Memory functions
void *memset(void *ptr, int value, size_t num);
void *memcpy(void *dest, const void *src, size_t num);
void *memmove(void *dest, const void *src, size_t num);
int memcmp(const void *ptr1, const void *ptr2, size_t num);

// Kernel-specific memory functions - Internal implementations
void *__kmemset_impl(void *s, int c, size_t n);
void *__kmemcpy_impl(void *dest, const void *src, size_t n);
char *__kstrncpy_impl(char *dest, const char *src, size_t n);

// Kernel-specific memory functions - Checked wrappers with debug info
void *__kmemset_checked(void *dst, int c, size_t n,
                       const char *file, int line, const char *caller);
void *__kmemcpy_checked(void *dst, const void *src, size_t n,
                       const char *file, int line, const char *caller);
char *__kstrncpy_checked(char *dst, const char *src, size_t n,
                        const char *file, int line, const char *caller);

// Automatic debug tracking macros - capture caller location transparently
#define kmemset(dst, c, n) \
    __kmemset_checked((dst), (c), (n), __FILE__, __LINE__, __func__)

#define kmemcpy(dst, src, n) \
    __kmemcpy_checked((dst), (src), (n), __FILE__, __LINE__, __func__)

#define kstrncpy(dst, src, n) \
    __kstrncpy_checked((dst), (src), (n), __FILE__, __LINE__, __func__)

// String conversion functions
int atoi(const char *str);
unsigned long strtoul(const char *str, char **endptr, int base);
long strtol(const char *str, char **endptr, int base);
char *itoa(int value, char *str, int base);

// String formatting
int snprintf(char *str, size_t size, const char *format, ...);
int vsnprintf(char *str, size_t size, const char *format, va_list ap);

#endif // STRING_H
