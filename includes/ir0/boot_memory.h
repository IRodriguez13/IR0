/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — normalized early physical-memory inventory facade.
 *
 * Board/ISA code provides raw reserved-free RAM through arch_mm.h.  Common MM
 * consumes only the normalized inventory exposed here.
 */

#pragma once

#include <ir0/platform_resource.h>
#include <stddef.h>
#include <stdint.h>

#define IR0_BOOT_MEMORY_MAX_BANKS 32U
#define IR0_BOOT_MEMORY_MAX_RESERVATIONS 32U
#define IR0_BOOT_CMDLINE_MAX 256U

/* Pixel layout of a linear framebuffer handed over by the firmware. */
#define IR0_BOOT_FB_TYPE_INDEXED 0U
#define IR0_BOOT_FB_TYPE_RGB     1U
#define IR0_BOOT_FB_TYPE_TEXT    2U

/*
 * Firmware framebuffer in neutral form. Each ISA fills it from its boot
 * protocol (Multiboot on x86-64, a DTB simple-framebuffer on ARM64); video
 * drivers consume only this. width == 0 means no usable framebuffer.
 */
struct ir0_boot_framebuffer
{
	uint64_t base;
	uint64_t size;
	uint32_t width;
	uint32_t height;
	uint32_t pitch;      /* bytes per line */
	uint8_t bpp;
	uint8_t type;        /* IR0_BOOT_FB_TYPE_* */
	/* RGB only: red/green/blue field position and size, in bits. */
	uint8_t red_pos;
	uint8_t red_size;
	uint8_t green_pos;
	uint8_t green_size;
	uint8_t blue_pos;
	uint8_t blue_size;
};

struct ir0_boot_snapshot
{
	struct ir0_phys_range usable[IR0_BOOT_MEMORY_MAX_BANKS];
	size_t usable_count;
	struct ir0_phys_range reserved[IR0_BOOT_MEMORY_MAX_RESERVATIONS];
	size_t reserved_count;
	struct ir0_phys_range firmware;
	struct ir0_phys_range initrd;
	struct ir0_boot_framebuffer framebuffer;
	/*
	 * Physical address of the ACPI RSDP handed over by the boot protocol
	 * (Linux: boot_params.acpi_rsdp_addr); 0 when the platform has none.
	 */
	uint64_t acpi_rsdp;
	char cmdline[IR0_BOOT_CMDLINE_MAX];
	int valid;
	int synthetic_memory;
};

int boot_memory_snapshot_init(uintptr_t firmware_handle);
const struct ir0_boot_snapshot *boot_memory_snapshot(void);
const char *boot_memory_cmdline(void);
int boot_memory_range_is_reserved(uintptr_t base, size_t size);

/* Read, page-normalize, order and validate the platform usable-RAM banks. */
int boot_memory_get_usable_ranges(struct ir0_phys_range *ranges,
				  size_t capacity, size_t *count);

/* Normalize a caller-supplied inventory without consulting an ISA backend. */
int boot_memory_normalize_ranges(const struct ir0_phys_range *input,
				 size_t input_count,
				 struct ir0_phys_range *output,
				 size_t output_capacity, size_t *output_count);

/* Remove page-expanded firmware reservations from normalized usable RAM. */
int boot_memory_exclude_reserved(const struct ir0_phys_range *usable,
				 size_t usable_count,
				 const struct ir0_phys_range *reserved,
				 size_t reserved_count,
				 struct ir0_phys_range *output,
				 size_t output_capacity, size_t *output_count);

/* Read the typed reservations supplied by the active architecture backend. */
int boot_memory_get_reserved_ranges(struct ir0_phys_range *ranges,
				    size_t capacity, size_t *count);

/* Build the final reserved-free inventory consumed by the generic PMM. */
int boot_memory_get_allocatable_ranges(struct ir0_phys_range *ranges,
				       size_t capacity, size_t *count);

/* Initialize the generic PMM from the normalized platform inventory. */
int boot_memory_init_pmm(void);
