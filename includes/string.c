// SPDX-License-Identifier: GPL-3.0-only
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: string.c
 * Description: Standard C library string manipulation functions
 */

#include "string.h"
#include "stdarg.h"

/* Compiler optimization hints */
#define likely(x)	__builtin_expect(!!(x), 1)
#define unlikely(x)	__builtin_expect(!!(x), 0)

/**
 * strlen - calculate length of string
 * @str: string to measure
 *
 * Returns length of string excluding null terminator.
 */
size_t strlen(const char *str)
{
	size_t len = 0;
	
	if (unlikely(!str))
		return 0;
	
	while (str[len] != '\0')
		len++;
	
	return len;
}

/**
 * strcmp - compare two strings
 * @s1: first string
 * @s2: second string
 *
 * Returns negative, zero, or positive value if s1 is less than,
 * equal to, or greater than s2.
 */
int strcmp(const char *s1, const char *s2)
{
	if (unlikely(!s1 || !s2))
		return s1 ? 1 : (s2 ? -1 : 0);
	
	while (*s1 && (*s1 == *s2)) {
		s1++;
		s2++;
	}
	
	return *(unsigned char *)s1 - *(unsigned char *)s2;
}

/**
 * strncmp - compare two strings up to n characters
 * @s1: first string
 * @s2: second string
 * @n: maximum number of characters to compare
 *
 * Returns negative, zero, or positive value if s1 is less than,
 * equal to, or greater than s2.
 */
int strncmp(const char *s1, const char *s2, size_t n)
{
	if (unlikely(!s1 || !s2 || n == 0))
		return 0;
	
	while (n && *s1 && (*s1 == *s2)) {
		s1++;
		s2++;
		n--;
	}
	
	if (n == 0)
		return 0;
	
	return *(unsigned char *)s1 - *(unsigned char *)s2;
}

char *strcpy(char *dest, const char *src)
{
    char *d = dest;
    while ((*d++ = *src++) != '\0')
        ;
    return dest;
}

char *strncpy(char *dest, const char *src, size_t n)
{
    char *d = dest;
    while (n > 0 && *src != '\0')
    {
        *d++ = *src++;
        n--;
    }
    while (n > 0)
    {
        *d++ = '\0';
        n--;
    }
    return dest;
}

char *strcat(char *dest, const char *src)
{
    char *d = dest;
    while (*d != '\0')
        d++;
    while ((*d++ = *src++) != '\0')
        ;
    return dest;
}

char *strncat(char *dest, const char *src, size_t n)
{
    char *d = dest;
    while (*d != '\0')
        d++;
    while (n > 0 && *src != '\0')
    {
        *d++ = *src++;
        n--;
    }
    *d = '\0';
    return dest;
}

// ===============================================================================
// STRING SEARCH FUNCTIONS
// ===============================================================================

char *strchr(const char *str, int c)
{
    while (*str != '\0')
    {
        if (*str == (char)c)
        {
            return (char *)str;
        }
        str++;
    }
    if (c == '\0')
    {
        return (char *)str;
    }
    return NULL;
}

char *strrchr(const char *str, int c)
{
    const char *last = NULL;
    while (*str != '\0')
    {
        if (*str == (char)c)
        {
            last = str;
        }
        str++;
    }
    if (c == '\0')
    {
        return (char *)str;
    }
    return (char *)last;
}

char *strstr(const char *haystack, const char *needle)
{
    if (*needle == '\0')
    {
        return (char *)haystack;
    }

    while (*haystack != '\0')
    {
        const char *h = haystack;
        const char *n = needle;

        while (*n != '\0' && *h == *n)
        {
            h++;
            n++;
        }

        if (*n == '\0')
        {
            return (char *)haystack;
        }

        haystack++;
    }

    return NULL;
}

// ===============================================================================
// MEMORY FUNCTIONS
// ===============================================================================

void *memset(void *ptr, int value, size_t num)
{
    unsigned char *p = (unsigned char *)ptr;
    while (num-- > 0)
    {
        *p++ = (unsigned char)value;
    }
    return ptr;
}

void *memcpy(void *dest, const void *src, size_t num)
{
    unsigned char *d = (unsigned char *)dest;
    const unsigned char *s = (const unsigned char *)src;
    while (num-- > 0)
    {
        *d++ = *s++;
    }
    return dest;
}

void *memmove(void *dest, const void *src, size_t num)
{
    unsigned char *d = (unsigned char *)dest;
    const unsigned char *s = (const unsigned char *)src;

    if (d < s)
    {
        while (num-- > 0)
        {
            *d++ = *s++;
        }
    }
    else
    {
        d += num;
        s += num;
        while (num-- > 0)
        {
            *--d = *--s;
        }
    }

    return dest;
}

