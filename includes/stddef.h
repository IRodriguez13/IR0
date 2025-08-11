// Por ahora las repito antes de hacer refactor por cuestiones de economía y ver qué necesito de las libs std.

#ifndef _STDDEF_H
#define _STDDEF_H

/* Standard definitions for IR0 Kernel - Freestanding implementation */
/* Architecture-aware definitions */

/* NULL pointer constant */
#ifndef NULL
#ifdef __cplusplus
#define NULL 0
#else
#define NULL ((void*)0)
#endif
#endif

/* Architecture-dependent size and pointer difference types */
#if defined(__x86_64__) || defined(__amd64__) || defined(_M_X64)
    /* 64-bit architecture */
    typedef unsigned long size_t;
    typedef long ptrdiff_t;
#elif defined(__i386__) || defined(_M_IX86)
    /* 32-bit architecture */
    typedef unsigned int size_t;
    typedef int ptrdiff_t;
#else
    #error "Unsupported architecture for size_t and ptrdiff_t"
#endif

/* wchar_t - wide character type */
#ifndef __cplusplus
typedef int wchar_t;
#endif

/* offsetof macro - offset of member in structure */
#define offsetof(type, member) __builtin_offsetof(type, member)

/* max_align_t - type with strictest alignment requirement */
typedef struct {
    long long __ll;
    long double __ld;
} max_align_t;

#endif /* _STDDEF_H */
