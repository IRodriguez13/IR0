/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * File: procfs_system.c
 * Description: /proc system-wide entries (meminfo, stat, devices, irq, ...).
 */

#include "procfs_internal.h"


int proc_drivers_read(char *buf, size_t count)
{
    return ir0_driver_list_to_buffer(buf, count);
}

/*
 * /proc/meminfo — labelled kB from PMM frames (PAGE_SIZE = 4 KiB).
 * MemUsed + MemFree == MemTotal (PMM-managed region only).
 */
int proc_meminfo_read(char *buf, size_t count)
{
	size_t total_frames = 0;
	size_t used_frames = 0;
	size_t free_frames = 0;
	size_t heap_total = 0;
	size_t heap_used = 0;
	size_t heap_allocs = 0;
	uint64_t total_kb;
	uint64_t used_kb;
	uint64_t free_kb;
	int len;

	if (VALIDATE_BUFFER(buf, count) != 0)
		return -1;
	memset(buf, 0, count);

	ir0_mm_pmm_stats(&total_frames, &used_frames, &free_frames);
	total_kb = ((uint64_t)total_frames * (uint64_t)IR0_MM_PAGE_SIZE) / BYTES_PER_KB;
	used_kb = ((uint64_t)used_frames * (uint64_t)IR0_MM_PAGE_SIZE) / BYTES_PER_KB;
	free_kb = ((uint64_t)free_frames * (uint64_t)IR0_MM_PAGE_SIZE) / BYTES_PER_KB;

	/*
	 * Report the kernel heap too. The PMM figures barely move because the
	 * heap is carved out of a region reserved at boot, so an exec failing
	 * with -ENOMEM from a fragmented heap looked like a machine with tens
	 * of MiB free. Slab is the Linux field for kernel data structures.
	 */
	ir0_mm_alloc_stats(&heap_total, &heap_used, &heap_allocs);

	len = snprintf(buf, count,
		       "MemTotal:       %llu kB\n"
		       "MemFree:        %llu kB\n"
		       /*
			* Linux field order; BusyBox free reads MemAvailable
			* for its "available" column and printed 0 while the
			* field was missing. IR0 has no reclaimable page
			* cache, so everything free is available and Buffers
			* and Cached are genuinely zero rather than unknown.
			*/
		       "MemAvailable:   %llu kB\n"
		       "Buffers:        0 kB\n"
		       "Cached:         0 kB\n"
		       "Shmem:          0 kB\n"
		       "MemUsed:        %llu kB\n"
		       "PageSize:       %u kB\n"
		       "Slab:           %llu kB\n"
		       "SlabTotal:      %llu kB\n"
		       "SlabAllocs:     %llu\n"
		       "KStackMinFree:  %llu\n"
		       "IrqNestMax:     %u\n"
		       "KStackPeak:     %llu\n",
		       (unsigned long long)total_kb,
		       (unsigned long long)free_kb,
		       (unsigned long long)free_kb,
		       (unsigned long long)used_kb,
		       (unsigned)(IR0_MM_PAGE_SIZE / BYTES_PER_KB),
		       (unsigned long long)((uint64_t)heap_used / BYTES_PER_KB),
		       (unsigned long long)((uint64_t)heap_total / BYTES_PER_KB),
		       (unsigned long long)(uint64_t)heap_allocs,
		       /* Worst kernel stack headroom seen since boot; a #DF on
			* a deep path walk showed 32 KiB is not comfortable. */
		       (unsigned long long)ktm_stack_min_headroom_get(),
		       ktm_irq_nest_max_get(),
		       (unsigned long long)ktm_stack_peak_used());
	if (len < 0)
		return -1;
	if (len >= (int)count)
	{
		buf[count - 1] = '\0';
		return (int)(count - 1);
	}
	buf[len] = '\0';
	return len;
}

/*
 * /proc/uptime — "<seconds_uptime> <seconds_idle>\n" (Unix/BusyBox contract).
 * Values are real seconds from the monotonic clock / idle tick counter.
 */
