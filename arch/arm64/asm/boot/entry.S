/* SPDX-License-Identifier: GPL-3.0-only */
/* ARM64 early boot mechanics; C owns the portable boot policy. */
/*
 * ARM64 firmware entry. Preserve the Linux arm64 boot-protocol x0 DTB pointer,
 * normalize an EL2 firmware entry to EL1h, turn the MMU on with the kernel
 * image in TTBR1, then establish the C stack. No compiler-generated prologue
 * is allowed before x0 has been saved.
 */
#include <asm/boot.h>

/* MAIR_EL1 slots, as arch/arm64/sources/mmu.c encodes them. */
#define MAIR_INIT	0x44ff00	/* 0 Device-nGnRnE, 1 Normal WB, 2 Normal NC */
/*
 * TCR_EL1: 48-bit VA in both halves (T0SZ = T1SZ = 16), 4 KiB granules,
 * write-back inner-shareable walks, 40-bit IPA.
 */
#define TCR_INIT	0x2b5103510
/* EL1 RWX Normal block, never executable at EL0 (UXN, AF, SH inner). */
#define PMD_KERNEL	0x0040000000000705
/* Device-nGnRnE block, never executable (PXN, UXN, AF). */
#define PMD_DEVICE	0x0060000000000401
#define PTE_TABLE	0x3
#define SCTLR_MMU_ON	0x1005	/* M, C, I */
#define SCTLR_MMU_CLR	0x2080000	/* EE, WXN */


	.section .text.boot, "ax"
	.align  3
	.global _start
	.type   _start, %function
_start:
	/* Linux arm64 Image header: executable branch plus 4 KiB LE contract. */
	b	.Lprimary_entry
	.long	0
	.quad	0x80000
	/* QEMU ignores text_offset unless image_size is non-zero. */
	.quad	arm64_image_size
	.quad	0x2
	.quad	0
	.quad	0
	.quad	0
	.long	0x644d5241
	.long	0

.Lprimary_entry:
	/* x21: firmware DTB, kept in a register until the MMU is on. */
	mov	x21, x0

	/* The common kernel always starts in EL1, independently of firmware. */
	mrs	x2, CurrentEL
	cmp	x2, #0x4
	b.eq	.Lel1_entry
	cmp	x2, #0x8
	b.ne	.Lunsupported_el

	/* EL1 executes AArch64 and receives the physical timer directly. */
	mov	x3, #0x80000000
	msr	hcr_el2, x3
	mov	x3, #3
	msr	cnthctl_el2, x3
	msr	cntvoff_el2, xzr

	/* Do not inherit firmware traps or stage-2 translation state. */
	msr	cptr_el2, xzr
	msr	hstr_el2, xzr
	msr	vttbr_el2, xzr

	adr	x3, .Lel1_entry
	msr	elr_el2, x3
	mov	x3, #0x3c5		/* DAIF masked, EL1h. */
	msr	spsr_el2, x3
	isb
	eret

.Lel1_entry:
	/*
	 * Use SP_EL1 for the boot stack so EL1h matches arm64_cpu_switch and
	 * vectors.S (current EL with SPx). firmware may leave SPSel undefined.
	 */
	msr	spsel, #1
	isb
	msr	daifset, #0xf

	/* x19: physical _start; x20: the 2 MiB aligned image base. */
	adr	x19, _start
	sub	x20, x19, #KIMAGE_TEXT_OFFSET

	/*
	 * The tables are written with the MMU and D-cache off. Invalidate
	 * them before (no dirty line may later overwrite them) and after (no
	 * stale line may hide them from the cacheable table walk).
	 */
	bl	.Linval_init_pg
	adrp	x0, init_pg_start
	adrp	x1, init_pg_end
