/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * File: acpi_pm_stub.c
 * Description: ACPI PM facades are x86/legacy-table specific; ARM64 returns honest errno.
 */

#include <ir0/acpi_pm.h>
#include <ir0/errno.h>

int ir0_acpi_pm_init(void)
{
	return -ENODEV;
}

int ir0_acpi_pm_try_poweroff(void)
{
	return -ENODEV;
}

int ir0_acpi_pm_try_suspend(void)
{
	return -EINVAL;
}

int ir0_acpi_pm_s3_ok(void)
{
	return 0;
}