int proc_uptime_read(char *buf, size_t count)
{
	uint64_t up_ms;
	uint64_t idle_ms;
	uint64_t up_sec;
	uint64_t idle_sec;
	unsigned up_frac;
	unsigned idle_frac;
	int len;

	if (VALIDATE_BUFFER(buf, count) != 0)
		return -1;
	memset(buf, 0, count);

	up_ms = clock_get_uptime_milliseconds();
	idle_ms = clock_get_idle_milliseconds();
	up_sec = up_ms / 1000;
	idle_sec = idle_ms / 1000;
	up_frac = (unsigned)((up_ms % 1000) / 10);     /* hundredths */
	idle_frac = (unsigned)((idle_ms % 1000) / 10);

	len = snprintf(buf, count, "%llu.%02u %llu.%02u\n",
		       (unsigned long long)up_sec, up_frac,
		       (unsigned long long)idle_sec, idle_frac);
	if (len < 0)
		return -1;
	if (len >= (int)count)
	{
		buf[count - 1] = '\0';
		return (int)(count - 1);
	}
	buf[len] = '\0';
	return len;
}

/*
 * /proc/stat — Linux proc(5) / BusyBox top contract.
 *
 * Aggregate "cpu" + single "cpu0" jiffy lines (USER_HZ ≈ CONFIG_TICK_RATE_HZ).
 * Jiffies from clock_get_cpu_accounting(); nice/iowait/irq/softirq/steal are 0
 * until IR0 tracks those buckets (honest zeros, not invented).
 * Source: man7.org/linux/man-pages/man5/proc_stat.5.html
 */
int proc_stat_read(char *buf, size_t count)
{
	uint64_t total;
	uint64_t user;
	uint64_t system;
	uint64_t idle;
	uint64_t nice = 0;
	uint64_t iowait = 0;
	uint64_t irq = 0;
	uint64_t softirq = 0;
	uint64_t steal = 0;
	unsigned runnable = 0;
	unsigned nprocs = 0;
	uint64_t irq_total;
	uint64_t ctxt_total;
	time_t btime;
	int len;

	if (VALIDATE_BUFFER(buf, count) != 0)
		return -1;
	memset(buf, 0, count);

	total = clock_get_tick_count();
	clock_get_cpu_accounting(&user, &system, &idle);
	if (user + system + idle > total)
		total = user + system + idle;

	clock_get_loadavg(NULL, NULL, NULL, &runnable, &nprocs, NULL);
	irq_total = clock_get_irq_count();
	ctxt_total = clock_get_context_switch_count();
	btime = (time_t)clock_get_boot_time();

	/*
	 * Eight fields after the label so FEATURE_TOP_SMP_CPU (BusyBox) accepts
	 * both the aggregate line (>=4) and cpu0 (>4). Duplicate cpu0: one logical CPU.
	 * intr/ctxt/btime: runtime counters (not placeholder zeros when activity exists).
	 */
	len = snprintf(buf, count,
		       "cpu  %llu %llu %llu %llu %llu %llu %llu %llu\n"
		       "cpu0 %llu %llu %llu %llu %llu %llu %llu %llu\n"
		       "intr %llu\n"
		       "ctxt %llu\n"
		       "btime %lld\n"
		       "processes %u\n"
		       "procs_running %u\n"
		       "procs_blocked 0\n",
		       (unsigned long long)user,
		       (unsigned long long)nice,
		       (unsigned long long)system,
		       (unsigned long long)idle,
		       (unsigned long long)iowait,
		       (unsigned long long)irq,
		       (unsigned long long)softirq,
		       (unsigned long long)steal,
		       (unsigned long long)user,
		       (unsigned long long)nice,
		       (unsigned long long)system,
		       (unsigned long long)idle,
		       (unsigned long long)iowait,
		       (unsigned long long)irq,
		       (unsigned long long)softirq,
		       (unsigned long long)steal,
		       (unsigned long long)irq_total,
		       (unsigned long long)ctxt_total,
		       (long long)btime,
		       nprocs,
		       runnable);
	if (len < 0)
		return -1;
	if (len >= (int)count)
	{
		buf[count - 1] = '\0';
		return (int)(count - 1);
	}
	buf[len] = '\0';
	return len;
}

/* /proc/version: raw data only. One line: version\tdate\ttime\tuser\thost\tcompiler */
int proc_version_read(char *buf, size_t count)
{
    char uname_ver[64];

    if (VALIDATE_BUFFER(buf, count) != 0)
        return -1;
    memset(buf, 0, count);
    ir0_utsname_fill_version(uname_ver, sizeof(uname_ver));
    /* Human line aligned with uname(2), including the immutable build id. */
    int len = snprintf(buf, count, "IR0 version %s %s (%s %s by %s@%s with %s)\n",
                       IR0_VERSION_STRING, uname_ver,
                       IR0_BUILD_DATE, IR0_BUILD_TIME,
                       IR0_BUILD_USER, IR0_BUILD_HOST, IR0_BUILD_CC);
    if (len < 0) return -1;
    if (len >= (int)count) { buf[count - 1] = '\0'; return (int)(count - 1); }
    buf[len] = '\0';
    return len;
}

