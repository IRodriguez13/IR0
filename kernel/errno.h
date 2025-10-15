#pragma once

#include <errno.h>

#define ERRNO_MAX EHWPOISON

const char *strerror(int errnum);

