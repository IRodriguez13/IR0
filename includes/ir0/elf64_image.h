/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef IR0_ELF64_IMAGE_H
#define IR0_ELF64_IMAGE_H

#include <stddef.h>
#include <stdint.h>

#define IR0_ELF64_PF_X 1U
#define IR0_ELF64_PF_W 2U
#define IR0_ELF64_PF_R 4U

struct ir0_elf64_image_ops
{
	int (*machine_supported)(void *context, uint16_t machine);
	int (*map_segment)(void *context, uint64_t vaddr, uint64_t memsz,
			   uint32_t flags);
	int (*copy_segment)(void *context, uint64_t vaddr, const void *source,
			    uint64_t length);
	int (*zero_segment)(void *context, uint64_t vaddr, uint64_t length);
};

struct ir0_elf64_image
{
	uint64_t entry;
	uint64_t phdr;
	uint16_t phent;
	uint16_t phnum;
};

int ir0_elf64_image_load(const void *blob, size_t blob_length,
			 const struct ir0_elf64_image_ops *ops, void *context,
			 struct ir0_elf64_image *image);

#endif