/* /proc/cmdline: portable boot snapshot (Multiboot or DTB bootargs). */
int proc_boot_cmdline_read(char *buf, size_t count)
{
    const char *cmdline;
    size_t n;
    size_t i;

    if (VALIDATE_BUFFER(buf, count) != 0)
        return -1;

    cmdline = boot_memory_cmdline();
    if (!cmdline || cmdline[0] == '\0')
    {
        buf[0] = '\0';
        return 0;
    }

    n = 0;
    while (cmdline[n] && n + 1 < count)
        n++;
    for (i = 0; i < n; i++)
        buf[i] = cmdline[i];
    if (n + 1 < count)
        buf[n++] = '\n';
    buf[n] = '\0';
    return (int)n;
}

/*
 * Build "flags" line from CPUID.1 EDX/ECX (silicon feature bits).
 * Each (bit, name) is appended when the bit is set.
 */
static void proc_cpuinfo_flags_from_cpuid(uint32_t edx, uint32_t ecx, char *out, size_t out_size)
{
    static const struct { uint32_t bit; const char *name; } edx_flags[] = {
        { 0, "fpu" }, { 1, "vme" }, { 2, "de" }, { 3, "pse" }, { 4, "tsc" },
        { 5, "msr" }, { 6, "pae" }, { 7, "mce" }, { 8, "cx8" }, { 9, "apic" },
        { 10, "sep" }, { 11, "mtrr" }, { 12, "pge" }, { 13, "mca" }, { 15, "cmov" },
        { 16, "pat" }, { 17, "pse36" }, { 19, "clflush" }, { 23, "mmx" },
        { 24, "fxsr" }, { 25, "sse" }, { 26, "sse2" }, { 28, "htt" }, { 29, "tm" },
        { 31, "pbe" }
    };
    static const struct { uint32_t bit; const char *name; } ecx_flags[] = {
        { 0, "sse3" }, { 1, "pclmulqdq" }, { 9, "ssse3" }, { 12, "fma" },
        { 13, "cx16" }, { 19, "sse4_1" }, { 20, "sse4_2" }, { 21, "x2apic" },
        { 22, "movbe" }, { 23, "popcnt" }, { 25, "aes" }, { 26, "xsave" },
        { 28, "avx" }, { 29, "f16c" }, { 30, "rdrand" }, { 31, "hypervisor" }
    };
    size_t len = 0;
    out[0] = '\0';
    for (size_t i = 0; i < sizeof(edx_flags)/sizeof(edx_flags[0]) && len < out_size - 8; i++)
    {
        if (edx & (1U << edx_flags[i].bit))
        {
            if (len > 0) { out[len++] = ' '; out[len] = '\0'; }
            len += (size_t)snprintf(out + len, out_size - len, "%s", edx_flags[i].name);
        }
    }
    for (size_t i = 0; i < sizeof(ecx_flags)/sizeof(ecx_flags[0]) && len < out_size - 8; i++)
    {
        if (ecx & (1U << ecx_flags[i].bit))
        {
            if (len > 0) { out[len++] = ' '; out[len] = '\0'; }
            len += (size_t)snprintf(out + len, out_size - len, "%s", ecx_flags[i].name);
        }
    }
}

