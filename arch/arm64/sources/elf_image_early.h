/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef IR0_ARM64_ELF_IMAGE_EARLY_H
#define IR0_ARM64_ELF_IMAGE_EARLY_H

#include <stddef.h>
#include <ir0/elf64_image.h>

int arm64_elf_image_load_early(const void *blob, size_t blob_length,
			       struct ir0_elf64_image *image);
void arm64_elf_zero_early(uint64_t address, uint64_t length);

#endif
