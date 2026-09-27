/* SPDX-License-Identifier: GPL-3.0-only */
#include "test_harness.h"

#include <ir0/elf64_image.h>
#include <stdint.h>
#include <string.h>

struct test_ehdr
{
	unsigned char ident[16];
	uint16_t type, machine;
	uint32_t version;
	uint64_t entry, phoff, shoff;
	uint32_t flags;
	uint16_t ehsize, phentsize, phnum, shentsize, shnum, shstrndx;
};

struct test_phdr
{
	uint32_t type, flags;
	uint64_t offset, vaddr, paddr, filesz, memsz, align;
};

struct load_context
{
	unsigned char memory[128];
	uint32_t flags;
	int mapped;
};

static int supported(void *context, uint16_t machine)
{
	(void)context;
	return machine == 183;
}

static int map_segment(void *opaque, uint64_t va, uint64_t size, uint32_t flags)
{
	struct load_context *context = opaque;

	context->flags = flags;
	context->mapped = 1;
	return va >= 0x1000 && va + size <= 0x1000 + sizeof(context->memory) ? 0 : -1;
}

static int copy_segment(void *opaque, uint64_t va, const void *source, uint64_t size)
{
	struct load_context *context = opaque;

	if (!context->mapped)
		return -1;
	memcpy(context->memory + va - 0x1000, source, (size_t)size);
	return 0;
}

static int zero_segment(void *opaque, uint64_t va, uint64_t size)
{
	struct load_context *context = opaque;

	memset(context->memory + va - 0x1000, 0, (size_t)size);
	return 0;
}

static int exec_or_dyn(void *context, uint16_t type)
{
	(void)context;
	return type == IR0_ELF64_ET_EXEC || type == IR0_ELF64_ET_DYN;
}

static void make_image(unsigned char *blob, size_t length)
{
	struct test_ehdr *ehdr = (struct test_ehdr *)blob;
	struct test_phdr *phdr = (struct test_phdr *)(blob + sizeof(*ehdr));

	memset(blob, 0, length);
	ehdr->ident[0] = 0x7f;
	ehdr->ident[1] = 'E';
	ehdr->ident[2] = 'L';
	ehdr->ident[3] = 'F';
	ehdr->ident[4] = 2;
	ehdr->ident[5] = 1;
	ehdr->type = 2;
	ehdr->machine = 183;
	ehdr->entry = 0x1040;
	ehdr->phoff = sizeof(*ehdr);
	ehdr->ehsize = sizeof(*ehdr);
	ehdr->phentsize = sizeof(*phdr);
	ehdr->phnum = 1;
	phdr->type = 1;
	phdr->flags = IR0_ELF64_PF_R | IR0_ELF64_PF_X;
	phdr->offset = 0;
	phdr->vaddr = 0x1000;
	phdr->filesz = sizeof(*ehdr) + sizeof(*phdr) + 4;
	phdr->memsz = phdr->filesz + 4;
	blob[sizeof(*ehdr) + sizeof(*phdr) + 0] = 0xaa;
	blob[sizeof(*ehdr) + sizeof(*phdr) + 1] = 0xbb;
}

void test_elf64_image_contract(void)
{
	unsigned char blob[256];
	struct test_ehdr *ehdr;
	struct test_phdr *phdr;
	struct load_context context;
	struct ir0_elf64_image image;
	const struct ir0_elf64_image_ops ops = {
		.machine_supported = supported,
		.map_segment = map_segment,
		.copy_segment = copy_segment,
		.zero_segment = zero_segment,
	};

	TEST_BEGIN("elf64_image_contract");
	make_image(blob, sizeof(blob));
	memset(&context, 0xcc, sizeof(context));
	context.mapped = 0;
	ASSERT_EQ(ir0_elf64_image_load(blob, sizeof(blob), &ops, &context, &image), 0);
	ASSERT_EQ(image.entry, 0x1040);
	ASSERT_EQ(image.type, IR0_ELF64_ET_EXEC);
	ASSERT_EQ(image.machine, 183);
	ASSERT_EQ(image.phdr, 0x1000 + sizeof(struct test_ehdr));
	ASSERT_EQ(context.flags, IR0_ELF64_PF_R | IR0_ELF64_PF_X);
	ASSERT_EQ(context.memory[sizeof(struct test_ehdr) + sizeof(struct test_phdr)], 0xaa);
	phdr = (struct test_phdr *)(blob + sizeof(struct test_ehdr));
	ASSERT_EQ(context.memory[phdr->filesz], 0);

	make_image(blob, sizeof(blob));
	ehdr = (struct test_ehdr *)blob;
	ehdr->phoff = UINT64_MAX - 8;
	ASSERT_NE(ir0_elf64_image_load(blob, sizeof(blob), &ops, &context, &image), 0);

	make_image(blob, sizeof(blob));
	phdr = (struct test_phdr *)(blob + sizeof(*ehdr));
	phdr->offset = UINT64_MAX - 2;
	phdr->filesz = 16;
	ASSERT_NE(ir0_elf64_image_load(blob, sizeof(blob), &ops, &context, &image), 0);

	make_image(blob, sizeof(blob));
	phdr = (struct test_phdr *)(blob + sizeof(*ehdr));
	phdr->vaddr = UINT64_MAX - 2;
	phdr->memsz = 16;
	ASSERT_NE(ir0_elf64_image_load(blob, sizeof(blob), &ops, &context, &image), 0);

	make_image(blob, sizeof(blob));
	ehdr = (struct test_ehdr *)blob;
	ehdr->type = IR0_ELF64_ET_DYN;
	ASSERT_NE(ir0_elf64_image_inspect(blob, sizeof(blob), &ops, &context, &image), 0);
	{
		struct ir0_elf64_image_ops dyn_ops = ops;

		dyn_ops.type_supported = exec_or_dyn;
		ASSERT_EQ(ir0_elf64_image_inspect(blob, sizeof(blob), &dyn_ops,
					     &context, &image), 0);
		ASSERT_EQ(image.type, IR0_ELF64_ET_DYN);
	}
	TEST_END();
}
