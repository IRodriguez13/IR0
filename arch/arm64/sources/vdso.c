/* SPDX-License-Identifier: GPL-3.0-only */
/* ARM64 vDSO image provider; no image is shipped yet. */
#include <stddef.h>
#include <stdint.h>

const uint8_t *vdso_blob_start(void)
{
	return NULL;
}

size_t vdso_blob_size(void)
{
	return 0;
}
