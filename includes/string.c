/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: string.c
 * Description: IR0 kernel source/header file
 */

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
#define likely(x) __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)

/* One byte per lane, used by the word-at-a-time helpers. */
typedef unsigned long __attribute__((may_alias)) word_t;
#define WORD_BYTES sizeof(word_t)
#define WORD_ONES (~(word_t)0 / 0xff)
#define WORD_HIGHS (WORD_ONES << 7)

/*
 * True when at least one byte of the word is zero.  The classic
 * (word - 0x01..) & ~word & 0x80.. test only inspects lanes whose
 * subtraction did not borrow across the byte boundary.
 */
static inline int word_has_zero(word_t word)
{
    return ((word - WORD_ONES) & ~word & WORD_HIGHS) != 0;
}

/**
 * strlen - calculate length of string
 * @str: string to measure
 *
 * Returns length of string excluding null terminator.
 */
size_t strlen(const char *str)
{
    const char *p = str;

    if (unlikely(!str))
        return 0;

    /* Align first so a word load never crosses a page boundary. */
    while ((uintptr_t)p & (WORD_BYTES - 1))
    {
        if (*p == '\0')
            return (size_t)(p - str);
        p++;
    }

    for (;;)
    {
        word_t word = *(const word_t *)p;

        if (word_has_zero(word))
        {
            while (*p != '\0')
                p++;
            return (size_t)(p - str);
        }
        p += WORD_BYTES;
    }
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

    while (*s1 && (*s1 == *s2))
    {
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

    while (n && *s1 && (*s1 == *s2))
    {
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
    size_t len = strlen(src);

    if (len >= n)
    {
        if (n > 0)
            memcpy(dest, src, n);
    }
    else
    {
        memcpy(dest, src, len);
        memset(dest + len, '\0', n - len);
    }

    return dest;
}

char *strncat(char *dest, const char *src, size_t n)
{
    size_t dest_len = strlen(dest);
    size_t src_len = strlen(src);

    if (src_len > n)
        src_len = n;

    memcpy(dest + dest_len, src, src_len);
    dest[dest_len + src_len] = '\0';
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
    unsigned char byte = (unsigned char)value;
    word_t word;

    if (num == 0)
        return ptr;

    while (num > 0 && ((uintptr_t)p & (WORD_BYTES - 1)))
    {
        *p++ = byte;
        num--;
    }

    word = (word_t)byte * WORD_ONES;
    while (num >= WORD_BYTES)
    {
        *(word_t *)p = word;
        p += WORD_BYTES;
        num -= WORD_BYTES;
    }
    while (num-- > 0)
        *p++ = byte;

    return ptr;
}

void *memcpy(void *dest, const void *src, size_t num)
{
    unsigned char *d = (unsigned char *)dest;
    const unsigned char *s = (const unsigned char *)src;

    if (num == 0)
        return dest;

    /*
     * Word copies need both pointers aligned.  Walk a matching offset
     * down to the next word boundary, then copy whole words; a pair with
     * different offsets stays on the byte path.
     */
    if (((uintptr_t)d & (WORD_BYTES - 1)) ==
        ((uintptr_t)s & (WORD_BYTES - 1)))
    {
        while (num > 0 && ((uintptr_t)d & (WORD_BYTES - 1)))
        {
            *d++ = *s++;
            num--;
        }
        while (num >= WORD_BYTES)
        {
            *(word_t *)d = *(const word_t *)s;
            d += WORD_BYTES;
            s += WORD_BYTES;
            num -= WORD_BYTES;
        }
    }

    while (num-- > 0)
        *d++ = *s++;

    return dest;
}

void *memmove(void *dest, const void *src, size_t num)
{
    unsigned char *d = (unsigned char *)dest;
    const unsigned char *s = (const unsigned char *)src;

    if (num == 0 || d == s)
    {
        return dest;
    }

    if (d < s)
    {
        /* Disjoint ranges keep the faster memcpy paths; subtraction
         * avoids overflowing the end address. */
        if ((uintptr_t)s - (uintptr_t)d >= num)
            return memcpy(dest, src, num);

        while (num-- > 0)
        {
            *d++ = *s++;
        }
    }
    else
    {
        if ((uintptr_t)d - (uintptr_t)s >= num)
            return memcpy(dest, src, num);

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
    word_t w1;
    word_t w2;

    /*
     * Equal offsets can be aligned together; after that the byte loop
     * only sees the head and tail.  A differing result falls through to
     * the byte loop from the first mismatching word.
     */
    if (((uintptr_t)p1 & (WORD_BYTES - 1)) ==
        ((uintptr_t)p2 & (WORD_BYTES - 1)))
    {
        while (num > 0 && ((uintptr_t)p1 & (WORD_BYTES - 1)))
        {
            if (*p1 != *p2)
                return *p1 - *p2;
            p1++;
            p2++;
            num--;
        }
        while (num >= WORD_BYTES)
        {
            w1 = *(const word_t *)p1;
            w2 = *(const word_t *)p2;
            if (w1 != w2)
                break;
            p1 += WORD_BYTES;
            p2 += WORD_BYTES;
            num -= WORD_BYTES;
        }
    }

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

unsigned long strtoul(const char *str, char **endptr, int base)
{
    const char *start = str;
    unsigned long result = 0;
    const unsigned long max = ~0UL;
    int negative = 0;
    int converted = 0;

    if (!str)
    {
        if (endptr)
            *endptr = NULL;
        return 0;
    }

    while (*str == ' ' || *str == '\t' || *str == '\n')
        str++;

    if (*str == '-' || *str == '+')
    {
        negative = *str == '-';
        str++;
    }

    if (base != 0 && (base < 2 || base > 36))
    {
        if (endptr)
            *endptr = (char *)start;
        return 0;
    }

    if (base == 0)
    {
        base = *str == '0' ? 8 : 10;
        if (str[0] == '0' && (str[1] == 'x' || str[1] == 'X'))
        {
            int digit = str[2] >= '0' && str[2] <= '9'
                            ? str[2] - '0'
                            : (str[2] >= 'a' && str[2] <= 'f')
                                  ? str[2] - 'a' + 10
                                  : (str[2] >= 'A' && str[2] <= 'F')
                                        ? str[2] - 'A' + 10
                                        : -1;

            if (digit >= 0)
            {
                base = 16;
                str += 2;
            }
        }
    }
    else if (base == 16 && str[0] == '0' &&
             (str[1] == 'x' || str[1] == 'X'))
    {
        int digit = str[2] >= '0' && str[2] <= '9'
                        ? str[2] - '0'
                        : (str[2] >= 'a' && str[2] <= 'f')
                              ? str[2] - 'a' + 10
                              : (str[2] >= 'A' && str[2] <= 'F')
                                    ? str[2] - 'A' + 10
                                    : -1;

        if (digit >= 0)
            str += 2;
    }

    while (*str)
    {
        int digit;

        if (*str >= '0' && *str <= '9')
            digit = *str - '0';
        else if (*str >= 'a' && *str <= 'z')
            digit = *str - 'a' + 10;
        else if (*str >= 'A' && *str <= 'Z')
            digit = *str - 'A' + 10;
        else
            break;

        if (digit >= base)
            break;

        converted = 1;
        if (result > (max - (unsigned long)digit) / (unsigned long)base)
            result = max;
        else
            result = result * (unsigned long)base + (unsigned long)digit;
        str++;
    }

    if (endptr)
        *endptr = (char *)(converted ? str : start);

    return negative ? 0UL - result : result;
}

long strtol(const char *str, char **endptr, int base)
{
    return (long)strtoul(str, endptr, base);
}

// ===============================================================================
// STRING FORMATTING
// ===============================================================================

/* Full-width unsigned — used by %u/%x and %l / %ll modifiers. */
static int u64_to_string(uint64_t value, char *str, int base, int upper)
{
	static const char dig_lo[] = "0123456789abcdef";
	static const char dig_up[] = "0123456789ABCDEF";
	const char *dig = upper ? dig_up : dig_lo;
	char rev[32];
	int n = 0;
	int i;

	if (base < 2 || base > 16)
		base = 10;

	if (value == 0)
	{
		str[0] = '0';
		str[1] = '\0';
		return 1;
	}

	while (value > 0 && n < (int)sizeof(rev))
	{
		rev[n++] = dig[value % (uint64_t)base];
		value /= (uint64_t)base;
	}

	for (i = 0; i < n; i++)
		str[i] = rev[n - 1 - i];
	str[n] = '\0';
	return n;
}

static int pointer_to_string(uintptr_t value, char *str)
{
    const char *hex = "0123456789abcdef";
    int pos = 0;
    int started = 0;

    str[pos++] = '0';
    str[pos++] = 'x';

    for (int nibble = (int)(sizeof(uintptr_t) * 2) - 1; nibble >= 0; nibble--)
    {
        uint8_t digit = (uint8_t)((value >> (nibble * 4)) & 0x0FULL);
        if (digit != 0 || started || nibble == 0)
        {
            str[pos++] = hex[digit];
            started = 1;
        }
    }

    str[pos] = '\0';
    return pos;
}

int snprintf(char *str, size_t size, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    int result = vsnprintf(str, size, format, args);
    va_end(args);
    return result;
}

/*
 * Emit a text field with its width padding.  Every write is bounded by
 * *remaining, so a caller may pass any width and the buffer stays intact.
 */
static char *emit_text(char *ptr, size_t *remaining, const char *text,
                       size_t len, int width, int left, char pad)
{
    size_t pad_len = width > (int)len ? (size_t)(width - (int)len) : 0;
    size_t i;

    if (!left)
    {
        for (i = 0; i < pad_len && *remaining > 0; i++)
        {
            *ptr++ = pad;
            (*remaining)--;
        }
    }

    if (len > *remaining)
        len = *remaining;
    if (len > 0)
    {
        memcpy(ptr, text, len);
        ptr += len;
        *remaining -= len;
    }

    if (left)
    {
        for (i = 0; i < pad_len && *remaining > 0; i++)
        {
            *ptr++ = ' ';
            (*remaining)--;
        }
    }

    return ptr;
}

/*
 * Emit an integer with sign, precision, zero padding, and width.  Digits
 * arrive most-significant first and without a sign.
 */
static char *emit_number(char *ptr, size_t *remaining, const char *digits,
                         int ndigits, int negative, int width, int precision,
                         int zero, int left)
{
    int zeros = 0;
    int total;
    int pad;
    int i;

    /* "%.0d" of zero prints nothing. */
    if (precision >= 0 && ndigits == 1 && digits[0] == '0' && precision == 0)
        ndigits = 0;

    if (precision > ndigits)
        zeros = precision - ndigits;
    else if (zero && !left)
    {
        int need = width - (negative ? 1 : 0) - ndigits;

        if (need > 0)
            zeros = need;
    }

    total = (negative ? 1 : 0) + zeros + ndigits;
    pad = (!left && width > total) ? width - total : 0;

    if (!left)
    {
        for (i = 0; i < pad && *remaining > 0; i++)
        {
            *ptr++ = ' ';
            (*remaining)--;
        }
    }
    if (negative && *remaining > 0)
    {
        *ptr++ = '-';
        (*remaining)--;
    }
    for (i = 0; i < zeros && *remaining > 0; i++)
    {
        *ptr++ = '0';
        (*remaining)--;
    }
    if (ndigits > 0)
    {
        size_t n = (size_t)ndigits < *remaining ? (size_t)ndigits : *remaining;

        if (n > 0)
        {
            memcpy(ptr, digits, n);
            ptr += n;
            *remaining -= n;
        }
    }
    if (left)
    {
        for (i = 0; i < pad && *remaining > 0; i++)
        {
            *ptr++ = ' ';
            (*remaining)--;
        }
    }

    return ptr;
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
        if (*format != '%')
        {
            *ptr++ = *format++;
            remaining--;
            continue;
        }

        format++;
        int width = 0;
        int precision = -1;
        int left = 0;
        int zero = 0;

        for (;;)
        {
            if (*format == '-')
            {
                left = 1;
                format++;
            }
            else if (*format == '0')
            {
                zero = 1;
                format++;
            }
            else
            {
                break;
            }
        }

        while (*format >= '0' && *format <= '9')
        {
            if (width < 1000000)
                width = width * 10 + (*format - '0');
            format++;
        }

        if (*format == '.')
        {
            format++;
            precision = 0;
            while (*format >= '0' && *format <= '9')
            {
                if (precision < 1000000)
                    precision = precision * 10 + (*format - '0');
                format++;
            }
        }

        /* Length: l / ll (and z as size_t). Missing these → literal "lu"/"lx". */
        int long_count = 0;
        int is_size_t = 0;

        if (*format == 'z')
        {
            is_size_t = 1;
            format++;
        }
        else
        {
            while (*format == 'l' && long_count < 2)
            {
                long_count++;
                format++;
            }
        }

        switch (*format)
        {
        case 'd':
        case 'i':
        {
            char num_str[32];
            int64_t value;
            uint64_t magnitude;
            int negative;
            int len;

            if (long_count >= 2)
                value = va_arg(ap, long long);
            else if (long_count == 1)
                value = va_arg(ap, long);
            else
                value = va_arg(ap, int);

            negative = value < 0;
            /* Negate in unsigned arithmetic: -INT64_MIN overflows. */
            magnitude = negative ? (uint64_t)(-(value + 1)) + 1u
                                 : (uint64_t)value;
            len = u64_to_string(magnitude, num_str, 10, 0);
            ptr = emit_number(ptr, &remaining, num_str, len, negative,
                              width, precision, zero, left);
            break;
        }
        case 'u':
        {
            char num_str[32];
            uint64_t value;
            int len;

            if (is_size_t)
                value = (uint64_t)va_arg(ap, size_t);
            else if (long_count >= 2)
                value = (uint64_t)va_arg(ap, unsigned long long);
            else if (long_count == 1)
                value = (uint64_t)va_arg(ap, unsigned long);
            else
                value = (uint64_t)va_arg(ap, unsigned int);
            len = u64_to_string(value, num_str, 10, 0);
            ptr = emit_number(ptr, &remaining, num_str, len, 0,
                              width, precision, zero, left);
            break;
        }
        case 'x':
        case 'X':
        {
            char num_str[32];
            uint64_t value;
            int upper = (*format == 'X');
            int len;

            if (is_size_t)
                value = (uint64_t)va_arg(ap, size_t);
            else if (long_count >= 2)
                value = (uint64_t)va_arg(ap, unsigned long long);
            else if (long_count == 1)
                value = (uint64_t)va_arg(ap, unsigned long);
            else
                value = (uint64_t)va_arg(ap, unsigned int);
            len = u64_to_string(value, num_str, 16, upper);
            ptr = emit_number(ptr, &remaining, num_str, len, 0,
                              width, precision, zero, left);
            break;
        }
        case 'p':
        {
            uintptr_t value = (uintptr_t)va_arg(ap, void *);
            char ptr_str[2 + (sizeof(uintptr_t) * 2) + 1];
            int len = pointer_to_string(value, ptr_str);

            ptr = emit_text(ptr, &remaining, ptr_str, (size_t)len,
                            width, left, ' ');
            break;
        }
        case 's':
        {
            const char *s = va_arg(ap, const char *);
            size_t len;

            if (s == NULL)
                s = "(null)";
            len = strlen(s);
            if (precision >= 0 && (size_t)precision < len)
                len = (size_t)precision;
            ptr = emit_text(ptr, &remaining, s, len, width, left, ' ');
            break;
        }
        case 'c':
        {
            char c = (char)va_arg(ap, int);

            ptr = emit_text(ptr, &remaining, &c, 1, width, left, ' ');
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
        case '\0':
        {
            /* Trailing '%' in the format string. */
            if (remaining > 0)
            {
                *ptr++ = '%';
                remaining--;
            }
            break;
        }
        default:
        {
            if (remaining > 0)
            {
                *ptr++ = '%';
                remaining--;
            }
            if (remaining > 0)
            {
                *ptr++ = *format;
                remaining--;
            }
            break;
        }
        }

        if (*format != '\0')
            format++;
    }

    *ptr = '\0';
    return ptr - str;
}
