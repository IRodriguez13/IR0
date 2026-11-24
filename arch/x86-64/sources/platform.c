/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: platform.c
 * Description: x86-64 CPUID helpers + PC/QEMU platform power ops.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <arch/common/arch_portable.h>
#include <asm/processor.h>
#include <ir0/acpi_pm.h>
#include <ir0/platform_ops.h>
#include <ir0/cpu_info.h>
#include <ir0/errno.h>
#include <ir0/ktm/klog.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <asm/io.h>

#if defined(__MINGW32__) || defined(__MINGW64__) || defined(_WIN32)
#define MINGW_BUILD 1
#else
#define MINGW_BUILD 0
#endif

/**
 * Execute CPUID instruction
 * @leaf: CPUID leaf (eax input)
 * @subleaf: CPUID subleaf (ecx input)
 * @eax: Output for EAX register
 * @ebx: Output for EBX register
 * @ecx: Output for ECX register
 * @edx: Output for EDX register
 */
static inline void cpuid_count(uint32_t leaf, uint32_t subleaf,
                         uint32_t *eax, uint32_t *ebx,
                         uint32_t *ecx, uint32_t *edx)
{
    #if MINGW_BUILD
        __asm__ __volatile__("cpuid"
                            : "=a"(*eax), "=b"(*ebx), "=c"(*ecx), "=d"(*edx)
                            : "a"(leaf), "c"(subleaf)
                            : "memory");
    #else
        asm volatile("cpuid"
                    : "=a"(*eax), "=b"(*ebx), "=c"(*ecx), "=d"(*edx)
                    : "a"(leaf), "c"(subleaf));
    #endif
}

/**
 * Get CPU ID (APIC ID)
 * Returns the current CPU's APIC ID using CPUID
 */
uint32_t get_cpu_id(void)
{
    uint32_t eax, ebx, ecx, edx;
    
    /* Check if CPUID supports leaf 0x1 (basic info) */
    cpuid_count(0, 0, &eax, &ebx, &ecx, &edx);
    if (eax >= 1)
    {
        /* Get CPUID leaf 0x1 - contains APIC ID in bits 31:24 of EBX */
        cpuid_count(1, 0, &eax, &ebx, &ecx, &edx);
        return (ebx >> 24) & 0xFF;
    }
    /* Default to 0 if CPUID not available or not x86 */
    return 0;
}

/**
 * Get number of CPUs
 * For now, detects if HT/SMT is enabled and logical cores
 * Full SMP detection would require ACPI MADT parsing
 */
uint32_t get_cpu_count(void)
{
    uint32_t eax, ebx, ecx, edx;
    
    /* Check if CPUID supports leaf 0x1 */
    cpuid_count(0, 0, &eax, &ebx, &ecx, &edx);
    if (eax >= 1)
    {
        /* Get CPUID leaf 0x1 */
        cpuid_count(1, 0, &eax, &ebx, &ecx, &edx);
        
        /* Check if HT/SMT is supported */
        if (edx & (1 << 28))  /* HTT bit */
        {
            /* Get number of logical processors per package */
            /* Bits 23:16 of EBX contain maximum number of addressable IDs */
            uint32_t max_logical_cores = (ebx >> 16) & 0xFF;
            if (max_logical_cores > 0)
                return max_logical_cores;
        }
    }
    
    /* Check if CPUID supports leaf 0xB (extended topology) */
    cpuid_count(0, 0, &eax, &ebx, &ecx, &edx);
    if (eax >= 0xB)
    {
        /* Try to get logical processor count from leaf 0xB */
        cpuid_count(0xB, 0, &eax, &ebx, &ecx, &edx);
        if (ebx != 0)
        {
            /* EBX contains number of logical processors at this level */
            return ebx & 0xFFFF;
        }
    }
    /* Default to 1 for single-core or non-x86 */
    return 1;
}

/**
 * Get CPU vendor string using CPUID
 * @vendor_buf: Buffer to store vendor string (must be at least 13 bytes)
 * Returns: 0 on success, -1 on failure
 */
int get_cpu_vendor(char *vendor_buf)
{
    if (!vendor_buf)
        return -1;
    
    uint32_t eax, ebx, ecx, edx;
    cpuid_count(0, 0, &eax, &ebx, &ecx, &edx);
    
    /* Vendor string is in EBX, EDX, ECX (in that order) */
    memcpy(vendor_buf, &ebx, 4);
    memcpy(vendor_buf + 4, &edx, 4);
    memcpy(vendor_buf + 8, &ecx, 4);
    vendor_buf[12] = '\0';
    
    return 0;
}