int memcmp(const void *ptr1, const void *ptr2, size_t num)
{
    const unsigned char *p1 = (const unsigned char *)ptr1;
    const unsigned char *p2 = (const unsigned char *)ptr2;

    while (num-- > 0)
    {
        if (*p1 != *p2)
        {
            return *p1 - *p2;
        }

        p1++;
        p2++;
    }

    return 0;
}

// ===============================================================================
// STRING TOKENIZATION
// ===============================================================================

static char *strtok_str = NULL;

char *strtok(char *str, const char *delim)
{
    return strtok_r(str, delim, &strtok_str);
}

char *strtok_r(char *str, const char *delim, char **saveptr)
{
    if (str != NULL)
    {
        *saveptr = str;
    }

    if (*saveptr == NULL)
    {
        return NULL;
    }

    // Skip leading delimiters
    while (**saveptr != '\0' && strchr(delim, **saveptr) != NULL)
    {
        (*saveptr)++;
    }

    if (**saveptr == '\0')
    {
        return NULL;
    }

    char *token = *saveptr;

    // Find end of token
    while (**saveptr != '\0' && strchr(delim, **saveptr) == NULL)
    {
        (*saveptr)++;
    }

    if (**saveptr != '\0')
    {
        **saveptr = '\0';
        (*saveptr)++;
    }

    return token;
}

// ===============================================================================
// STRING CONVERSION FUNCTIONS
// ===============================================================================

int atoi(const char *str)
{
    int result = 0;
    int sign = 1;

    // Skip whitespace
    while (*str == ' ' || *str == '\t' || *str == '\n')
    {
        str++;
    }

    // Handle sign
    if (*str == '-')
    {
        sign = -1;
        str++;
    }
    else if (*str == '+')
    {
        str++;
    }

    // Convert digits
    while (*str >= '0' && *str <= '9')
    {
        result = result * 10 + (*str - '0');
        str++;
    }

    return sign * result;
}

long atol(const char *str)
{
    long result = 0;
    int sign = 1;

    // Skip whitespace
    while (*str == ' ' || *str == '\t' || *str == '\n')
    {
        str++;
    }

    // Handle sign
    if (*str == '-')
    {
        sign = -1;
        str++;
    }
    else if (*str == '+')
    {
        str++;
    }

    // Convert digits
    while (*str >= '0' && *str <= '9')
    {
        result = result * 10 + (*str - '0');
        str++;
    }

    return sign * result;
}

unsigned long strtoul(const char *str, char **endptr, int base)
{
    unsigned long result = 0;
    int sign = 1;

    // Skip whitespace
    while (*str == ' ' || *str == '\t' || *str == '\n')
    {
        str++;
    }

    // Handle sign
    if (*str == '-')
    {
        sign = -1;
        str++;
    }
    else if (*str == '+')
    {
        str++;
    }

    // Handle base
    if (base == 0)
    {
        if (*str == '0')
        {
            str++;
            if (*str == 'x' || *str == 'X')
            {
                base = 16;
                str++;
            }
            else
            {
                base = 8;
            }
        }
        else
        {
            base = 10;
        }
    }

    // Convert digits
    while (*str != '\0')
    {
        int digit;
        if (*str >= '0' && *str <= '9')
        {
            digit = *str - '0';
        }
        else if (*str >= 'a' && *str <= 'z')
        {
            digit = *str - 'a' + 10;
        }
        else if (*str >= 'A' && *str <= 'Z')
        {
            digit = *str - 'A' + 10;
        }
        else
        {
            break;
        }

        if (digit >= base)
        {
            break;
        }

        result = result * base + digit;
        str++;
    }

    if (endptr != NULL)
    {
        *endptr = (char *)str;
    }

    return sign * result;
}

long strtol(const char *str, char **endptr, int base)
{
    return (long)strtoul(str, endptr, base);
}

// ===============================================================================
// STRING FORMATTING
// ===============================================================================

static void reverse_string(char *str, int length)
{
    int start = 0;
    int end = length - 1;
    while (start < end)
    {
        char temp = str[start];
        str[start] = str[end];
        str[end] = temp;
        start++;
        end--;
    }
}

static int int_to_string(int value, char *str, int base)
{
    int i = 0;
    int is_negative = 0;

    if (value == 0)
    {
        str[i++] = '0';
        str[i] = '\0';
        return i;
    }

    if (value < 0 && base == 10)
    {
        is_negative = 1;
        value = -value;
    }

    while (value != 0)
    {
        int rem = value % base;
        str[i++] = (rem > 9) ? (rem - 10) + 'a' : rem + '0';
        value = value / base;
    }

    if (is_negative)
    {
        str[i++] = '-';
    }

    str[i] = '\0';
    reverse_string(str, i);
    return i;
}

