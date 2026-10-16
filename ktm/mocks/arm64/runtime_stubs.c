/* SPDX-License-Identifier: GPL-3.0-only */
/** Remaining test-only services for the staged ARM64 scheduler harness. */

#include <ir0/process.h>
#include <ir0/boot_log.h>

void handle_signals(void)
{
}

int signals_should_handle_on_run(process_t *p)
{
	(void)p;
	return 0;
}

void klog_info(const char *component, const char *message)
{
	ir0_boot_info(component, message);
}