/**
 * Get CPU family, model, and stepping from CPUID
 * @family: Output for CPU family
 * @model: Output for CPU model
 * @stepping: Output for CPU stepping
 * Returns: 0 on success, -1 on failure
 */
int get_cpu_signature(uint32_t *family, uint32_t *model, uint32_t *stepping)
{
    uint32_t eax, ebx, ecx, edx;
    
    /* Check if CPUID supports leaf 0x1 */
    cpuid_count(0, 0, &eax, &ebx, &ecx, &edx);
    if (eax >= 1)
    {
        /* Get CPUID leaf 0x1 - contains family/model/stepping in EAX */
        cpuid_count(1, 0, &eax, &ebx, &ecx, &edx);
        
        if (family)
        {
            /* Extract family: bits 11:8, extended family: bits 27:20 */
            uint32_t base_family = (eax >> 8) & 0xF;
            uint32_t ext_family = (eax >> 20) & 0xFF;
            *family = (ext_family > 0) ? (base_family + ext_family) : base_family;
        }
        
        if (model)
        {
            /* Extract model: bits 7:4, extended model: bits 19:16 */
            uint32_t base_model = (eax >> 4) & 0xF;
            uint32_t ext_model = (eax >> 16) & 0xF;
            *model = (ext_model > 0) ? ((ext_model << 4) + base_model) : base_model;
        }
        
        if (stepping)
        {
            /* Extract stepping: bits 3:0 */
            *stepping = eax & 0xF;
        }
        
        return 0;
    }
    /* Fallback values */
    if (family) *family = 0;
    if (model) *model = 0;
    if (stepping) *stepping = 0;
    return -1;
}

/**
 * Get CPUID maximum leaf (EAX from CPUID.0)
 * @max_leaf: Output for maximum supported leaf
 * Returns: 0 on success, -1 on failure
 */
int get_cpuid_max_leaf(uint32_t *max_leaf)
{
    uint32_t eax, ebx, ecx, edx;
    cpuid_count(0, 0, &eax, &ebx, &ecx, &edx);
    if (max_leaf)
        *max_leaf = eax;
    return 0;
}

/**
 * Get CPU brand string from CPUID (leaves 0x80000002-0x80000004)
 * @buf: Output buffer (at least 49 bytes recommended)
 * @size: Buffer size
 * Returns: 0 on success, -1 if not supported or failure
 */
int get_cpu_brand_string(char *buf, size_t size)
{
    if (!buf || size < 49)
        return -1;
    uint32_t eax, ebx, ecx, edx;
    cpuid_count(0x80000000U, 0, &eax, &ebx, &ecx, &edx);
    if (eax < 0x80000004U)
        return -1;
    uint32_t *u = (uint32_t *)buf;
    cpuid_count(0x80000002U, 0, &u[0], &u[1], &u[2], &u[3]);
    cpuid_count(0x80000003U, 0, &u[4], &u[5], &u[6], &u[7]);
    cpuid_count(0x80000004U, 0, &u[8], &u[9], &u[10], &u[11]);
    buf[48] = '\0';
    /* Trim trailing spaces */
    for (int i = 47; i >= 0 && (buf[i] == ' ' || buf[i] == '\0'); i--)
        buf[i] = '\0';
    return 0;
}

/**
 * Get CPU feature bits from CPUID.1 (EDX and ECX)
 * @out_edx: Output for EDX feature flags (NULL allowed)
 * @out_ecx: Output for ECX feature flags (NULL allowed)
 * Returns: 0 on success, -1 on failure
 */
int get_cpu_feature_bits(uint32_t *out_edx, uint32_t *out_ecx)
{
    uint32_t eax, ebx, ecx, edx;
    cpuid_count(0, 0, &eax, &ebx, &ecx, &edx);
    if (eax < 1)
        return -1;
    cpuid_count(1, 0, &eax, &ebx, &ecx, &edx);
    if (out_edx)
        *out_edx = edx;
    if (out_ecx)
        *out_ecx = ecx;
    return 0;
}

/**
 * Get CLFLUSH line size from CPUID.1 EBX bits 15:8 (units: 8 bytes)
 * Returns: size in bytes, or 0 if not reported
 */
uint32_t get_cpu_clflush_size(void)
{
    uint32_t eax, ebx, ecx, edx;
    cpuid_count(0, 0, &eax, &ebx, &ecx, &edx);
    if (eax < 1)
        return 0;
    cpuid_count(1, 0, &eax, &ebx, &ecx, &edx);
    return ((ebx >> 8) & 0xFF) * 8;
}

