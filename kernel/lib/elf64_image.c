/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026 Iván Rodriguez
 *
 * ISA-neutral loader for an in-memory static ELF64 executable.  Address-space
 * policy and machine validation stay behind callbacks so early boot and the
 * production exec path can share bounds checking without sharing page tables.
 */

#include <ir0/elf64_image.h>

#define ELF64_EI_NIDENT 16U
#define ELF64_PT_LOAD 1U
#define ELF64_PT_PHDR 6U

struct elf64_ehdr
{
	unsigned char e_ident[ELF64_EI_NIDENT];
	uint16_t e_type;
	uint16_t e_machine;
	uint32_t e_version;
	uint64_t e_entry;
	uint64_t e_phoff;
	uint64_t e_shoff;
	uint32_t e_flags;
	uint16_t e_ehsize;
	uint16_t e_phentsize;
	uint16_t e_phnum;
	uint16_t e_shentsize;
	uint16_t e_shnum;
	uint16_t e_shstrndx;
};

struct elf64_phdr
{
	uint32_t p_type;
	uint32_t p_flags;
	uint64_t p_offset;
	uint64_t p_vaddr;
	uint64_t p_paddr;
	uint64_t p_filesz;
	uint64_t p_memsz;
	uint64_t p_align;
};

static int range_valid(uint64_t offset, uint64_t length, uint64_t limit)
{
	return offset <= limit && length <= limit - offset;
}

static int inspect_header(const void *blob_ptr, size_t blob_length,
			  const struct ir0_elf64_image_ops *ops, void *context,
			  struct ir0_elf64_image *image)
{
	const unsigned char *blob = blob_ptr;
	const struct elf64_ehdr *ehdr;
	uint64_t phdr_bytes;
	uint64_t phdr_end;
	uint64_t phdr_va = 0;
	uint16_t i;

	if (!blob || !ops || !image || !ops->machine_supported ||
	    blob_length < sizeof(*ehdr))
		return -1;
	ehdr = (const struct elf64_ehdr *)blob;
	if (ehdr->e_ident[0] != 0x7f || ehdr->e_ident[1] != 'E' ||
	    ehdr->e_ident[2] != 'L' || ehdr->e_ident[3] != 'F' ||
	    ehdr->e_ident[4] != 2 || ehdr->e_ident[5] != 1 ||
	    ehdr->e_ehsize < sizeof(*ehdr) ||
	    ehdr->e_phentsize != sizeof(struct elf64_phdr) || ehdr->e_phnum == 0 ||
	    (ops->type_supported && !ops->type_supported(context, ehdr->e_type)) ||
	    (!ops->type_supported && ehdr->e_type != IR0_ELF64_ET_EXEC) ||
	    !ops->machine_supported(context, ehdr->e_machine))
		return -1;

	phdr_bytes = (uint64_t)ehdr->e_phnum * ehdr->e_phentsize;
	if (!range_valid(ehdr->e_phoff, phdr_bytes, blob_length))
		return -1;
	phdr_end = ehdr->e_phoff + phdr_bytes;

	image->type = ehdr->e_type;
	image->machine = ehdr->e_machine;
	image->entry = ehdr->e_entry;
	image->phdr = 0;
	image->phent = ehdr->e_phentsize;
	image->phnum = ehdr->e_phnum;

	for (i = 0; i < ehdr->e_phnum; i++)
	{
		const struct elf64_phdr *ph;
		uint64_t offset = ehdr->e_phoff + (uint64_t)i * ehdr->e_phentsize;

		ph = (const struct elf64_phdr *)(blob + offset);
		if (ph->p_type == ELF64_PT_PHDR)
			phdr_va = ph->p_vaddr;
		if (ph->p_type != ELF64_PT_LOAD)
			continue;
		if (ph->p_memsz < ph->p_filesz ||
		    !range_valid(ph->p_offset, ph->p_filesz, blob_length) ||
		    ph->p_vaddr + ph->p_memsz < ph->p_vaddr)
			return -1;
		if (ehdr->e_phoff >= ph->p_offset &&
		    phdr_end <= ph->p_offset + ph->p_filesz)
			phdr_va = ph->p_vaddr + (ehdr->e_phoff - ph->p_offset);
	}

	image->phdr = phdr_va;
	return 0;
}

int ir0_elf64_image_inspect(const void *blob, size_t blob_length,
			    const struct ir0_elf64_image_ops *ops, void *context,
			    struct ir0_elf64_image *image)
{
	return inspect_header(blob, blob_length, ops, context, image);
}

int ir0_elf64_image_load(const void *blob_ptr, size_t blob_length,
			 const struct ir0_elf64_image_ops *ops, void *context,
			 struct ir0_elf64_image *image)
{
	const unsigned char *blob = blob_ptr;
	const struct elf64_ehdr *ehdr;
	uint16_t i;

	if (!ops || !ops->map_segment || !ops->copy_segment || !ops->zero_segment ||
	    inspect_header(blob_ptr, blob_length, ops, context, image) != 0)
		return -1;
	ehdr = (const struct elf64_ehdr *)blob;

	/* Linux-style ordering: establish the complete image before populating it. */
	for (i = 0; i < ehdr->e_phnum; i++)
	{
		const struct elf64_phdr *ph = (const struct elf64_phdr *)(
			blob + ehdr->e_phoff + (uint64_t)i * ehdr->e_phentsize);

		if (ph->p_type == ELF64_PT_LOAD &&
		    ops->map_segment(context, ph->p_vaddr, ph->p_memsz, ph->p_flags) != 0)
			return -1;
	}
	for (i = 0; i < ehdr->e_phnum; i++)
	{
		const struct elf64_phdr *ph = (const struct elf64_phdr *)(
			blob + ehdr->e_phoff + (uint64_t)i * ehdr->e_phentsize);

		if (ph->p_type != ELF64_PT_LOAD)
			continue;
		if (ops->copy_segment(context, ph->p_vaddr, blob + ph->p_offset,
				      ph->p_filesz) != 0 ||
		    (ph->p_memsz > ph->p_filesz &&
		     ops->zero_segment(context, ph->p_vaddr + ph->p_filesz,
				       ph->p_memsz - ph->p_filesz) != 0))
			return -1;
	}
	return 0;
}
