/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: elf_loader.h
 * Description: ELF exec/load public API facade.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <stdint.h>
#include <stddef.h>

struct process;

int load_elf_program(const char *path, struct process *process);
int kexecve(const char *path, char *const argv[], char *const envp[]);
int exec_replace_current(const char *path, char *const argv[], char *const envp[]);

void debug_elf_header(const void *header);
void debug_program_header(const void *phdr, int index);

#define ELF_MAGIC 0x464C457Fu
#define PT_LOAD 1
#define PF_R 4
#define PF_W 2
#define PF_X 1

typedef struct
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
} elf64_header_t;

typedef struct
{
	uint32_t p_type;
	uint32_t p_flags;
	uint64_t p_offset;
	uint64_t p_vaddr;
	uint64_t p_paddr;
	uint64_t p_filesz;
	uint64_t p_memsz;
	uint64_t p_align;
} elf64_phdr_t;