int hypervisor_kind(enum ir0_hv_kind *kind)
{
	static int cached;
	static int ran;
	static enum ir0_hv_kind found;
	uint32_t eax;
	uint32_t ebx;
	uint32_t ecx;
	uint32_t edx;
	char vendor[12];

	if (!kind)
		return -1;
	if (cached)
	{
		if (!ran)
			return -1;
		*kind = found;
		return 0;
	}

	cpuid_count(0, 0, &eax, &ebx, &ecx, &edx);
	if (eax < 1)
	{
		ran = 0;
		cached = 1;
		return -1;
	}
	cpuid_count(1, 0, &eax, &ebx, &ecx, &edx);
	if ((ecx & (1u << 31)) == 0)
	{
		found = IR0_HV_NONE;
		ran = 1;
		cached = 1;
		*kind = found;
		return 0;
	}

	/* Leaf 0x40000000 vendor is ebx, ecx, edx — 12 bytes, not a C string. */
	cpuid_count(0x40000000U, 0, &eax, &ebx, &ecx, &edx);
	memcpy(vendor + 0, &ebx, 4);
	memcpy(vendor + 4, &ecx, 4);
	memcpy(vendor + 8, &edx, 4);
	if (memcmp(vendor, "KVMKVMKVM\0\0\0", 12) == 0)
		found = IR0_HV_KVM;
	else if (memcmp(vendor, "XenVMMXenVMM", 12) == 0)
		found = IR0_HV_XEN;
	else
		found = IR0_HV_OTHER;
	ran = 1;
	cached = 1;
	*kind = found;
	return 0;
}

int hypervisor_vendor(char *buf, size_t n)
{
	uint32_t eax, ebx, ecx, edx;
	char vendor[13];

	if (!buf || n < 13)
		return -1;
	if (!hypervisor_present())
	{
		buf[0] = '\0';
		return -1;
	}
	cpuid_count(0x40000000U, 0, &eax, &ebx, &ecx, &edx);
	memcpy(vendor + 0, &ebx, 4);
	memcpy(vendor + 4, &ecx, 4);
	memcpy(vendor + 8, &edx, 4);
	vendor[12] = '\0';
	memcpy(buf, vendor, 13);
	return 0;
}


static void __attribute__((noreturn)) x86_platform_halt_loop(void)
{
	disable_interrupts();
	for (;;)
		cpu_wait();
}

static void x86_platform_halt(void)
{
	/*
	 * QEMU isa-debug-exit (iobase 0xf4): write (status << 1) | 1.
	 * status 0 → guest exit code 1 (QEMU quirk); still terminates cleanly.
	 * Print the success tag before outb — QEMU exits the VM on the write.
	 */
	klog_smoke("ISA_DEBUG_EXIT_OK");
	outb(0xf4, 0x01);
	x86_platform_halt_loop();
}

static void x86_platform_reboot(void)
{
	outb(0x64, 0xFE);
	x86_platform_halt_loop();
}

static void x86_platform_poweroff(void)
{
	/*
	 * isa-debug-exit FIRST. With QEMU -no-shutdown, an ACPI PM1a write
	 * pauses the VM immediately ("QEMU [Paused]") and never reaches a
	 * later outb(0xf4). Exit the process when the device is present;
	 * then try ACPI; then hlt.
	 */
	klog_smoke("ISA_DEBUG_EXIT_OK");
	outb(0xf4, 0x01);
	(void)ir0_acpi_pm_try_poweroff();
	x86_platform_halt_loop();
}

static const struct ir0_platform_ops x86_platform_ops = {
	.halt = x86_platform_halt,
	.reboot = x86_platform_reboot,
	.poweroff = x86_platform_poweroff,
};

static const struct ir0_platform_ops *g_platform_ops = &x86_platform_ops;

const struct ir0_platform_ops *ir0_platform_ops_get(void)
{
	return g_platform_ops;
}

void ir0_platform_ops_set(const struct ir0_platform_ops *ops)
{
	if (ops)
		g_platform_ops = ops;
}


