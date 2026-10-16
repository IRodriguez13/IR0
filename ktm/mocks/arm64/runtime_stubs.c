/* SPDX-License-Identifier: GPL-3.0-only */
/** Remaining test-only services for the staged ARM64 scheduler harness. */

#include <ir0/boot_log.h>

void klog_info(const char *component, const char *message)
{
	ir0_boot_info(component, message);
}