/* Generate /proc/cpuinfo content - all fields from silicon (CPUID) where available */
int proc_cpuinfo_read(char *buf, size_t count)
{
    if (VALIDATE_BUFFER(buf, count) != 0)
        return -1;

    memset(buf, 0, count);

    uint32_t cpu_id = get_cpu_id();
    uint32_t cpu_count = get_cpu_count();

    char vendor_str[13] = {0};
    if (get_cpu_vendor(vendor_str) < 0)
        strncpy(vendor_str, "Unknown", sizeof(vendor_str) - 1);

    uint32_t family = 0, model = 0, stepping = 0;
    get_cpu_signature(&family, &model, &stepping);

    char model_name[49] = {0};
    if (get_cpu_brand_string(model_name, sizeof(model_name)) < 0)
        strncpy(model_name, get_arch_name() ? get_arch_name() : "Unknown", sizeof(model_name) - 1);

    uint32_t max_leaf = 0;
    get_cpuid_max_leaf(&max_leaf);

    uint32_t feat_edx = 0, feat_ecx = 0;
    get_cpu_feature_bits(&feat_edx, &feat_ecx);

    char flags_buf[512];
    proc_cpuinfo_flags_from_cpuid(feat_edx, feat_ecx, flags_buf, sizeof(flags_buf));

    uint32_t clflush_sz = get_cpu_clflush_size();
    if (clflush_sz == 0)
        clflush_sz = 64;

    uint32_t arch_bits = (uint32_t)(sizeof(uintptr_t) * 8U);

    /* Raw: one line per field, key\tvalue */
    size_t off = 0;
    char t[64];
    int n;
    n = snprintf(buf + off, (off < count) ? (count - off) : 0, "processor\t%u\n", cpu_id); if (n > 0 && (size_t)n < count - off) off += (size_t)n;
    n = snprintf(buf + off, (off < count) ? (count - off) : 0, "vendor_id\t%s\n", vendor_str); if (n > 0 && (size_t)n < count - off) off += (size_t)n;
    n = snprintf(buf + off, (off < count) ? (count - off) : 0, "cpu family\t%u\nmodel\t%u\n", family, model); if (n > 0 && (size_t)n < count - off) off += (size_t)n;
    n = snprintf(buf + off, (off < count) ? (count - off) : 0, "model name\t%s\nstepping\t%u\n", model_name, stepping); if (n > 0 && (size_t)n < count - off) off += (size_t)n;
    n = snprintf(buf + off, (off < count) ? (count - off) : 0, "siblings\t%u\napicid\t%u\ncpuid level\t%u\n", cpu_count, cpu_id, max_leaf); if (n > 0 && (size_t)n < count - off) off += (size_t)n;
    n = snprintf(buf + off, (off < count) ? (count - off) : 0, "flags\t%s\nclflush size\t%u\n", flags_buf, clflush_sz); if (n > 0 && (size_t)n < count - off) off += (size_t)n;
    {
	char hv_vendor[16];

	if (hypervisor_present() &&
	    hypervisor_vendor(hv_vendor, sizeof(hv_vendor)) == 0)
	{
	    n = snprintf(buf + off, (off < count) ? (count - off) : 0,
			 "hypervisor_vendor\t%s\n", hv_vendor);
	    if (n > 0 && (size_t)n < count - off)
		off += (size_t)n;
	}
    }
    snprintf(t, sizeof(t), "%ubits physical, %ubits virtual", arch_bits, arch_bits);
    n = snprintf(buf + off, (off < count) ? (count - off) : 0, "address sizes\t%s\n", t); if (n > 0 && (size_t)n < count - off) off += (size_t)n;
    if (off < count) buf[off] = '\0';
    return (int)off;
}

/*
 * /proc/loadavg — "0.03 0.01 0.00 1/7 123\n"
 * EMA from scheduler samples (see clock_get_loadavg); not raw tick noise.
 */
int proc_loadavg_read(char *buf, size_t count)
{
	uint32_t l1 = 0, l5 = 0, l15 = 0;
	unsigned runnable = 0, nprocs = 0;
	int last_pid = 0;
	int len;

	if (VALIDATE_BUFFER(buf, count) != 0)
		return -1;
	memset(buf, 0, count);

	clock_get_loadavg(&l1, &l5, &l15, &runnable, &nprocs, &last_pid);
	len = snprintf(buf, count, "%u.%02u %u.%02u %u.%02u %u/%u %d\n",
		       l1 / 100, l1 % 100, l5 / 100, l5 % 100, l15 / 100,
		       l15 % 100, runnable, nprocs, last_pid);
	if (len < 0)
		return -1;
	if (len >= (int)count)
	{
		buf[count - 1] = '\0';
		return (int)(count - 1);
	}
	buf[len] = '\0';
	return len;
}

void proc_u64_to_dec(uint64_t value, char *out, size_t out_len)
{
    char rev[24];
    size_t idx = 0;
    size_t pos = 0;

    if (!out || out_len == 0)
        return;

    if (value == 0)
    {
        if (out_len >= 2)
        {
            out[0] = '0';
            out[1] = '\0';
        }
        else
        {
            out[0] = '\0';
        }
        return;
    }

    while (value > 0 && idx < sizeof(rev))
    {
        rev[idx++] = (char)('0' + (value % 10));
        value /= 10;
    }

    while (idx > 0 && pos + 1 < out_len)
    {
        out[pos++] = rev[--idx];
    }
    out[pos] = '\0';
}

