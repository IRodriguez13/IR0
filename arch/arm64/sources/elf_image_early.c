/* SPDX-License-Identifier: GPL-3.0-only */
/** ARM64 provider for the common in-memory ELF64 loader. */

#include "elf_image_early.h"
#include "mmu_early.h"

#include <ir0/arch_elf.h>

#define PAGE_SIZE 4096UL
#define ARM64_ELF_SEGMENT_MAX 32U

struct arm64_elf_segment
{
	uint64_t start;
	uint64_t end;
	uint32_t flags;
};

struct arm64_elf_load_context
{
	uint64_t root_ttbr;
	unsigned int segment_count;
	struct arm64_elf_segment segments[ARM64_ELF_SEGMENT_MAX];
};

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
	struct arm64_elf_load_context *load_context = context;
	uint64_t root_ttbr;
	uint64_t page;
	uint64_t end = vaddr + memsz;

	if (!load_context || !load_context->root_ttbr)
		return -1;
	root_ttbr = load_context->root_ttbr;
	if (end < vaddr)
		return -1;
	if (memsz == 0)
		return 0;
	if (load_context->segment_count >= ARM64_ELF_SEGMENT_MAX)
		return -1;
	for (page = vaddr & ~(PAGE_SIZE - 1UL); page < end; page += PAGE_SIZE)
	{
		if (arm64_mmu_map_user_page_at_flags_in_root(root_ttbr, page, page,
							     1, 0) != 0)
			return -1;
	}
	load_context->segments[load_context->segment_count].start =
		vaddr & ~(PAGE_SIZE - 1UL);
	load_context->segments[load_context->segment_count].end = end;
	load_context->segments[load_context->segment_count].flags = flags;
	load_context->segment_count++;
	return 0;
}

static int seal_image(void *context)
{
	struct arm64_elf_load_context *load_context = context;
	unsigned int i;

	if (!load_context || !load_context->root_ttbr)
		return -1;
	for (i = 0; i < load_context->segment_count; i++)
	{
		uint64_t page;

		for (page = load_context->segments[i].start;
		     page < load_context->segments[i].end; page += PAGE_SIZE)
		{
			unsigned int j;
			uint32_t flags = 0;

			for (j = 0; j < load_context->segment_count; j++)
			{
				if (page < load_context->segments[j].start ||
				    page >= load_context->segments[j].end)
					continue;
				flags |= load_context->segments[j].flags;
			}
			if (arm64_mmu_protect_user_range_in_root(load_context->root_ttbr,
				page, PAGE_SIZE, (flags & IR0_ELF64_PF_W) != 0,
				(flags & IR0_ELF64_PF_X) != 0) != 0)
				return -1;
		}
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
	return arm64_elf_image_load_early_in_root(blob, blob_length,
					     arm64_mmu_root_a(), image);
}

int arm64_elf_image_load_early_in_root(const void *blob, size_t blob_length,
				       uint64_t root_ttbr,
				       struct ir0_elf64_image *image)
{
	static const struct ir0_elf64_image_ops ops = {
		.machine_supported = machine_supported,
		.map_segment = map_segment,
		.copy_segment = copy_segment,
		.zero_segment = zero_segment,
		.seal_image = seal_image,
	};
	struct arm64_elf_load_context context;

	context.root_ttbr = root_ttbr;
	context.segment_count = 0;

	if (!root_ttbr ||
	    ir0_elf64_image_load(blob, blob_length, &ops,
				 &context, image) != 0)
		return -1;
	return image->phdr != 0 ? 0 : -1;
}