1:	stp	xzr, xzr, [x0], #16
	cmp	x0, x1
	b.lo	1b

	/*
	 * TTBR1: swapper_pg_dir[256] -> L1[0] -> L2, KIMAGE_MAP_SIZE of 2 MiB
	 * Normal blocks at KIMAGE_VADDR over the physical image base.
	 */
	adrp	x0, swapper_pg_dir
	adrp	x1, kimage_pg_l1
	orr	x2, x1, #PTE_TABLE
	str	x2, [x0, #(256 * 8)]
	adrp	x0, kimage_pg_l2
	orr	x2, x0, #PTE_TABLE
	str	x2, [x1]
	ldr	x3, =PMD_KERNEL
	orr	x2, x20, x3
	mov	x4, #(KIMAGE_MAP_SIZE / KIMAGE_BLOCK_SIZE)
2:	str	x2, [x0], #8
	add	x2, x2, #KIMAGE_BLOCK_SIZE
	subs	x4, x4, #1
	b.ne	2b

	/*
	 * TTBR0: identity map of the low 512 GiB as Device-nGnRnE 1 GiB
	 * blocks, which is how data accesses behaved with the MMU off, so the
	 * DTB and the console UART stay reachable at their physical
	 * addresses. The 2 MiB block that holds this code is Normal and
	 * executable so the instructions after the MMU is enabled can run.
	 */
	adrp	x0, idmap_pg_l0
	adrp	x1, idmap_pg_l1
	orr	x2, x1, #PTE_TABLE
	str	x2, [x0]
	ldr	x2, =PMD_DEVICE
	mov	x3, x1
	mov	x4, #512
	mov	x7, #0x40000000
3:	str	x2, [x3], #8
	add	x2, x2, x7
	subs	x4, x4, #1
	b.ne	3b
	adrp	x5, idmap_pg_l2
	orr	x2, x5, #PTE_TABLE
	ubfx	x6, x20, #30, #9
	str	x2, [x1, x6, lsl #3]
	lsr	x2, x20, #30
	lsl	x2, x2, #30
	ldr	x3, =PMD_DEVICE
	orr	x2, x2, x3
	mov	x3, x5
	mov	x4, #512
4:	str	x2, [x3], #8
	add	x2, x2, #KIMAGE_BLOCK_SIZE
	subs	x4, x4, #1
	b.ne	4b
	ubfx	x6, x20, #21, #9
	ldr	x3, =PMD_KERNEL
	orr	x2, x20, x3
	str	x2, [x5, x6, lsl #3]

	dsb	sy
	bl	.Linval_init_pg

	ldr	x0, =MAIR_INIT
	msr	mair_el1, x0
	ldr	x0, =TCR_INIT
	msr	tcr_el1, x0
	adrp	x0, idmap_pg_l0
	msr	ttbr0_el1, x0
	adrp	x0, swapper_pg_dir
	msr	ttbr1_el1, x0
	isb
	tlbi	vmalle1
	dsb	nsh
	isb
	mrs	x0, sctlr_el1
	ldr	x1, =SCTLR_MMU_ON
	orr	x0, x0, x1
	ldr	x1, =SCTLR_MMU_CLR
	bic	x0, x0, x1
	msr	sctlr_el1, x0
	isb
	ic	iallu
	dsb	nsh
	isb
	ldr	x0, =.Lkimage_entry
	br	x0

.Lkimage_entry:
	/* Running at KIMAGE_VADDR now: adrp yields kernel-half addresses. */
	ldr	x0, =_start
	sub	x0, x0, x19
	adrp	x1, kimage_voffset
	str	x0, [x1, :lo12:kimage_voffset]
	adrp	x1, arm64_firmware_fdt
	str	x21, [x1, :lo12:arm64_firmware_fdt]

	adrp	x1, boot_stack
	add	x1, x1, :lo12:boot_stack
	add	sp, x1, #(ARM64_BOOT_STACK_SIZE >> 12), lsl #12
	mov	x0, x21
	bl	kmain

.Lunsupported_el:
	wfe
	b	.Lunsupported_el

/* dc ivac over [init_pg_start, init_pg_end); clobbers x0-x3. No stack. */
.Linval_init_pg:
	mrs	x3, ctr_el0
	ubfx	x3, x3, #16, #4
	mov	x2, #4
	lsl	x2, x2, x3
	adrp	x0, init_pg_start
	adrp	x1, init_pg_end
5:	dc	ivac, x0
	add	x0, x0, x2
	cmp	x0, x1
	b.lo	5b
	dsb	sy
	ret
	.ltorg
	.size	_start, .-_start

/*
 * Boot translation tables. swapper_pg_dir stays the TTBR1 root for the
 * kernel's lifetime (mmu.c adds the linear map to it); the identity tables
 * only carry the boot until arm64_mmu_enable() installs its own TTBR0.
 */
	.section .bss..init_pg, "aw", %nobits
	.balign	4096
init_pg_start:
	.global	swapper_pg_dir
swapper_pg_dir:
	.space	4096
kimage_pg_l1:
	.space	4096
kimage_pg_l2:
	.space	4096
idmap_pg_l0:
	.space	4096
idmap_pg_l1:
	.space	4096
idmap_pg_l2:
	.space	4096
init_pg_end:
