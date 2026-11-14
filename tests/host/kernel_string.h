/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: kernel_string.h
 * Description: Host-test surface for the freestanding kernel string library.
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

size_t ir0_strlen(const char *str);
int ir0_strcmp(const char *s1, const char *s2);
int ir0_strncmp(const char *s1, const char *s2, size_t n);
char *ir0_strcpy(char *dest, const char *src);
char *ir0_strncpy(char *dest, const char *src, size_t n);
char *ir0_strncat(char *dest, const char *src, size_t n);
char *ir0_strchr(const char *str, int c);
char *ir0_strrchr(const char *str, int c);
char *ir0_strstr(const char *haystack, const char *needle);
void *ir0_memset(void *ptr, int value, size_t num);
void *ir0_memcpy(void *dest, const void *src, size_t num);
void *ir0_memmove(void *dest, const void *src, size_t num);
int ir0_memcmp(const void *ptr1, const void *ptr2, size_t num);
int ir0_atoi(const char *str);
unsigned long ir0_strtoul(const char *str, char **endptr, int base);
long ir0_strtol(const char *str, char **endptr, int base);
int ir0_snprintf(char *str, size_t size, const char *format, ...);
