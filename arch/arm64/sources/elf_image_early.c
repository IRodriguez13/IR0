/* SPDX-License-Identifier: GPL-3.0-only */
/** ARM64 provider for the common in-memory ELF64 loader. */

#include "elf_image_early.h"
#include "mmu_early.h"

#include <ir0/arch_elf.h>

#define PAGE_SIZE 4096UL

void arm64_elf_zero_early(uint64_t address, uint64_t length)
{
	volatile uint8_t *p = (volatile uint8_t *)(uintptr_t)address;

	while (length--)
		*p++ = 0;
}

static int machine_supported(void *context, uint16_t machine)
{
	(void)context;
	return elf_machine_supported(machine);
}

static int map_segment(void *context, uint64_t vaddr, uint64_t memsz,
		       uint32_t flags)
{
	uint64_t page;
	uint64_t end = vaddr + memsz;

	(void)context;
	if (end < vaddr)
		return -1;
	for (page = vaddr & ~(PAGE_SIZE - 1UL); page < end; page += PAGE_SIZE)
	{
		if (arm64_mmu_map_user_page_flags(page,
						 (flags & IR0_ELF64_PF_X) != 0) != 0)
			return -1;
	}
	return 0;
}

static int copy_segment(void *context, uint64_t vaddr, const void *source,
			uint64_t length)
{
	uint8_t *dst = (uint8_t *)(uintptr_t)vaddr;
	const uint8_t *src = source;

	(void)context;
	while (length--)
		*dst++ = *src++;
	return 0;
}

static int zero_segment(void *context, uint64_t vaddr, uint64_t length)
{
	(void)context;
	arm64_elf_zero_early(vaddr, length);
	return 0;
}

int arm64_elf_image_load_early(const void *blob, size_t blob_length,
			       struct ir0_elf64_image *image)
{
	static const struct ir0_elf64_image_ops ops = {
		.machine_supported = machine_supported,
		.map_segment = map_segment,
		.copy_segment = copy_segment,
		.zero_segment = zero_segment,
	};

	return ir0_elf64_image_load(blob, blob_length, &ops, 0, image);
}