/*
 * Format size in sectors (512B) to string in G or M; *len receives length.
 */
static void proc_format_size(uint64_t sectors, char *out, size_t out_size, int *len)
{
    (void)out_size;
    uint64_t bytes = sectors * 512;
    uint64_t mb = bytes / (BYTES_PER_KB * BYTES_PER_KB);
    uint64_t gb = mb / 1024;
    uint64_t val = (gb > 0) ? gb : mb;
    char *p = out;
    if (val == 0)
        *p++ = '0';
    else
    {
        char rev[24];
        int idx = 0;
        while (val > 0) { rev[idx++] = '0' + (val % 10); val /= 10; }
        while (idx > 0) *p++ = rev[--idx];
    }
    *p++ = (gb > 0) ? 'G' : 'M';
    *p = '\0';
    *len = (int)(p - out);
}

/*
 * /proc/blockdevices — tab-separated; sectors are 512-byte units.
 * Header documents the contract; size_human uses MiB/GiB from those sectors.
 * Columns: type name maj min sectors_512 size_human model serial
 */
int proc_blockdevices_read(char *buf, size_t count)
{
    if (VALIDATE_BUFFER(buf, count) != 0)
        return -1;
    memset(buf, 0, count);
    size_t off = 0;
    {
	int nh = snprintf(buf, count,
			  "# type\tname\tmaj\tmin\tsectors_512\tsize\tmodel\tserial\n");
	if (nh < 0)
		return -1;
	if ((size_t)nh >= count)
	{
		buf[count - 1] = '\0';
		return (int)(count - 1);
	}
	off = (size_t)nh;
    }
    for (uint8_t i = 0; i < 4; i++)
    {
        const char *disk_name = ir0_block_legacy_name(i);
        if (!disk_name || !ir0_block_name_is_present(disk_name))
            continue;
        uint64_t size = ir0_block_sector_count_by_name(disk_name);
        const char *model = "-";
        const char *serial = "-";
        char name_buf[8];
        char size_human[16];
        int sh_len;
        proc_format_size(size, size_human, sizeof(size_human), &sh_len);
        snprintf(name_buf, sizeof(name_buf), "hd%c", 'a' + (int)i);
        char sectors_str[24];
        proc_u64_to_dec(size, sectors_str, sizeof(sectors_str));
        int n = snprintf(buf + off, (off < count) ? (count - off) : 0,
                         "disk\t%s\t%u\t%u\t%s\t%s\t%s\t%s\n",
                         name_buf, (unsigned)i, 0u, sectors_str,
                         size_human, model, serial);
        if (n < 0) return -1;
        if (n >= (int)(count - off)) n = (int)(count - off) - 1;
        off += (size_t)n;
        int part_count = get_partition_count(i);
        for (int part_idx = 0; part_idx < part_count && off < count; part_idx++)
        {
            partition_info_t part_info;
            if (partition_nth_on_disk(i, (unsigned)part_idx, &part_info) != 0)
                continue;
            char part_name[12];
            char part_size_human[16];
            int psh_len;
            proc_format_size(part_info.total_sectors, part_size_human, sizeof(part_size_human), &psh_len);
            snprintf(part_name, sizeof(part_name), "hd%c%d", 'a' + (int)i,
                     (int)part_info.partition_number + 1);
            char part_sectors_str[24];
            proc_u64_to_dec(part_info.total_sectors, part_sectors_str, sizeof(part_sectors_str));
            n = snprintf(buf + off, (off < count) ? (count - off) : 0,
                         "part\t%s\t%u\t%u\t%s\t%s\t-\t-\n",
                         part_name, (unsigned)i, (unsigned)(part_info.partition_number + 1),
                         part_sectors_str, part_size_human);
            if (n < 0) break;
            if (n >= (int)(count - off)) n = (int)(count - off) - 1;
            off += (size_t)n;
        }
    }
    if (off < count) buf[off] = '\0';
    return (int)off;
}

