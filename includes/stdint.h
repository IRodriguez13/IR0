#ifndef _STDINT_H
#define _STDINT_H

/* Integer types for IR0 Kernel - Freestanding implementation */
/* Architecture-aware definitions */

/* Exact-width integer types */
typedef signed char        int8_t;
typedef short              int16_t;  
typedef int                int32_t;
typedef long long          int64_t;

typedef unsigned char      uint8_t;
typedef unsigned short     uint16_t;
typedef unsigned int       uint32_t; 
typedef unsigned long long uint64_t;

/* Minimum-width integer types */
typedef int8_t   int_least8_t;
typedef int16_t  int_least16_t;
typedef int32_t  int_least32_t;
typedef int64_t  int_least64_t;

typedef uint8_t  uint_least8_t;
typedef uint16_t uint_least16_t;
typedef uint32_t uint_least32_t;
typedef uint64_t uint_least64_t;

/* Fastest minimum-width integer types */
typedef int8_t   int_fast8_t;
typedef int32_t  int_fast16_t;
typedef int32_t  int_fast32_t;
typedef int64_t  int_fast64_t;

typedef uint8_t  uint_fast8_t;
typedef uint32_t uint_fast16_t;
typedef uint32_t uint_fast32_t;
typedef uint64_t uint_fast64_t;

/* Architecture-dependent pointer types */
#if defined(__x86_64__) || defined(__amd64__) || defined(_M_X64)
    /* 64-bit architecture */
    typedef long          intptr_t;
    typedef unsigned long uintptr_t;
    #define INTPTR_MIN  (-9223372036854775807L - 1)
    #define INTPTR_MAX  (9223372036854775807L)
    #define UINTPTR_MAX (18446744073709551615UL)
#elif defined(__i386__) || defined(_M_IX86)
    /* 32-bit architecture */
    typedef int           intptr_t;
    typedef unsigned int  uintptr_t;
    #define INTPTR_MIN  (-2147483647 - 1)
    #define INTPTR_MAX  (2147483647)
    #define UINTPTR_MAX (4294967295U)
#else
    #error "Unsupported architecture for pointer types"
#endif

/* Greatest-width integer types */
typedef long long          intmax_t;
typedef unsigned long long uintmax_t;

/* Limits of exact-width integer types */
#define INT8_MIN    (-128)
#define INT8_MAX    (127)
#define INT16_MIN   (-32768)
#define INT16_MAX   (32767)
#define INT32_MIN   (-2147483647 - 1)
#define INT32_MAX   (2147483647)
#define INT64_MIN   (-9223372036854775807LL - 1)
#define INT64_MAX   (9223372036854775807LL)

#define UINT8_MAX   (255)
#define UINT16_MAX  (65535)
#define UINT32_MAX  (4294967295U)
#define UINT64_MAX  (18446744073709551615ULL)

/* Limits of other integer types */
#define PTRDIFF_MIN INTPTR_MIN
#define PTRDIFF_MAX INTPTR_MAX
#define SIZE_MAX    UINTPTR_MAX

/* Limits of greatest-width integer types */
#define INTMAX_MIN  INT64_MIN
#define INTMAX_MAX  INT64_MAX
#define UINTMAX_MAX UINT64_MAX

/* Macros for integer constants */
#define INT8_C(c)   c
#define INT16_C(c)  c
#define INT32_C(c)  c
#define INT64_C(c)  c ## LL

#define UINT8_C(c)  c
#define UINT16_C(c) c
#define UINT32_C(c) c ## U
#define UINT64_C(c) c ## ULL

#define INTMAX_C(c)  INT64_C(c)
#define UINTMAX_C(c) UINT64_C(c)

#endif /* _STDINT_H */