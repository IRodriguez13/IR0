/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — scatter/gather I/O types (Linux uapi subset).
 */

#ifndef _IR0_UIO_H
#define _IR0_UIO_H

#include <stddef.h>

struct iovec
{
	void *iov_base;
	size_t iov_len;
};

#define IR0_UIO_MAXIOV 1024

#endif /* _IR0_UIO_H */