/* /proc/filesystems: raw data only. One line per fs: type\tname (type=nodev or empty) */
int proc_filesystems_read(char *buf, size_t count)
{
    if (VALIDATE_BUFFER(buf, count) != 0)
        return -1;
    memset(buf, 0, count);
    size_t off = 0;
    int n = snprintf(buf + off, (off < count) ? (count - off) : 0, "nodev\tproc\n");
    if (n > 0 && (size_t)n < count - off) off += (size_t)n;
    n = snprintf(buf + off, (off < count) ? (count - off) : 0, "nodev\tdevfs\n");
    if (n > 0 && (size_t)n < count - off) off += (size_t)n;
#if CONFIG_ENABLE_FS_TMPFS
    n = snprintf(buf + off, (off < count) ? (count - off) : 0, "nodev\ttmpfs\n");
    if (n > 0 && (size_t)n < count - off) off += (size_t)n;
    n = snprintf(buf + off, (off < count) ? (count - off) : 0, "nodev\tramfs\n");
    if (n > 0 && (size_t)n < count - off) off += (size_t)n;
#endif
#if CONFIG_ENABLE_FS_MINIX
    n = snprintf(buf + off, (off < count) ? (count - off) : 0, "\tminix\n");
    if (n > 0 && (size_t)n < count - off) off += (size_t)n;
#endif
#if CONFIG_ENABLE_FS_SIMPLEFS
    n = snprintf(buf + off, (off < count) ? (count - off) : 0, "\tsimplefs\n");
    if (n > 0 && (size_t)n < count - off) off += (size_t)n;
#endif
#if CONFIG_ENABLE_FS_FAT16
    n = snprintf(buf + off, (off < count) ? (count - off) : 0, "\tfat16\n");
    if (n > 0 && (size_t)n < count - off) off += (size_t)n;
#endif
    if (off < count) buf[off] = '\0';
    return (int)off;
}

/* /proc/partitions: raw data only. One line per device: major\tminor\tblocks_1k\tname */
int proc_partitions_read(char *buf, size_t count)
{
    if (VALIDATE_BUFFER(buf, count) != 0)
        return -1;
    memset(buf, 0, count);
    size_t off = 0;
    for (uint8_t disk_id = 0; disk_id < MAX_DISKS; disk_id++)
    {
        const char *disk_name = ir0_block_legacy_name(disk_id);
        if (!disk_name || !ir0_block_name_is_present(disk_name))
            continue;
        uint64_t disk_blocks_1k = ir0_block_sector_count_by_name(disk_name) / 2;
        char name_buf[16];
        snprintf(name_buf, sizeof(name_buf), "hd%c", 'a' + disk_id);
        char disk_blocks_str[24];
        proc_u64_to_dec(disk_blocks_1k, disk_blocks_str, sizeof(disk_blocks_str));
        int n = snprintf(buf + off, (off < count) ? (count - off) : 0,
                         "%u\t%u\t%s\t%s\n",
                         (unsigned)disk_id, 0u, disk_blocks_str, name_buf);
        if (n < 0) break;
        if (n >= (int)(count - off)) n = (int)(count - off) - 1;
        off += (size_t)n;
        int part_count = get_partition_count(disk_id);
        for (int part_idx = 0; part_idx < part_count; part_idx++)
        {
            partition_info_t part_info;
            if (partition_nth_on_disk(disk_id, (unsigned)part_idx, &part_info) != 0)
                continue;
            uint64_t part_blocks_1k = part_info.total_sectors / 2;
            snprintf(name_buf, sizeof(name_buf), "hd%c%d", 'a' + disk_id,
                     (int)part_info.partition_number + 1);
            char part_blocks_str[24];
            proc_u64_to_dec(part_blocks_1k, part_blocks_str, sizeof(part_blocks_str));
            n = snprintf(buf + off, (off < count) ? (count - off) : 0,
                         "%u\t%u\t%s\t%s\n",
                         (unsigned)disk_id, (unsigned)(part_info.partition_number + 1),
                         part_blocks_str, name_buf);
            if (n < 0) break;
            if (n >= (int)(count - off)) n = (int)(count - off) - 1;
            off += (size_t)n;
        }
    }
    if (off < count) buf[off] = '\0';
    return (int)off;
}

/* /proc/mounts: one line per VFS mount — device path fstype ro|rw 0 0 */
int proc_mounts_read(char *buf, size_t count)
{
    if (VALIDATE_BUFFER(buf, count) != 0)
        return -1;
    memset(buf, 0, count);
    size_t off = 0;

    for (struct vfs_mount *m = vfs_get_mounts(); m && off < count; m = m->next) {
        const char *dev = (m->dev[0] != '\0') ? m->dev : "none";
        const char *fst = (m->fs && m->fs->name) ? m->fs->name : "unknown";
        const char *opts = (m->flags & IR0_MS_RDONLY) ? "ro" : "rw";
        int n = snprintf(buf + off, (off < count) ? (count - off) : 0,
                         "%s %s %s %s 0 0\n",
                         dev, m->path, fst, opts);
        if (n <= 0 || (size_t)n >= count - off)
            break;
        off += (size_t)n;
    }
    /*
     * Pseudo-filesystems are served through the registry, not through the VFS
     * mount list, so they never appeared here and mount(1) showed only the
     * root. Linux lists proc, sysfs and devtmpfs; /heart is IR0-specific but
     * is a namespace the session can use, so it is listed too. vfs_statfs()
     * reports them with zero blocks, so df skips them without -a.
     */
    {
        static const char *const pseudo[] = {
            "proc /proc proc rw 0 0\n",
            "sysfs /sys sysfs rw 0 0\n",
            "devtmpfs /dev devtmpfs rw 0 0\n",
            "heartfs /heart heartfs rw 0 0\n",
        };
        size_t i;

        for (i = 0; i < sizeof(pseudo) / sizeof(pseudo[0]) && off < count; i++) {
            size_t len = strlen(pseudo[i]);

            if (len >= count - off)
                break;
            memcpy(buf + off, pseudo[i], len);
            off += len;
        }
    }

    if (off < count)
        buf[off] = '\0';
    return (int)off;
}

/*
 * Generate /proc/interrupts content from resource registry only.
 * Data comes from drivers that registered their IRQ (silicon/hardware).
 */
struct irq_collect_ctx {
    char *buf;
    size_t count;
    size_t off;
    uint8_t irqs[16];
    const char *names[16];
    int n;
};

static int irq_collect_cb(uint8_t irq, const char *name, void *ctx)
{
    struct irq_collect_ctx *c = (struct irq_collect_ctx *)ctx;
    if (c->n < 16)
    {
        c->irqs[c->n] = irq;
        c->names[c->n] = name;
        c->n++;
    }
    return 0;
}

/* /proc/interrupts: raw data only. One line per IRQ: irq\tname */
int proc_interrupts_read(char *buf, size_t count)
{
    if (VALIDATE_BUFFER(buf, count) != 0)
        return -1;
    memset(buf, 0, count);
    struct irq_collect_ctx ctx = { .buf = buf, .count = count, .off = 0, .n = 0 };
    resource_foreach_irq(irq_collect_cb, &ctx);
    size_t off = 0;
    uint8_t order[16];
    for (int i = 0; i < ctx.n; i++) order[i] = (uint8_t)i;
    for (int i = 0; i < ctx.n - 1; i++)
        for (int j = i + 1; j < ctx.n; j++)
            if (ctx.irqs[order[i]] > ctx.irqs[order[j]])
                { uint8_t t = order[i]; order[i] = order[j]; order[j] = t; }
    for (int i = 0; i < ctx.n && off < count; i++)
    {
        int idx = order[i];
        int n = snprintf(buf + off, (off < count) ? (count - off) : 0,
                         "%u\t%s\n", (unsigned)ctx.irqs[idx], ctx.names[idx]);
        if (n <= 0 || n >= (int)(count - off)) break;
        off += (size_t)n;
    }
    if (off < count) buf[off] = '\0';
    return (int)off;
}

/*
 * Generate /proc/iomem content (physical memory map).
 * System RAM from PMM; MMIO ranges from resource registry (driver-registered).
 */
struct iomem_ctx {
    char *buf;
    size_t count;
    size_t off;
};

static int iomem_mmio_cb(uint64_t start, uint64_t end, const char *name, void *ctx)
{
	struct iomem_ctx *c = (struct iomem_ctx *)ctx;
	int n;

	/* Registry stores inclusive end (see resource_register_mmio callers). */
	n = snprintf(c->buf + c->off, (c->off < c->count) ? (c->count - c->off) : 0,
		     "%016llx-%016llx : %s\n",
		     (unsigned long long)start, (unsigned long long)end,
		     name ? name : "MMIO");
	if (n > 0 && (size_t)n < c->count - c->off)
	{
		c->off += (size_t)n;
		return 0;
	}
	return 1;
}

/*
 * /proc/iomem — inclusive hex ranges from the real PMM window + MMIO registry.
 * "System RAM" is only the region pmm_init manages (not a hardcoded 16 MiB).
 */
