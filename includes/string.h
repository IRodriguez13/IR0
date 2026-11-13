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

// String conversion functions
int atoi(const char *str);
unsigned long strtoul(const char *str, char **endptr, int base);
long strtol(const char *str, char **endptr, int base);
char *itoa(int value, char *str, int base);

// String formatting
int snprintf(char *str, size_t size, const char *format, ...);
int vsnprintf(char *str, size_t size, const char *format, va_list ap);

#endif // STRING_H