int snprintf(char *str, size_t size, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    int result = vsnprintf(str, size, format, args);
    va_end(args);
    return result;
}

int sprintf(char *str, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    int result = vsnprintf(str, (size_t)-1, format, args);
    va_end(args);
    return result;
}

int vsnprintf(char *str, size_t size, const char *format, va_list ap)
{
    if (str == NULL || format == NULL || size == 0)
    {
        return 0;
    }

    char *ptr = str;
    size_t remaining = size - 1;

    while (*format != '\0' && remaining > 0)
    {
        if (*format == '%')
        {
            format++;
            switch (*format)
            {
            case 'd':
            case 'i':
            {
                int value = va_arg(ap, int);
                char num_str[32];
                size_t len = int_to_string(value, num_str, 10);
                if (len > remaining)
                {
                    len = remaining;
                }
                memcpy(ptr, num_str, len);
                ptr += len;
                remaining -= len;
                break;
            }
            case 'u':
            {
                unsigned int value = va_arg(ap, unsigned int);
                char num_str[32];
                size_t len = int_to_string((int)value, num_str, 10);
                if (len > remaining)
                {
                    len = remaining;
                }
                memcpy(ptr, num_str, len);
                ptr += len;
                remaining -= len;
                break;
            }
            case 'x':
            case 'X':
            {
                unsigned int value = va_arg(ap, unsigned int);
                char num_str[32];
                size_t len = int_to_string((int)value, num_str, 16);
                if (len > remaining)
                {
                    len = remaining;
                }
                memcpy(ptr, num_str, len);
                ptr += len;
                remaining -= len;
                break;
            }
            case 's':
            {
                char *s = va_arg(ap, char *);
                if (s == NULL)
                {
                    s = "(null)";
                }
                size_t len = strlen(s);
                if (len > remaining)
                {
                    len = remaining;
                }
                memcpy(ptr, s, len);
                ptr += len;
                remaining -= len;
                break;
            }
            case 'c':
            {
                char c = va_arg(ap, int);
                if (remaining > 0)
                {
                    *ptr++ = c;
                    remaining--;
                }
                break;
            }
            case '%':
            {
                if (remaining > 0)
                {
                    *ptr++ = '%';
                    remaining--;
                }
                break;
            }
            default:
                if (remaining > 0)
                {
                    *ptr++ = '%';
                    *ptr++ = *format;
                    remaining -= 2;
                }
                break;
            }
        }
        else
        {
            if (remaining > 0)
            {
                *ptr++ = *format;
                remaining--;
            }
        }
        format++;
    }

    *ptr = '\0';
    return ptr - str;
}

// ===============================================================================
// CHARACTER CLASSIFICATION
// ===============================================================================

int isdigit(int c)
{
    return (c >= '0' && c <= '9');
}

int isalpha(int c)
{
    return ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'));
}

int isalnum(int c)
{
    return (isalpha(c) || isdigit(c));
}

int isspace(int c)
{
    return (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v');
}

int isupper(int c)
{
    return (c >= 'A' && c <= 'Z');
}

int islower(int c)
{
    return (c >= 'a' && c <= 'z');
}

// ===============================================================================
// CHARACTER CONVERSION
// ===============================================================================

int toupper(int c)
{
    if (islower(c))
    {
        return c - 'a' + 'A';
    }
    return c;
}

int tolower(int c)
{
    if (isupper(c))
    {
        return c - 'A' + 'a';
    }
    return c;
}

// ===============================================================================
// STRING CONVERSION FUNCTIONS
// ===============================================================================

char *itoa(int value, char *str, int base)
{
    char *ptr = str, *ptr1 = str, tmp_char;
    int tmp_value;

    if (base < 2 || base > 36)
    {
        *str = '\0';
        return str;
    }

    while (value)
    {
        tmp_value = value;
        value /= base;
        *ptr++ = "0123456789abcdefghijklmnopqrstuvwxyz"[tmp_value - value * base];
    } 
    
    // Apply negative sign for base 10
    if (tmp_value < 0 && base == 10)
        *ptr++ = '-';
    *ptr-- = '\0';

    // Reverse the string
    while (ptr1 < ptr)
    {
        tmp_char = *ptr;
        *ptr-- = *ptr1;
        *ptr1++ = tmp_char;
    }

    return str;
}