int proc_iomem_read(char *buf, size_t count)
{
	size_t off = 0;
	uintptr_t ram_start;
	uintptr_t ram_end_excl;
	uint64_t ram_end_incl;
	int n;

	if (VALIDATE_BUFFER(buf, count) != 0)
		return -1;
	memset(buf, 0, count);

	ram_start = ir0_mm_pmm_start();
	ram_end_excl = ir0_mm_pmm_end();
	if (ram_end_excl > ram_start)
	{
		ram_end_incl = (uint64_t)ram_end_excl - 1ULL;
		n = snprintf(buf, count,
			     "%016llx-%016llx : System RAM (PMM-managed)\n",
			     (unsigned long long)ram_start,
			     (unsigned long long)ram_end_incl);
		if (n < 0)
			return -1;
		if ((size_t)n >= count)
		{
			buf[count - 1] = '\0';
			return (int)(count - 1);
		}
		off = (size_t)n;
	}

	{
		struct iomem_ctx ctx = { .buf = buf, .count = count, .off = off };

		resource_foreach_mmio(iomem_mmio_cb, &ctx);
		off = ctx.off;
	}

	if (off < count)
		buf[off] = '\0';
	return (int)off;
}

/* /proc/kmsg: kernel log ring buffer (read-only, no serial side effects). */
int proc_kmsg_read(char *buf, size_t count)
{
    int n;

    if (VALIDATE_BUFFER(buf, count) != 0)
        return -1;
    memset(buf, 0, count);
    n = klog_read_records(buf, count);
    if (n < 0)
        return -1;
    if (n >= (int)count)
    {
        buf[count - 1] = '\0';
        return (int)(count - 1);
    }
    return n;
}

/* /proc/swaps: Linux-style header; empty table when no swap devices. */
int proc_swaps_read(char *buf, size_t count)
{
    return ir0_swap_format_proc(buf, count);
}

/*
 * Generate /proc/ioports content from resource registry only.
 * Ranges and names come from drivers that registered their ports (silicon/hardware).
 */
struct ioport_ctx {
    char *buf;
    size_t count;
    size_t off;
};

/* Raw: start\tend\tname per line */
static int ioport_cb(uint16_t start, uint16_t end, const char *name, void *ctx)
{
    struct ioport_ctx *c = (struct ioport_ctx *)ctx;
    int n = snprintf(c->buf + c->off, (c->off < c->count) ? (c->count - c->off) : 0,
                     "%u\t%u\t%s\n", (unsigned)start, (unsigned)end, name);
    if (n > 0 && (size_t)n < c->count - c->off) { c->off += (size_t)n; return 0; }
    return 1;
}

int proc_ioports_read(char *buf, size_t count)
{
    if (VALIDATE_BUFFER(buf, count) != 0)
        return -1;
    memset(buf, 0, count);
    struct ioport_ctx ctx = { .buf = buf, .count = count, .off = 0 };
    resource_foreach_ioport(ioport_cb, &ctx);
    if (ctx.off < count)
        buf[ctx.off] = '\0';
    return (int)ctx.off;
}

/* /proc/modules: raw data only. Same as drivers: name\tversion\tlang\tstate\tdescription */
int proc_modules_read(char *buf, size_t count)
{
    return ir0_driver_list_to_buffer(buf, count);
}

/* /proc/timer_list: raw data only. One line: timer\tname\tfrequency\ttick_count\tuptime_sec\tuptime_ms */
int proc_timer_list_read(char *buf, size_t count)
{
    if (VALIDATE_BUFFER(buf, count) != 0)
        return -1;
    memset(buf, 0, count);
    clock_stats_t stats;
    if (clock_get_stats(&stats) != 0)
        return 0;
    const char *timer_name = "Unknown";
    switch (stats.active_timer) {
        case CLOCK_TIMER_NONE: timer_name = "None"; break;
        case CLOCK_TIMER_PIT:  timer_name = "PIT"; break;
        case CLOCK_TIMER_HPET:  timer_name = "HPET"; break;
        case CLOCK_TIMER_LAPIC: timer_name = "LAPIC"; break;
        case CLOCK_TIMER_RTC:  timer_name = "RTC"; break;
    }
    char tick_count_str[24];
    char uptime_sec_str[24];
    proc_u64_to_dec(stats.tick_count, tick_count_str, sizeof(tick_count_str));
    proc_u64_to_dec(stats.uptime_seconds, uptime_sec_str, sizeof(uptime_sec_str));
    int n = snprintf(buf, count, "%s\t%u\t%s\t%s\t%u\n",
                     timer_name, stats.timer_frequency,
                     tick_count_str,
                     uptime_sec_str,
                     stats.uptime_milliseconds);
    if (n < 0) return -1;
    if (n >= (int)count) { buf[count - 1] = '\0'; return (int)(count - 1); }
    return n;
}
