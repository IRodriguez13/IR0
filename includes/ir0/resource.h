/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 - Linux struct rlimit, rusage (musl compatibility)
 */
#ifndef _IR0_RESOURCE_H
#define _IR0_RESOURCE_H

#include <stdint.h>
#include <ir0/time.h>

typedef unsigned long long rlim_t;

struct rlimit {
    rlim_t rlim_cur;
    rlim_t rlim_max;
};

struct rusage {
    struct timeval ru_utime;
    struct timeval ru_stime;
    long ru_maxrss;
    long ru_ixrss;
    long ru_idrss;
    long ru_isrss;
    long ru_minflt;
    long ru_majflt;
    long ru_nswap;
    long ru_inblock;
    long ru_oublock;
    long ru_msgsnd;
    long ru_msgrcv;
    long ru_nsignals;
    long ru_nvcsw;
    long ru_nivcsw;
    long __reserved[16];
};

/* RLIMIT_* constants */
#define RLIMIT_CPU      0
#define RLIMIT_FSIZE    1
#define RLIMIT_DATA     2
#define RLIMIT_STACK    3
#define RLIMIT_CORE     4
#define RLIMIT_NOFILE   7
#define RLIMIT_AS       9
#define RLIMIT_INFINITY ((rlim_t)-1)

/* RUSAGE_* for getrusage */
#define RUSAGE_SELF     0
#define RUSAGE_CHILDREN (-1)

#endif /* _IR0_RESOURCE_H */
