/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: kernel_string_wrapper.c
 * Description: Compile the kernel string library in a host unit test.
 *
 * Every public symbol is renamed before the source is included so host libc
 * keeps its own memcpy/snprintf and the test calls the kernel implementations
 * directly.
 */

#define atoi ir0_atoi
#define memcmp ir0_memcmp
#define memcpy ir0_memcpy
#define memmove ir0_memmove
#define memset ir0_memset
#define snprintf ir0_snprintf
#define strchr ir0_strchr
#define strcmp ir0_strcmp
#define strcpy ir0_strcpy
#define strlen ir0_strlen
#define strncat ir0_strncat
#define strncmp ir0_strncmp
#define strncpy ir0_strncpy
#define strrchr ir0_strrchr
#define strstr ir0_strstr
#define strtol ir0_strtol
#define strtoul ir0_strtoul
#define vsnprintf ir0_vsnprintf

#include "../../includes/string.c"
