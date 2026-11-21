/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: elf_interp.c
 * Description: execve returns ENOENT when PT_INTERP is not on the filesystem.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#ifdef IR0_SELFTEST_NOLIBC
#include "ir0/nolibc.h"
#else
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#endif

#include <stdint.h>

#include "ir0/ksft.h"

/* Same on-disk fields as fs/elf64_image.c elf64_ehdr and elf64_phdr. */
struct elf64_ehdr
{
	unsigned char e_ident[16];
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

static const char image_path[] = "/tmp/elf_interp";
static const char interp_path[] = "/no/such/ld.so";

static void zero_bytes(void *dst, unsigned int n)
{
	volatile unsigned char *p;
	unsigned int i;

	p = (volatile unsigned char *)dst;
	for (i = 0; i < n; i++)
	{
		p[i] = 0;
	}
}

static void copy_bytes(void *dst, const void *src, unsigned int n)
{
	unsigned char *d;
	const unsigned char *s;
	unsigned int i;

	d = (unsigned char *)dst;
	s = (const unsigned char *)src;
	for (i = 0; i < n; i++)
	{
		d[i] = s[i];
	}
}

/* ET_EXEC, one PT_INTERP, one trivial PT_LOAD. Returns the image length. */
static unsigned int build_image(unsigned char *image, unsigned int cap)
{
	struct elf64_ehdr ehdr;
	struct elf64_phdr iph;
	struct elf64_phdr lph;
	unsigned int phoff;
	unsigned int stroff;
	unsigned int loadoff;
	unsigned int total;

	if (sizeof(ehdr) != 64 || sizeof(iph) != 56)
	{
		return 0;
	}
	phoff = (unsigned int)sizeof(ehdr);
	stroff = phoff + 2U * (unsigned int)sizeof(iph);
	loadoff = stroff + (unsigned int)sizeof(interp_path);
	total = loadoff + 1U;
	if (total > cap)
	{
		return 0;
	}

	zero_bytes(image, cap);
	zero_bytes(&ehdr, (unsigned int)sizeof(ehdr));
	zero_bytes(&iph, (unsigned int)sizeof(iph));
	zero_bytes(&lph, (unsigned int)sizeof(lph));

	ehdr.e_ident[0] = 0x7f;
	ehdr.e_ident[1] = 'E';
	ehdr.e_ident[2] = 'L';
	ehdr.e_ident[3] = 'F';
	ehdr.e_ident[4] = 2;
	ehdr.e_ident[5] = 1;
	ehdr.e_ident[6] = 1;
	ehdr.e_type = 2;
#if defined(__aarch64__)
	/* arch/arm64/sources/elf_policy.c: ELF_EM_AARCH64 */
	ehdr.e_machine = 183;
#else
	/* arch/x86-64/sources/elf_policy.c: ELF_EM_X86_64 */
	ehdr.e_machine = 62;
#endif
	ehdr.e_version = 1;
	ehdr.e_entry = 0x400000ULL;
	ehdr.e_phoff = phoff;
	ehdr.e_ehsize = (uint16_t)sizeof(ehdr);
	ehdr.e_phentsize = (uint16_t)sizeof(iph);
	ehdr.e_phnum = 2;

	iph.p_type = 3;
	iph.p_flags = 4;
	iph.p_offset = stroff;
	iph.p_filesz = (uint64_t)sizeof(interp_path);
	iph.p_memsz = iph.p_filesz;
	iph.p_align = 1;

	lph.p_type = 1;
	lph.p_flags = 5;
	lph.p_offset = loadoff;
	lph.p_vaddr = 0x400000ULL;
	lph.p_paddr = lph.p_vaddr;
	lph.p_filesz = 1;
	lph.p_memsz = 1;
	lph.p_align = 1;

	copy_bytes(image, &ehdr, (unsigned int)sizeof(ehdr));
	copy_bytes(image + phoff, &iph, (unsigned int)sizeof(iph));
	copy_bytes(image + phoff + sizeof(iph), &lph, (unsigned int)sizeof(lph));
	copy_bytes(image + stroff, interp_path, (unsigned int)sizeof(interp_path));
	image[loadoff] = 0x90;
	return total;
}

static int fail(void)
{
	ksft_test_result(0, "elf_interp");
	return ksft_exit();
}

int main(void)
{
	unsigned char image[64 + 56 + 56 + sizeof(interp_path) + 1];
	unsigned int total;
	char *av[2];
	ssize_t n;
	int fd;
	int rc;
	int pass;

	ksft_set_plan(1);
	total = build_image(image, (unsigned int)sizeof(image));
	if (total == 0)
	{
		return fail();
	}

#ifdef IR0_SELFTEST_NOLIBC
	/* arch/arm64/include/uapi/asm/unistd.h: __NR_unlinkat is 35. */
	(void)syscall(35, AT_FDCWD, (long)image_path, 0);
	/*
	 * asm-generic/fcntl.h: O_WRONLY is 1, O_CREAT is 0100, O_TRUNC is 01000.
	 * Mode 0755 keeps an execute bit so execve reaches the interpreter.
	 */
	fd = (int)syscall(SYS_openat, AT_FDCWD, (long)image_path,
			  1 | 0100 | 01000, 0755);
#else
	(void)unlink(image_path);
	fd = open(image_path, O_CREAT | O_TRUNC | O_WRONLY, 0755);
#endif
	if (fd < 0)
	{
		return fail();
	}
	n = write(fd, image, total);
	(void)close(fd);
	if (n != (ssize_t)total)
	{
		return fail();
	}
	fd = open(image_path, 0);
	if (fd < 0)
	{
		return fail();
	}
	(void)close(fd);

	av[0] = (char *)image_path;
	av[1] = 0;
#ifdef IR0_SELFTEST_NOLIBC
	/* arch/arm64/include/uapi/asm/unistd.h: __NR_execve is 221. */
	rc = (int)syscall(221, (long)image_path, (long)av, 0);
	/* Linux errno-base.h: ENOENT is 2. */
	pass = rc < 0 && errno == 2;
#else
	rc = execve(image_path, av, 0);
	pass = rc < 0 && errno == ENOENT;
#endif
	ksft_test_result(pass, "elf_interp");
	return ksft_exit();
}