/* CPUID.1 EDX/ECX feature names, in Linux /proc/cpuinfo spelling. */
static const struct
{
	uint8_t bit;
	const char *name;
} cpuinfo_edx_flags[] = {
	{ 0, "fpu" }, { 1, "vme" }, { 2, "de" }, { 3, "pse" }, { 4, "tsc" },
	{ 5, "msr" }, { 6, "pae" }, { 7, "mce" }, { 8, "cx8" }, { 9, "apic" },
	{ 11, "sep" }, { 12, "mtrr" }, { 13, "pge" }, { 14, "mca" },
	{ 15, "cmov" }, { 16, "pat" }, { 17, "pse36" }, { 19, "clflush" },
	{ 23, "mmx" }, { 24, "fxsr" }, { 25, "sse" }, { 26, "sse2" },
	{ 28, "ht" },
}, cpuinfo_ecx_flags[] = {
	{ 0, "pni" }, { 1, "pclmulqdq" }, { 9, "ssse3" }, { 12, "fma" },
	{ 13, "cx16" }, { 19, "sse4_1" }, { 20, "sse4_2" }, { 21, "x2apic" },
	{ 22, "movbe" }, { 23, "popcnt" }, { 25, "aes" }, { 26, "xsave" },
	{ 28, "avx" }, { 29, "f16c" }, { 30, "rdrand" }, { 31, "hypervisor" },
};

struct cpuinfo_out
{
	char *buf;
	size_t count;
	size_t off;
};

static void cpuinfo_puts(struct cpuinfo_out *o, const char *s)
{
	while (*s && o->off + 1 < o->count)
		o->buf[o->off++] = *s++;
	o->buf[o->off] = '\0';
}

static void cpuinfo_line(struct cpuinfo_out *o, const char *key,
			 const char *value)
{
	cpuinfo_puts(o, key);
	cpuinfo_puts(o, "\t: ");
	cpuinfo_puts(o, value);
	cpuinfo_puts(o, "\n");
}

static void cpuinfo_uint(struct cpuinfo_out *o, const char *key, uint32_t v)
{
	char t[16];

	snprintf(t, sizeof(t), "%u", v);
	cpuinfo_line(o, key, t);
}

/* x86 show_cpuinfo(): identification and features from CPUID. */
int show_cpuinfo(unsigned int cpu, char *buf, size_t count)
{
	struct cpuinfo_out o = { buf, count, 0 };
	char vendor[13] = { 0 };
	char model_name[49] = { 0 };
	char t[64];
	uint32_t family = 0, model = 0, stepping = 0, max_leaf = 0;
	uint32_t edx = 0, ecx = 0, eax, ebx, ecx2, edx2;
	size_t i;

	if (!buf || count == 0)
		return -EINVAL;
	buf[0] = '\0';
	if (get_cpu_vendor(vendor) < 0)
		strncpy(vendor, "unknown", sizeof(vendor) - 1);
	get_cpu_signature(&family, &model, &stepping);
	if (get_cpu_brand_string(model_name, sizeof(model_name)) < 0)
		strncpy(model_name, "unknown", sizeof(model_name) - 1);
	get_cpuid_max_leaf(&max_leaf);
	get_cpu_feature_bits(&edx, &ecx);

	cpuinfo_uint(&o, "processor", cpu);
	cpuinfo_line(&o, "vendor_id", vendor);
	cpuinfo_uint(&o, "cpu family", family);
	cpuinfo_uint(&o, "model", model);
	cpuinfo_line(&o, "model name", model_name);
	cpuinfo_uint(&o, "stepping", stepping);
	cpuinfo_uint(&o, "cpuid level", max_leaf);
	cpuinfo_puts(&o, "flags\t:");
	for (i = 0; i < sizeof(cpuinfo_edx_flags) / sizeof(cpuinfo_edx_flags[0]); i++)
	{
		if (edx & (1U << cpuinfo_edx_flags[i].bit))
		{
			cpuinfo_puts(&o, " ");
			cpuinfo_puts(&o, cpuinfo_edx_flags[i].name);
		}
	}
	for (i = 0; i < sizeof(cpuinfo_ecx_flags) / sizeof(cpuinfo_ecx_flags[0]); i++)
	{
		if (ecx & (1U << cpuinfo_ecx_flags[i].bit))
		{
			cpuinfo_puts(&o, " ");
			cpuinfo_puts(&o, cpuinfo_ecx_flags[i].name);
		}
	}
	cpuinfo_puts(&o, "\n");
	cpuinfo_uint(&o, "clflush size", get_cpu_clflush_size() ? get_cpu_clflush_size() : 64);
	/* CPUID 0x80000008: physical and linear address widths. */
	cpuid_count(0x80000000u, 0, &eax, &ebx, &ecx2, &edx2);
	if (eax >= 0x80000008u)
	{
		cpuid_count(0x80000008u, 0, &eax, &ebx, &ecx2, &edx2);
		snprintf(t, sizeof(t), "%u bits physical, %u bits virtual",
			 eax & 0xffu, (eax >> 8) & 0xffu);
		cpuinfo_line(&o, "address sizes", t);
	}
	return (int)o.off;
}
