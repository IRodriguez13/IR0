#ifndef _STDBOOL_H
#define _STDBOOL_H

/* Boolean type and values for IR0 Kernel - Freestanding implementation */

/* C99 boolean type support */
#ifndef __cplusplus

/* Define the boolean type */
#if !defined(__STDC_VERSION__) || __STDC_VERSION__ < 199901L
    /* Pre-C99 compilers */
    typedef unsigned char _Bool;
    #define bool _Bool
#else
    /* C99 and later - _Bool is a keyword */
    #define bool _Bool
#endif

/* Boolean constants */
#define true    1
#define false   0

/* Indicate that bool, true, false are available */
#define __bool_true_false_are_defined   1

#else
    /* C++ has built-in bool, true, false */
    /* No need to define anything */
#endif

#endif /* _STDBOOL_H */