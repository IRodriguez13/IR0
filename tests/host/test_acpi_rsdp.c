/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * The RSDP handed over in the boot snapshot is only trusted after its
 * signature and ACPI 1.0 checksum match (ACPI 6.5 §5.2.5.3).
 */

#include "test_harness.h"

#include <string.h>
#include <ir0/acpi_rsdp.h>

static void rsdp_fill(struct acpi_rsdp *rsdp)
{
	const uint8_t *bytes = (const uint8_t *)rsdp;
	uint8_t sum = 0;
	size_t i;

	memset(rsdp, 0, sizeof(*rsdp));
	memcpy(rsdp->signature, ACPI_RSDP_SIGNATURE, sizeof(rsdp->signature));
	memcpy(rsdp->oem_id, "BOCHS ", sizeof(rsdp->oem_id));
	rsdp->rsdt_address = 0x07FE1A2BU;
	for (i = 0; i < ACPI_RSDP_V1_SIZE; i++)
		sum = (uint8_t)(sum + bytes[i]);
	rsdp->checksum = (uint8_t)(0U - sum);
}

void test_acpi_rsdp_validation(void)
{
	struct acpi_rsdp rsdp;

	TEST_BEGIN("acpi_rsdp_validation");

	ASSERT_EQ(sizeof(struct acpi_rsdp), 36U);
	ASSERT(!acpi_rsdp_valid(NULL));

	rsdp_fill(&rsdp);
	ASSERT(acpi_rsdp_valid(&rsdp));

	/* Bytes past the v1 structure are outside the v1 checksum. */
	rsdp.xsdt_address = 0x1234U;
	ASSERT(acpi_rsdp_valid(&rsdp));

	rsdp_fill(&rsdp);
	rsdp.rsdt_address ^= 1U;
	ASSERT(!acpi_rsdp_valid(&rsdp));

	rsdp_fill(&rsdp);
	rsdp.signature[7] = '_';
	ASSERT(!acpi_rsdp_valid(&rsdp));

	TEST_END();
}
