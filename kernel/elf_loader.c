// SPDX-License-Identifier: GPL-3.0-only
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: elf_loader.c
 * Description: ELF binary loader for user programs with segment loading and process creation
 */

#include "process.h"
#include "scheduler_api.h"
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <ir0/kmem.h>
#include <fs/vfs.h>
#include <ir0/serial_io.h>
#include <kernel/process.h>
#include <mm/paging.h>
#include <mm/pmm.h>
#include <ir0/copy_user.h>
#include <ir0/debug_trap.h>
#include <ir0/oops.h>
#include <ir0/arch_port.h>
#include <config.h>
#include <errno.h>

/* Compiler optimization hints */
#define likely(x) __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)

/* Use external strstr from string.h */

/* Minimal ELF structures */
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

/* ELF constants */
#define ELF_MAGIC_0 0x7f
#define ELF_MAGIC_1 'E'
#define ELF_MAGIC_2 'L'
#define ELF_MAGIC_3 'F'
#define ELFCLASS64 2
#define ET_EXEC 2
#define ET_DYN  3
#define PT_LOAD 1
#define EM_X86_64 0x3e

/* Maximum program headers processed per executable (bounds cost and table size) */
#define ELF_MAX_PHNUM 64

/* Cap argv/envp enumeration so hostile or buggy user stacks cannot unbounded-scan */
#define ELF_ARG_MAX 256

#define AT_NULL   0
#define AT_PHDR   3
#define AT_PHENT  4
#define AT_PHNUM  5
#define AT_BASE   7
#define AT_PAGESZ 6
#define AT_ENTRY  9
#define AT_UID    11
#define AT_EUID   12
#define AT_GID    13
#define AT_EGID   14
#define AT_CLKTCK 17
#define AT_SECURE 23
#define AT_FLAGS  24
#define AT_RANDOM 25
#define PT_PHDR   6

#define ELF_AUXV_PAIRS 15
#define ELF_AT_RANDOM_BYTES 16
#define AT_CLKTCK_VALUE 100

/*
 * Read user-space bytes from @pml4 without switching CR3.
 * Returns 0 on success, -1 if any page is unmapped.
 */
static int elf_read_user_region_in_directory(uint64_t *pml4, uintptr_t src,
                                             void *dst, size_t n)
{
    uint8_t *d = (uint8_t *)dst;

    if (!pml4 || !dst)
        return -1;

    while (n > 0)
    {
        uintptr_t page = src & ~0xFFFULL;
        size_t off = (size_t)(src & 0xFFFULL);
        size_t chunk = PAGE_SIZE_4KB - off;
        uint64_t *pte;
        uintptr_t phys;

        if (chunk > n)
            chunk = n;

        pte = paging_get_pte(pml4, page);
        if (!pte || !(*pte & PAGE_PRESENT))
            return -1;

        phys = (uintptr_t)(*pte & PAGE_PTE_PFN_MASK);
        memcpy(d, (const void *)(phys + off), chunk);

        src += chunk;
        d += chunk;
        n -= chunk;
    }

    return 0;
}

static int elf_read_user_u64(uint64_t *pml4, uintptr_t src, uint64_t *out)
{
    return elf_read_user_region_in_directory(pml4, src, out, sizeof(*out));
}

static int elf_read_user_cstr_in_directory(uint64_t *pml4, uintptr_t src,
                                           char *dst, size_t dst_size)
{
    size_t i;
    uint8_t c;

    if (!dst || dst_size == 0)
        return -1;

    for (i = 0; i < dst_size - 1; i++)
    {
        if (elf_read_user_region_in_directory(pml4, src + i, &c, 1) != 0)
            return -1;
        dst[i] = (char)c;
        if (c == '\0')
            return 0;
    }

    dst[dst_size - 1] = '\0';
    return 0;
}

static void elf_trace_argv_contract(process_t *proc, const char *image_path,
                                    const char *stage)
{
#if !CONFIG_DEBUG_FASE50
    (void)proc;
    (void)image_path;
    (void)stage;
    return;
#else
    uint64_t argc = 0;
    uint64_t argv0_ptr = 0;
    char argv0_buf[128];
    int argv0_read_ok = 0;
    int broken = 0;

    if (!proc || !proc->page_directory)
        return;

    if (elf_read_user_u64(proc->page_directory, (uintptr_t)proc->task.rsp, &argc) != 0)
        return;
    if (elf_read_user_u64(proc->page_directory, (uintptr_t)(proc->task.rsp + sizeof(uint64_t)),
                          &argv0_ptr) != 0)
        return;

    argv0_buf[0] = '\0';
    if (argv0_ptr != 0 &&
        elf_read_user_cstr_in_directory(proc->page_directory, (uintptr_t)argv0_ptr,
                                        argv0_buf, sizeof(argv0_buf)) == 0)
    {
        argv0_read_ok = 1;
    }

    serial_print("[FASE50][EXEC_ARGV] stage=");
    serial_print(stage ? stage : "(null)");
    serial_print(" pid=");
    serial_print_hex32((uint32_t)proc->task.pid);
    serial_print(" path=");
    serial_print(image_path ? image_path : "(null)");
    serial_print(" argc_stack=");
    serial_print_hex64(argc);
    serial_print(" argv0_ptr=");
    serial_print_hex64(argv0_ptr);
    serial_print(" argv0=");
    serial_print(argv0_read_ok ? argv0_buf : "(unreadable)");
    serial_print("\n");

    if (image_path && strstr(image_path, "/bin/busybox") != NULL &&
        (argc == 0 || argv0_ptr == 0))
    {
        broken = 1;
    }

    if (broken)
    {
        serial_print("[FASE50][CLASSIFY] EXEC_ARGV_CONTRACT_BROKEN pid=");
        serial_print_hex32((uint32_t)proc->task.pid);
        serial_print(" path=");
        serial_print(image_path ? image_path : "(null)");
        serial_print(" argc_stack=");
        serial_print_hex64(argc);
        serial_print(" argv0_ptr=");
        serial_print_hex64(argv0_ptr);
        serial_print("\n");
    }
#endif
}

static void elf_trace_entry_stack_layout(process_t *proc, const elf64_header_t *header,
                                         uint64_t at_phdr, uint64_t at_base,
                                         const char *stage)
{
#if !CONFIG_DEBUG_FASE50
    (void)proc;
    (void)header;
    (void)at_phdr;
    (void)at_base;
    (void)stage;
    return;
#else
    uint8_t dump[256];
    uint64_t rsp;
    uint64_t argc = 0;
    uint64_t argv_base = 0;
    uint64_t envp_base = 0;
    uint64_t cursor = 0;
    int i;

    if (!proc || !proc->page_directory)
        return;

    rsp = proc->task.rsp;
    serial_print("[FASE50][EXEC_STACK] stage=");
    serial_print(stage ? stage : "(null)");
    serial_print(" pid=");
    serial_print_hex32((uint32_t)proc->task.pid);
    serial_print(" entry_elf=");
    serial_print_hex64(header ? header->e_entry : 0);
    serial_print(" frame_rip=");
    serial_print_hex64(proc->syscall_frame.rip);
    serial_print(" task_rip=");
    serial_print_hex64(proc->task.rip);
    serial_print(" rsp=");
    serial_print_hex64(rsp);
    serial_print(" rsp_aligned16=");
    serial_print((rsp & 0xFULL) == 0 ? "1" : "0");
    serial_print(" at_phdr=");
    serial_print_hex64(at_phdr);
    serial_print(" at_base=");
    serial_print_hex64(at_base);
    serial_print("\n");

    if (elf_read_user_region_in_directory(proc->page_directory, (uintptr_t)rsp,
                                          dump, sizeof(dump)) != 0)
    {
        serial_print("[FASE50][EXEC_STACK] stage=");
        serial_print(stage ? stage : "(null)");
        serial_print(" dump=unmapped\n");
        return;
    }

    for (i = 0; i < (int)sizeof(dump); i += 32)
    {
        serial_print("[FASE50][EXEC_STACK_DUMP] +");
        serial_print_hex32((uint32_t)i);
        serial_print(" ");
        for (int j = 0; j < 32; j++)
        {
            uint8_t b = dump[i + j];
            static const char hex[] = "0123456789ABCDEF";
            char out[3];
            out[0] = hex[(b >> 4) & 0xF];
            out[1] = hex[b & 0xF];
            out[2] = '\0';
            serial_print(out);
            if ((j & 1) == 1)
                serial_print(" ");
        }
        serial_print("\n");
    }

    if (elf_read_user_u64(proc->page_directory, (uintptr_t)rsp, &argc) != 0)
    {
        serial_print("[FASE50][EXEC_STACK] argc=read_fail\n");
        return;
    }
    if (argc > (uint64_t)(ELF_ARG_MAX * 4))
    {
        serial_print("[FASE50][EXEC_STACK] argc_suspicious=");
        serial_print_hex64(argc);
        serial_print("\n");
        return;
    }

    argv_base = rsp + sizeof(uint64_t);
    envp_base = argv_base + (argc + 1) * sizeof(uint64_t);
    cursor = envp_base;

    serial_print("[FASE50][EXEC_STACK] argc=");
    serial_print_hex64(argc);
    serial_print(" argv_base=");
    serial_print_hex64(argv_base);
    serial_print(" envp_base=");
    serial_print_hex64(envp_base);
    serial_print("\n");

    for (i = 0; i < 8; i++)
    {
        uint64_t ptr = 0;
        if (elf_read_user_u64(proc->page_directory,
                              (uintptr_t)(argv_base + (uint64_t)i * sizeof(uint64_t)),
                              &ptr) != 0)
            break;
        serial_print("[FASE50][EXEC_STACK_ARGV] idx=");
        serial_print_hex32((uint32_t)i);
        serial_print(" ptr=");
        serial_print_hex64(ptr);
        serial_print("\n");
        if (ptr == 0)
            break;
    }

    for (i = 0; i < 8; i++)
    {
        uint64_t ptr = 0;
        if (elf_read_user_u64(proc->page_directory,
                              (uintptr_t)(envp_base + (uint64_t)i * sizeof(uint64_t)),
                              &ptr) != 0)
            break;
        serial_print("[FASE50][EXEC_STACK_ENVP] idx=");
        serial_print_hex32((uint32_t)i);
        serial_print(" ptr=");
        serial_print_hex64(ptr);
        serial_print("\n");
        cursor += sizeof(uint64_t);
        if (ptr == 0)
            break;
    }

    if ((cursor & 0xFULL) != 0)
        serial_print("[FASE50][EXEC_STACK] warn=auxv_cursor_unaligned\n");

    for (i = 0; i < 20; i++)
    {
        uint64_t a_type = 0;
        uint64_t a_val = 0;
        if (elf_read_user_u64(proc->page_directory, (uintptr_t)cursor, &a_type) != 0 ||
            elf_read_user_u64(proc->page_directory,
                              (uintptr_t)(cursor + sizeof(uint64_t)),
                              &a_val) != 0)
            break;

        serial_print("[FASE50][EXEC_STACK_AUXV] idx=");
        serial_print_hex32((uint32_t)i);
        serial_print(" type=");
        serial_print_hex64(a_type);
        serial_print(" val=");
        serial_print_hex64(a_val);
        serial_print("\n");

        cursor += 2 * sizeof(uint64_t);
        if (a_type == AT_NULL)
            break;
    }
#endif
}

static int validate_elf_header(const elf64_header_t *header)
{
    /* Check ELF magic number */
    if (header->e_ident[0] != ELF_MAGIC_0 ||
        header->e_ident[1] != ELF_MAGIC_1 ||
        header->e_ident[2] != ELF_MAGIC_2 ||
        header->e_ident[3] != ELF_MAGIC_3)
    {
        return 0;
    }

    /* Check 64-bit and x86-64 architecture */
    if (header->e_ident[4] != ELFCLASS64 || header->e_machine != EM_X86_64)
    {
        return 0;
    }

    /* ET_EXEC (static) or ET_DYN (PIE) */
    if (header->e_type != ET_EXEC && header->e_type != ET_DYN)
        return 0;

    return 1;
}

static uint64_t elf_compute_load_base(const elf64_header_t *header, const uint8_t *file_data)
{
    uint16_t phnum = header->e_phnum;
    uint64_t base = (uint64_t)-1;
    elf64_phdr_t *phdr;
    int i;

    if (phnum > ELF_MAX_PHNUM)
        phnum = ELF_MAX_PHNUM;

    if (header->e_phoff > (uint64_t)-1 || !file_data)
        return 0;

    phdr = (elf64_phdr_t *)(file_data + header->e_phoff);

    for (i = 0; i < (int)phnum; i++)
    {
        if (phdr[i].p_type != PT_LOAD)
            continue;
        if (phdr[i].p_vaddr < base)
            base = phdr[i].p_vaddr;
    }

    return (base == (uint64_t)-1) ? 0 : base;
}

static uint64_t elf_compute_at_phdr(const elf64_header_t *header, const uint8_t *file_data)
{
    uint16_t phnum = header->e_phnum;
    elf64_phdr_t *phdr;
    int i;

    if (phnum > ELF_MAX_PHNUM)
        phnum = ELF_MAX_PHNUM;

    if (header->e_phoff > (uint64_t)-1 || !file_data)
        return 0;

    phdr = (elf64_phdr_t *)(file_data + header->e_phoff);

    for (i = 0; i < (int)phnum; i++)
    {
        if (phdr[i].p_type == PT_PHDR)
            return phdr[i].p_vaddr;
    }

    for (i = 0; i < (int)phnum; i++)
    {
        if (phdr[i].p_type != PT_LOAD)
            continue;
        if (header->e_phoff >= phdr[i].p_offset &&
            header->e_phoff < phdr[i].p_offset + phdr[i].p_filesz)
            return phdr[i].p_vaddr + (header->e_phoff - phdr[i].p_offset);
    }

    return 0;
}

static void elf_fill_random_bytes(uint8_t *buf, size_t len)
{
    uint64_t tsc;
    size_t i;

#if defined(__x86_64__) || defined(__i386__)
    __asm__ volatile("rdtsc" : "=a"(tsc) : : "rdx");
#else
    tsc = 0x5851f42d4c957f2dULL;
#endif

    for (i = 0; i < len; i++)
    {
        tsc = tsc * 6364136223846793005ULL + 1ULL;
        buf[i] = (uint8_t)(tsc >> 33);
    }
}

/* Load ELF segments into memory at correct virtual addresses */
static int elf_load_segments(elf64_header_t *header, uint8_t *file_data, size_t file_size,
                             process_t *process)
{
    uint16_t phnum = header->e_phnum;
    if (phnum > ELF_MAX_PHNUM)
        phnum = ELF_MAX_PHNUM;

    if (header->e_phoff > file_size)
    {
        serial_print("SERIAL: ELF: Program header table offset out of bounds\n");
        return -1;
    }

    {
        uint64_t ph_bytes = (uint64_t)phnum * sizeof(elf64_phdr_t);
        if (ph_bytes > (uint64_t)file_size - header->e_phoff)
        {
            serial_print("SERIAL: ELF: Program header table extends past file\n");
            return -1;
        }
    }

    elf64_phdr_t *phdr = (elf64_phdr_t *)(file_data + header->e_phoff);

    serial_print("SERIAL: ELF: Loading ");
    serial_print_hex32(phnum);
    serial_print(" program segments\n");

    /* Get process page directory */
    uint64_t *pml4 = process->page_directory;
    if (!pml4)
    {
        serial_print("SERIAL: ELF: Process has no page directory\n");
        return -1;
    }

    /*
     * Phase 1 — map all PT_LOAD regions under kernel CR3.  Page-table
     * allocation uses the kernel heap and must not run with child CR3 active.
     */
    for (int i = 0; i < (int)phnum; i++)
    {
        if (phdr[i].p_type != PT_LOAD)
            continue;

        if (phdr[i].p_memsz < phdr[i].p_filesz)
        {
            serial_print("SERIAL: ELF: PT_LOAD p_memsz < p_filesz\n");
            return -1;
        }

        if (phdr[i].p_filesz > 0)
        {
            if (phdr[i].p_offset > file_size ||
                phdr[i].p_filesz > (uint64_t)file_size - phdr[i].p_offset)
            {
                serial_print("SERIAL: ELF: PT_LOAD segment file range out of bounds\n");
                return -1;
            }
        }

        serial_print("SERIAL: ELF: Mapping segment ");
        serial_print_hex32(i);
        serial_print(" at vaddr 0x");
        serial_print_hex32((uint32_t)phdr[i].p_vaddr);
        serial_print(" size 0x");
        serial_print_hex32((uint32_t)phdr[i].p_memsz);
        serial_print("\n");

        {
            uint64_t memsz = phdr[i].p_memsz;
            uintptr_t vaddr = phdr[i].p_vaddr;
            uintptr_t vaddr_aligned = vaddr & ~0xFFF;
            size_t size_aligned = ((vaddr + memsz + 0xFFF) & ~0xFFF) - vaddr_aligned;
            uint64_t flags = PAGE_USER;

            if (phdr[i].p_flags & 2)
                flags |= PAGE_RW;
            if (phdr[i].p_flags & 1)
                flags |= PAGE_EXEC;

            if (map_user_region_in_directory(pml4, vaddr_aligned, size_aligned, flags) != 0)
            {
                serial_print("SERIAL: ELF: Failed to map user memory region\n");
                return -1;
            }
        }
    }

    /*
     * Phase 2 — copy segment bytes via physical frames (kernel CR3).
     */
    for (int i = 0; i < (int)phnum; i++)
    {
        if (phdr[i].p_type != PT_LOAD)
            continue;

        {
            uintptr_t vaddr = phdr[i].p_vaddr;

            if (phdr[i].p_filesz > 0)
            {
                if (copy_to_user_region_in_directory(pml4, vaddr,
                        file_data + phdr[i].p_offset,
                        (size_t)phdr[i].p_filesz) != 0)
                {
                    serial_print("SERIAL: ELF: Failed to copy segment data\n");
                    return -1;
                }
                serial_print("SERIAL: ELF: Copied ");
                serial_print_hex32((uint32_t)phdr[i].p_filesz);
                serial_print(" bytes from file to vaddr 0x");
                serial_print_hex32((uint32_t)vaddr);
                serial_print("\n");
            }

            if (phdr[i].p_memsz > phdr[i].p_filesz)
            {
                if (zero_user_region_in_directory(pml4,
                        vaddr + phdr[i].p_filesz,
                        (size_t)(phdr[i].p_memsz - phdr[i].p_filesz)) != 0)
                {
                    serial_print("SERIAL: ELF: Failed to zero BSS\n");
                    return -1;
                }
                serial_print("SERIAL: ELF: Zeroed BSS section\n");
            }
        }
    }

    return 0;
}

/* Dummy entry function for ELF processes (will be overridden) */
static void elf_dummy_entry(void)
{
    /* This should never be called as we override RIP */
    panic("ELF dummy entry should never be called :)\n");
}

/* Create a new process for the ELF program */
static process_t *elf_create_process(elf64_header_t *header, const char *path)
{

    serial_print("SERIAL: ELF: Creating process for ");
    serial_print(path);
    serial_print(" with entry point 0x");
    serial_print_hex32((uint32_t)header->e_entry);
    serial_print("\n");

    /* Extract basename for process name */
    const char *basename = path;
    const char *last_slash = path;
    while (*path) {
        if (*path == '/') last_slash = path + 1;
        path++;
    }
    basename = last_slash;

    /* Create user process using spawn (creates isolated page directory) */
    /* Use dummy entry that will be overridden with ELF entry point */
    /* ELF binaries always run in USER_MODE */
    pid_t pid = spawn_user(elf_dummy_entry, basename);
    if (pid < 0)
    {
        serial_print("SERIAL: ELF: Failed to create process\n");
        return NULL;
    }

    /* Find the created process */
    process_t *process = process_find_by_pid(pid);
    if (!process)
    {
        serial_print("SERIAL: ELF: Failed to find created process\n");
        return NULL;
    }

    /*
     * spawn() enqueues the task; keep it off the run queue until PT_LOAD
     * segments and stack are fully initialized (timer IRQ may preempt kmain).
     */
    sched_remove_process(process);

    /* Set up user mode execution */
    /* spawn() already set up the process structure correctly */
    /* We just need to set the entry point (will be done after loading segments) */
    
    /* Set entry point (will be adjusted after segments are loaded) */
    process->task.rip = header->e_entry;
    process->task.cs = 0x1B; // User code segment (GDT entry 3, RPL=3)
    process->task.ss = 0x23; // User data segment (GDT entry 4, RPL=3)
    process->task.ds = 0x23; // User data segment
    process->task.es = 0x23; // User data segment

    /* Stack is already set up by spawn() at 0x7FFFF000 */

    /* Enable interrupts in user mode */
    process->task.rflags = ir0_rflags_sanitize_user(0x202ULL);

    serial_print("SERIAL: ELF: Process created with PID ");
    serial_print_hex32(process->task.pid);
    serial_print("\n");
    serial_print("SERIAL: ELF: Entry point: 0x");
    serial_print_hex32((uint32_t)process->task.rip);
    serial_print("\n");
    serial_print("SERIAL: ELF: Stack: 0x");
    serial_print_hex32((uint32_t)process->task.rsp);
    serial_print("\n");

    return process;
}

/**
 * elf_setup_stack - Initialize stack with argc, argv, envp (x86-64 ABI)
 * @process: Process to set up stack for
 * @argv: Command line arguments (NULL-terminated)
 * @envp: Environment variables (NULL-terminated)
 * 
 * Sets up the stack according to x86-64 ABI:
 * - argc at bottom of stack
 * - argv[] array (pointers, NULL-terminated)
 * - envp[] array (pointers, NULL-terminated)
 * - Argument strings
 * - Environment strings
 * 
 * Registers will be set by context switch:
 * - rdi = argc
 * - rsi = argv
 * - rdx = envp
 */
static int elf_setup_stack(process_t *process, char *const argv[], char *const envp[],
                           const elf64_header_t *header, uint64_t at_phdr,
                           uint64_t at_base, const char *builder_tag,
                           const char *image_path)
{
    if (!process || process->mode != USER_MODE)
        return -1;
    
    /* Count arguments (cap to ELF_ARG_MAX) */
    int argc = 0;
    if (argv)
    {
        while (argc < ELF_ARG_MAX && argv[argc])
            argc++;
    }
    serial_print("[FASE50][EXEC_ARGV] stage=stack-builder-enter builder=");
    serial_print(builder_tag ? builder_tag : "(null)");
    serial_print(" pid=");
    serial_print_hex32(process ? (uint32_t)process->task.pid : 0);
    serial_print(" path=");
    serial_print(image_path ? image_path : "(null)");
    serial_print(" argc_in=");
    serial_print_hex64((uint64_t)argc);
    serial_print("\n");
    for (int i = 0; i < argc && i < 8; i++)
    {
        serial_print("[FASE50][EXEC_ARGV] stage=stack-builder-argv idx=");
        serial_print_hex32((uint32_t)i);
        serial_print(" str=");
        serial_print(argv[i] ? argv[i] : "(null)");
        serial_print("\n");
    }

    /* Count environment variables (cap to ELF_ARG_MAX) */
    int envc = 0;
    if (envp)
    {
        while (envc < ELF_ARG_MAX && envp[envc])
            envc++;
    }
    
    /* Calculate total stack size needed */
    size_t strings_size = 0;
    
    /* Calculate argv strings size */
    for (int i = 0; i < argc; i++)
    {
        if (argv[i])
            strings_size += strlen(argv[i]) + 1;
    }
    
    /* Calculate envp strings size */
    for (int i = 0; i < envc; i++)
    {
        if (envp[i])
            strings_size += strlen(envp[i]) + 1;
    }
    
    /* Total size: argc slot + argv[] + envp[] + auxv[] + strings + alignment */
    size_t stack_size = sizeof(uint64_t) +
                       (argc + 1) * sizeof(uint64_t) +
                       (envc + 1) * sizeof(uint64_t) +
                       ELF_AUXV_PAIRS * 2 * sizeof(uint64_t) +
                       ELF_AT_RANDOM_BYTES +
                       strings_size +
                       16;
    
    /* Leave 256 bytes margin for safety */
    size_t stack_margin = 256;
    if (stack_size > (process->stack_size - stack_margin))
    {
        serial_print("SERIAL: ELF: ERROR - Stack too small for arguments (need ");
        serial_print_hex32((uint32_t)stack_size);
        serial_print(" bytes, have ");
        serial_print_hex32((uint32_t)process->stack_size);
        serial_print(")\n");
        return -ENOMEM;
    }
    
    /* Switch to process page directory temporarily (after kernel heap allocs) */
    uint64_t *argv_ptrs = (uint64_t *)kmalloc((argc + 1) * sizeof(uint64_t));
    uint64_t *envp_ptrs = (uint64_t *)kmalloc((envc + 1) * sizeof(uint64_t));

    if (!argv_ptrs || !envp_ptrs)
    {
        if (argv_ptrs)
            kfree(argv_ptrs);
        if (envp_ptrs)
            kfree(envp_ptrs);
        return -1;
    }

    uint64_t stack_top = process->stack_start + process->stack_size;
    uint64_t stack_base = stack_top - stack_size;
    uint64_t argc_slot;
    uint64_t argv_array;
    uint64_t envp_array;
    uint64_t auxv_base;
    uint64_t random_base;
    uint64_t strings_base;
    uint64_t current_string_ptr;
    uint64_t *pml4 = process->page_directory;

    stack_base &= ~0xFULL;

    argc_slot = stack_base;
    argv_array = argc_slot + sizeof(uint64_t);
    envp_array = argv_array + (argc + 1) * sizeof(uint64_t);
    auxv_base = envp_array + (envc + 1) * sizeof(uint64_t);
    random_base = auxv_base + ELF_AUXV_PAIRS * 2 * sizeof(uint64_t);
    strings_base = random_base + ELF_AT_RANDOM_BYTES;

    /* Copy argv/env strings into userspace */
    current_string_ptr = strings_base;

    for (int i = 0; i < argc; i++)
    {
        if (argv[i])
        {
            size_t len = strlen(argv[i]) + 1;

            if (copy_to_user_region_in_directory(pml4, current_string_ptr,
                                                 argv[i], len) != 0)
            {
                kfree(argv_ptrs);
                kfree(envp_ptrs);
                return -1;
            }
            argv_ptrs[i] = current_string_ptr;
            serial_print("[FASE50][EXEC_ARGV] stage=stack-builder-argv-ptr idx=");
            serial_print_hex32((uint32_t)i);
            serial_print(" user_dst=");
            serial_print_hex64(argv_ptrs[i]);
            serial_print("\n");
            current_string_ptr += len;
        }
    }

    for (int i = 0; i < envc; i++)
    {
        if (envp[i])
        {
            size_t len = strlen(envp[i]) + 1;

            if (copy_to_user_region_in_directory(pml4, current_string_ptr,
                                                 envp[i], len) != 0)
            {
                kfree(argv_ptrs);
                kfree(envp_ptrs);
                return -1;
            }
            envp_ptrs[i] = current_string_ptr;
            current_string_ptr += len;
        }
    }

    /* argc at [RSP+0] per SysV ABI process entry stack contract. */
    {
        uint64_t argc_q = (uint64_t)argc;
        if (copy_to_user_region_in_directory(pml4, argc_slot, &argc_q, sizeof(uint64_t)) != 0)
        {
            kfree(argv_ptrs);
            kfree(envp_ptrs);
            return -1;
        }
    }

    /* argv[] */
    for (int i = 0; i < argc; i++)
    {
        uint64_t ptr = argv_ptrs[i];

        if (copy_to_user_region_in_directory(pml4,
                argv_array + (size_t)i * sizeof(uint64_t),
                &ptr, sizeof(uint64_t)) != 0)
        {
            kfree(argv_ptrs);
            kfree(envp_ptrs);
            return -1;
        }
    }

    {
        uint64_t zero = 0;

        if (copy_to_user_region_in_directory(pml4,
                argv_array + (size_t)argc * sizeof(uint64_t),
                &zero, sizeof(uint64_t)) != 0)
        {
            kfree(argv_ptrs);
            kfree(envp_ptrs);
            return -1;
        }
    }

    /* envp[] */
    for (int i = 0; i < envc; i++)
    {
        uint64_t ptr = envp_ptrs[i];

        if (copy_to_user_region_in_directory(pml4,
                envp_array + (size_t)i * sizeof(uint64_t),
                &ptr, sizeof(uint64_t)) != 0)
        {
            kfree(argv_ptrs);
            kfree(envp_ptrs);
            return -1;
        }
    }

    {
        uint64_t zero = 0;

        if (copy_to_user_region_in_directory(pml4,
                envp_array + (size_t)envc * sizeof(uint64_t),
                &zero, sizeof(uint64_t)) != 0)
        {
            kfree(argv_ptrs);
            kfree(envp_ptrs);
            return -1;
        }
    }

    /* 16-byte AT_RANDOM seed on stack */
    {
        uint8_t random_seed[ELF_AT_RANDOM_BYTES];

        elf_fill_random_bytes(random_seed, sizeof(random_seed));
        if (copy_to_user_region_in_directory(pml4, random_base, random_seed,
                                             sizeof(random_seed)) != 0)
        {
            kfree(argv_ptrs);
            kfree(envp_ptrs);
            return -1;
        }
    }

    /* auxv[] — musl walks past envp NULL to find these */
    {
        uint64_t at_secure = 0;

        if (process->uid != process->euid || process->gid != process->egid)
            at_secure = 1;

        struct
        {
            uint64_t a_type;
            uint64_t a_val;
        } auxv[ELF_AUXV_PAIRS] = {
            { AT_PHDR, at_phdr },
            { AT_PHENT, header ? header->e_phentsize : 0 },
            { AT_PHNUM, header ? header->e_phnum : 0 },
            { AT_UID, process->uid },
            { AT_EUID, process->euid },
            { AT_GID, process->gid },
            { AT_EGID, process->egid },
            { AT_CLKTCK, AT_CLKTCK_VALUE },
            { AT_SECURE, at_secure },
            { AT_FLAGS, 0 },
            { AT_RANDOM, random_base },
            { AT_PAGESZ, 4096 },
            { AT_BASE, at_base },
            { AT_ENTRY, process->task.rip },
            { AT_NULL, 0 },
        };
        size_t i;

        for (i = 0; i < ELF_AUXV_PAIRS; i++)
        {
            uint64_t off = i * 2 * sizeof(uint64_t);

            if (copy_to_user_region_in_directory(pml4, auxv_base + off,
                    &auxv[i].a_type, sizeof(uint64_t)) != 0 ||
                copy_to_user_region_in_directory(pml4,
                    auxv_base + off + sizeof(uint64_t),
                    &auxv[i].a_val, sizeof(uint64_t)) != 0)
            {
                kfree(argv_ptrs);
                kfree(envp_ptrs);
                return -1;
            }
        }
    }

    process->task.rsp = argc_slot;
    process->task.rbp = argc_slot;
    
    /* Set registers for x86-64 ABI: rdi=argc, rsi=argv, rdx=envp */
    process->task.rdi = (uint64_t)argc;
    process->task.rsi = argv_array;
    process->task.rdx = envp_array;

    kfree(argv_ptrs);
    kfree(envp_ptrs);
    
    serial_print("SERIAL: ELF: Stack initialized: argc=");
    serial_print_hex32(argc);
    serial_print(", argv=");
    serial_print_hex32((uint32_t)argv_array);
    serial_print(", envp=");
    serial_print_hex32((uint32_t)envp_array);
    serial_print("\n");
    serial_print("[FASE50][EXEC_ARGV] stage=stack-builder-final builder=");
    serial_print(builder_tag ? builder_tag : "(null)");
    serial_print(" pid=");
    serial_print_hex32((uint32_t)process->task.pid);
    serial_print(" path=");
    serial_print(image_path ? image_path : "(null)");
    serial_print(" argc_stack=");
    serial_print_hex64((uint64_t)argc);
    serial_print(" argv_array=");
    serial_print_hex64(argv_array);
    serial_print("\n");
    elf_trace_argv_contract(process, image_path, "stack-builder-final");
    elf_trace_entry_stack_layout(process, header, at_phdr, at_base, "elf_setup_stack-final");
    
    return 0;
}

static uint64_t fase41_count_vmas(const process_t *proc)
{
    uint64_t count = 0;
    const struct mmap_region *r;

    if (!proc)
        return 0;

    for (r = proc->mmap_list; r; r = r->next)
        count++;

    return count;
}

/**
 * kexecve - Load and execute ELF binary (kernel-level exec)
 * @path: Path to ELF executable file
 * @argv: Command line arguments (NULL-terminated, can be NULL)
 * @envp: Environment variables (NULL-terminated, can be NULL)
 *
 * This function loads an ELF binary from the filesystem, creates a process,
 * maps the segments into memory, and schedules the process for execution.
 * This is the kernel-level equivalent of execve() syscall.
 *
 * Algorithm:
 * 1. Read ELF file from filesystem via VFS
 * 2. Validate ELF header (magic, architecture, type)
 * 3. Create process structure with proper page directory
 * 4. Load ELF segments into memory at virtual addresses
 * 5. Set up entry point, stack with argc/argv/envp, and registers
 * 6. Free the in-kernel ELF buffer and enqueue the process for execution
 *
 * Returns: Process PID on success, -1 on error
 *
 * Thread safety: NOT thread-safe - should be called from process context
 */
int kexecve(const char *path, char *const argv[], char *const envp[])
{
    serial_print("SERIAL: ELF: ========================================\n");
    serial_print("SERIAL: ELF: Loading ELF file: ");
    serial_print(path);
    serial_print("\n");

    /* Step 1: Read the ELF file from filesystem */
    void *file_data = NULL;
    size_t file_size = 0;

    int result = vfs_read_file(path, &file_data, &file_size);
    if (result != 0 || !file_data)
    {
        serial_print("SERIAL: ELF: ERROR - Failed to read file from filesystem\n");
        return -1;
    }

    serial_print("SERIAL: ELF: File loaded successfully, size: ");
    serial_print_hex32(file_size);
    serial_print(" bytes\n");

    /* Step 2: Validate ELF header */
    if (!validate_elf_header((elf64_header_t *)file_data))
    {
        serial_print("SERIAL: ELF: ERROR - Invalid ELF header\n");
        kfree(file_data);
        return -1;
    }

    elf64_header_t *header = (elf64_header_t *)file_data;
    uint64_t at_phdr;
    uint64_t at_base;
    serial_print("SERIAL: ELF: Header validation passed\n");

    /* Step 3: Create process first */
    process_t *process = elf_create_process(header, path);
    if (!process)
    {
        serial_print("SERIAL: ELF: ERROR - Failed to create process\n");
        kfree(file_data);
        return -1;
    }

    /* Step 4: Load segments into memory */
    if (elf_load_segments(header, (uint8_t *)file_data, file_size, process) != 0)
    {
        serial_print("SERIAL: ELF: ERROR - Failed to load segments\n");
        /*
         * spawn_user() already queued this process; drop it from the scheduler
         * and free the struct so we do not run a half-loaded image.
         */
        sched_remove_process(process);
        (void)process_remove_from_list(process);
        process_destroy(process);
        kfree(process);
        kfree(file_data);
        return -1;
    }

    /* Step 5: Set up stack with argc/argv/envp */
    at_phdr = elf_compute_at_phdr(header, (uint8_t *)file_data);
    at_base = elf_compute_load_base(header, (uint8_t *)file_data);
    if (elf_setup_stack(process, argv, envp, header, at_phdr, at_base,
                        "kexecve", path) != 0)
    {
        serial_print("SERIAL: ELF: WARNING - Failed to set up stack arguments, continuing anyway\n");
        /* Continue even if stack setup fails - some binaries don't need args */
    }

    sched_add_process(process);

    /* Step 6: Clean up file data */
    kfree(file_data);

    serial_print("SERIAL: ELF: SUCCESS - Program loaded and scheduled for execution\n");
    serial_print("SERIAL: ELF: PID: ");
    serial_print_hex32(process->task.pid);
    serial_print(" Entry: 0x");
    serial_print_hex32((uint32_t)process->task.rip);
    serial_print("\n");
    serial_print("SERIAL: ELF: ========================================\n");

    return process->task.pid;
}

/*
 * exec_commit_ctx - per-exec_replace_current commit audit (single-threaded kernel).
 */
static struct
{
	uint64_t mm_entry;
	uint64_t task_cr3_entry;
	uint64_t active_cr3_entry;
	uint64_t entry_rip;
	uint64_t task_rip_final;
	uint64_t task_rsp_final;
	int unmapped;
	int segments_loaded;
	int stack_ready;
	const char *fail_point;
} exec_commit_ctx;

static void exec_commit_emit(const char *point, int64_t errno_val,
                             process_t *proc, const char *classify)
{
	serial_print("[EXEC_COMMIT] point=");
	serial_print(point ? point : "(null)");
	serial_print(" classify=");
	serial_print(classify ? classify : "(null)");
	serial_print(" errno=");
	serial_print_hex64((uint64_t)errno_val);
	serial_print(" pid=");
	serial_print_hex32(proc ? (uint32_t)proc->task.pid : 0);
	serial_print(" mm_entry=");
	serial_print_hex64(exec_commit_ctx.mm_entry);
	serial_print(" mm_now=");
	serial_print_hex64(proc ? (uint64_t)(uintptr_t)proc->page_directory : 0);
	serial_print(" mm_same=");
	serial_print((proc && (uint64_t)(uintptr_t)proc->page_directory ==
	              exec_commit_ctx.mm_entry) ? "1" : "0");
	serial_print(" task_cr3_entry=");
	serial_print_hex64(exec_commit_ctx.task_cr3_entry);
	serial_print(" task_cr3_now=");
	serial_print_hex64(proc ? proc->task.cr3 : 0);
	serial_print(" active_cr3=");
	serial_print_hex64(get_current_page_directory());
	serial_print(" cr3_activate=");
	serial_print((point && strcmp(point, "before-userswitch") == 0) ?
	             "arch_switch_to_user_asm" : "not_yet");
	serial_print(" entry_rip=");
	serial_print_hex64(exec_commit_ctx.entry_rip);
	serial_print(" task_rip=");
	serial_print_hex64(proc ? proc->task.rip : exec_commit_ctx.task_rip_final);
	serial_print(" task_rsp=");
	serial_print_hex64(proc ? proc->task.rsp : exec_commit_ctx.task_rsp_final);
	serial_print(" unmapped=");
	serial_print(exec_commit_ctx.unmapped ? "1" : "0");
	serial_print(" loaded=");
	serial_print(exec_commit_ctx.segments_loaded ? "1" : "0");
	serial_print(" stack=");
	serial_print(exec_commit_ctx.stack_ready ? "1" : "0");
	serial_print("\n");
	serial_print("[EXEC_COMMIT][CLASSIFY] ");
	serial_print(classify ? classify : "(null)");
	serial_print("\n");
}

static const char *exec_audit_classify_vfs(int vfs_ret, size_t file_size,
                                           void *file_data, const char *path)
{
	if (!path)
		return "EXEC_PATH_COPY_BAD";

	if (vfs_ret == -EFAULT)
		return "EXEC_PATH_COPY_BAD";

	if (vfs_ret == -ENOENT || vfs_ret == -ENOSYS)
	{
		if (strcmp(path, "/bin/busybox") == 0)
			return "EXEC_BUSYBOX_FILE_MISSING_OR_TRUNCATED";
		return "EXEC_LOOKUP_FAIL";
	}

	if (vfs_ret == -EINVAL)
		return "EXEC_OPEN_FAIL";

	if (vfs_ret != 0)
		return "EXEC_VFS_READ_ERR";

	if (file_size == 0 || !file_data)
	{
		if (path && strcmp(path, "/bin/busybox") == 0)
			return "EXEC_BUSYBOX_FILE_MISSING_OR_TRUNCATED";
		return "EXEC_VFS_READ_ZERO";
	}

	return NULL;
}

static const char *exec_audit_classify_elf(const elf64_header_t *header,
                                           size_t file_size)
{
	if (!header || file_size < sizeof(elf64_header_t))
		return "EXEC_VFS_READ_SHORT";

	if (header->e_ident[0] != ELF_MAGIC_0 ||
	    header->e_ident[1] != ELF_MAGIC_1 ||
	    header->e_ident[2] != ELF_MAGIC_2 ||
	    header->e_ident[3] != ELF_MAGIC_3)
		return "EXEC_ELF_MAGIC_BAD";

	return NULL;
}

static void exec_audit_emit_elf_header(const uint8_t *file_data, size_t file_size)
{
	size_t i;
	const elf64_header_t *header;

	if (!file_data || file_size < 4)
	{
		serial_print("[EXEC_AUDIT][ELF] stage=header_read bytes=0 magic=(none)\n");
		return;
	}

	header = (const elf64_header_t *)file_data;
	serial_print("[EXEC_AUDIT][ELF] stage=header_read expect=");
	serial_print_hex64((uint64_t)sizeof(elf64_header_t));
	serial_print(" got=");
	serial_print_hex64((uint64_t)file_size);
	serial_print(" magic=");
	for (i = 0; i < 4; i++)
	{
		if (i > 0)
			serial_print(" ");
		serial_print_hex64((uint64_t)file_data[i]);
	}
	serial_print(" e_type=");
	serial_print_hex64((uint64_t)header->e_type);
	serial_print(" e_machine=");
	serial_print_hex64((uint64_t)header->e_machine);
	serial_print("\n");
}

/**
 * exec_replace_current - Replace the current user process image in-place.
 * @path: Path to ELF executable
 * @argv: Command line arguments (NULL-terminated, may be NULL)
 * @envp: Environment variables (NULL-terminated, may be NULL)
 *
 * Unmaps the current user address space, reloads ELF segments into the same
 * process (same PID), sets up a fresh stack/auxv, and jumps to user mode.
 * Does not return on success.
 */
static void exec_fail_kill(process_t *proc, int code, const char *point)
{
	exec_commit_ctx.fail_point = point;
	exec_commit_emit(point ? point : "exec_fail_kill", (int64_t)code, proc,
	                 "EXEC_LOADER_FAIL");
	serial_print("[FASE50][TRACE] stage=exec_fail_kill pid=");
	serial_print_hex32(proc ? (uint32_t)proc->task.pid : 0);
	serial_print(" code=");
	serial_print_hex64((uint64_t)(uint32_t)code);
	serial_print(" exit_via=exec_fail_kill explicit=1 signal=0 point=");
	serial_print(point ? point : "(null)");
	serial_print(" current=");
	serial_print_hex64((uint64_t)(uintptr_t)current_process);
	serial_print(" active_cr3=");
	serial_print_hex64(get_current_page_directory());
	serial_print("\n");
	process_fase43_proc_audit("exec-fail-kill");
	paging_fase43_oom_audit("exec-fail-kill");
	process_exit(code);
}

int exec_replace_current(const char *path, char *const argv[], char *const envp[])
{
    process_t *proc = current_process;
    void *file_data = NULL;
    size_t file_size = 0;
    elf64_header_t *header;
    uint64_t at_phdr;
    uint64_t at_base;
    const char *basename;
    const char *last_slash;
    const char *walk;
    size_t total_frames_before = 0;
    size_t used_frames_before = 0;
    size_t total_frames_after = 0;
    size_t used_frames_after = 0;
    uint64_t vmas_before = 0;
    uint64_t vmas_after = 0;

    if (!proc || proc->mode != USER_MODE || !path)
    {
        exec_commit_emit("return-entry-invalid", -1, proc,
                         "EXEC_ABORT_BEFORE_COMMIT");
        return -1;
    }

    memset(&exec_commit_ctx, 0, sizeof(exec_commit_ctx));
    exec_commit_ctx.mm_entry = (uint64_t)(uintptr_t)proc->page_directory;
    exec_commit_ctx.task_cr3_entry = proc->task.cr3;
    exec_commit_ctx.active_cr3_entry = get_current_page_directory();

    serial_print("[FASE50][TRACE] stage=exec_replace_current-entry pid=");
    serial_print_hex32(proc ? (uint32_t)proc->task.pid : 0);
    serial_print(" proc=");
    serial_print_hex64((uint64_t)(uintptr_t)proc);
    serial_print(" mm=");
    serial_print_hex64(proc ? (uint64_t)(uintptr_t)proc->page_directory : 0);
    serial_print(" files=");
    serial_print_hex64(proc ? (uint64_t)(uintptr_t)proc->fd_table : 0);
    serial_print(" state=");
    serial_print_hex64((uint64_t)(proc ? proc->state : 0));
    serial_print(" task_cr3=");
    serial_print_hex64(proc ? proc->task.cr3 : 0);
    serial_print(" active_cr3=");
    serial_print_hex64(get_current_page_directory());
    serial_print("\n");
    paging_fase42_checkpoint("exec-before", (int32_t)proc->task.pid);
    process_fase44_list_checkpoint("exec-before");
    pmm_stats(&total_frames_before, &used_frames_before, NULL);
    vmas_before = fase41_count_vmas(proc);

    serial_print("SERIAL: ELF: exec_replace_current: ");
    serial_print(path);
    serial_print("\n");

    if (argv)
    {
        int ai;

        serial_print("[EXEC_AUDIT][LOADER] path=");
        serial_print(path ? path : "(null)");
        serial_print(" argv=");
        for (ai = 0; ai < 4 && argv[ai]; ai++)
        {
            if (ai > 0)
                serial_print(",");
            serial_print(argv[ai]);
        }
        serial_print("\n");
    }

    vfs_exec_audit_begin(path);
    {
        int vfs_ret = vfs_read_file(path, &file_data, &file_size);
        const char *vfs_class;

        vfs_exec_audit_end();
        vfs_class = exec_audit_classify_vfs(vfs_ret, file_size, file_data, path);
        if (vfs_ret != 0 || !file_data)
        {
            serial_print("[EXEC_AUDIT][CLASSIFY] ");
            serial_print(vfs_class ? vfs_class : "EXEC_VFS_READ_ERR");
            serial_print(" vfs_ret=");
            serial_print_hex64((uint64_t)(int64_t)vfs_ret);
            serial_print("\n");
            serial_print("[EXEC_ONLY][LOADER] vfs_read_fail path=");
            serial_print(path ? path : "(null)");
            serial_print(" errno=");
            serial_print_hex64((uint64_t)(int64_t)vfs_ret);
            serial_print("\n");
            exec_commit_emit("return-vfs_read_fail", (int64_t)vfs_ret, proc,
                             vfs_class ? vfs_class : "EXEC_ABORT_BEFORE_COMMIT");
            return vfs_ret < 0 ? vfs_ret : -ENOENT;
        }
    }

    exec_audit_emit_elf_header((const uint8_t *)file_data, file_size);
    {
        const char *elf_class =
            exec_audit_classify_elf((elf64_header_t *)file_data, file_size);

        if (elf_class)
        {
            serial_print("[EXEC_AUDIT][CLASSIFY] ");
            serial_print(elf_class);
            serial_print("\n");
            kfree(file_data);
            exec_commit_emit("return-validate_elf_fail", -ENOEXEC, proc,
                             elf_class);
            return -1;
        }
    }

    if (!validate_elf_header((elf64_header_t *)file_data))
    {
        kfree(file_data);
        exec_commit_emit("return-validate_elf_fail", -ENOEXEC, proc,
                         "EXEC_ELF_MAGIC_BAD");
        return -1;
    }

    header = (elf64_header_t *)file_data;
    at_phdr = elf_compute_at_phdr(header, (uint8_t *)file_data);
    at_base = elf_compute_load_base(header, (uint8_t *)file_data);

    process_exec_close_cloexec(proc);
    serial_print("[FASE50][TRACE] stage=exec_replace_current-after-cloexec pid=");
    serial_print_hex32((uint32_t)proc->task.pid);
    serial_print(" mm=");
    serial_print_hex64((uint64_t)(uintptr_t)proc->page_directory);
    serial_print(" files=");
    serial_print_hex64((uint64_t)(uintptr_t)proc->fd_table);
    serial_print("\n");

    {
        size_t used_after_unmap = 0;

        process_unmap_user_address_space(proc);
        pmm_stats(NULL, &used_after_unmap, NULL);
        exec_commit_ctx.unmapped = 1;
        serial_print("[FASE41][EXEC_OLD_AS] pid=");
        serial_print_hex32((uint32_t)proc->task.pid);
        serial_print(" used_after_unmap=");
        serial_print_hex64((uint64_t)used_after_unmap);
        serial_print(" class=");
        serial_print("PMM_LEAK_EXEC_OLD_ADDRESS_SPACE_CHECK\n");
    }
    serial_print("[FASE50][TRACE] stage=exec_replace_current-after-unmap pid=");
    serial_print_hex32((uint32_t)proc->task.pid);
    serial_print(" mm=");
    serial_print_hex64((uint64_t)(uintptr_t)proc->page_directory);
    serial_print(" active_cr3=");
    serial_print_hex64(get_current_page_directory());
    serial_print("\n");

    while (proc->mmap_list)
    {
        struct mmap_region *next = proc->mmap_list->next;

        kfree(proc->mmap_list);
        proc->mmap_list = next;
    }

    proc->heap_start = 0;
    proc->heap_end = 0;
    proc->stack_size = USER_STACK_SIZE;
    proc->stack_start = USER_STACK_TOP - USER_STACK_SIZE;

    if (map_user_region_in_directory(proc->page_directory, proc->stack_start,
                                     proc->stack_size, PAGE_RW) != 0)
    {
        kfree(file_data);
        exec_fail_kill(proc, 127, "map_stack_fail");
    }

    if (elf_load_segments(header, (uint8_t *)file_data, file_size, proc) != 0)
    {
        kfree(file_data);
        exec_fail_kill(proc, 127, "elf_load_segments_fail");
    }
    exec_commit_ctx.segments_loaded = 1;

    basename = path;
    last_slash = path;
    walk = path;
    while (*walk)
    {
        if (*walk == '/')
            last_slash = walk + 1;
        walk++;
    }
    basename = last_slash;
    strncpy(proc->comm, basename, sizeof(proc->comm) - 1);
    proc->comm[sizeof(proc->comm) - 1] = '\0';

    proc->task.rip = header->e_entry;
    exec_commit_ctx.entry_rip = header->e_entry;
    proc->task.cs = USER_CODE_SEL;
    proc->task.ss = USER_DATA_SEL;
    proc->task.ds = USER_DATA_SEL;
    proc->task.es = USER_DATA_SEL;
    proc->task.fs = USER_DATA_SEL;
    proc->task.gs = USER_DATA_SEL;
    proc->task.rflags = ir0_rflags_sanitize_user(0x202ULL);

    if (elf_setup_stack(proc, argv, envp, header, at_phdr, at_base,
                        "exec_replace_current", path) != 0)
    {
        kfree(file_data);
        exec_fail_kill(proc, 127, "elf_setup_stack_fail");
    }
    exec_commit_ctx.stack_ready = 1;
    exec_commit_ctx.task_rip_final = proc->task.rip;
    exec_commit_ctx.task_rsp_final = proc->task.rsp;
    elf_trace_entry_stack_layout(proc, header, at_phdr, at_base, "after-setup-stack");

    kfree(file_data);
    pmm_stats(&total_frames_after, &used_frames_after, NULL);
    vmas_after = fase41_count_vmas(proc);
    serial_print("[FASE41][EXEC_RECLAIM] pid=");
    serial_print_hex32((uint32_t)proc->task.pid);
    serial_print(" vmas_before=");
    serial_print_hex64(vmas_before);
    serial_print(" vmas_after=");
    serial_print_hex64(vmas_after);
    serial_print(" pages_before=");
    serial_print_hex64((uint64_t)used_frames_before);
    serial_print(" pages_after=");
    serial_print_hex64((uint64_t)used_frames_after);
    serial_print(" total=");
    serial_print_hex64((uint64_t)total_frames_after);
    serial_print("\n");
    paging_fase42_checkpoint("exec-after", (int32_t)proc->task.pid);
    process_fase44_list_checkpoint("exec-after");

    serial_print("SERIAL: ELF: exec_replace success PID ");
    serial_print_hex32((uint32_t)proc->task.pid);
    serial_print("\n");
    serial_print("SERIAL: ELF: exec CR3 active=");
    serial_print_hex64(get_current_page_directory());
    serial_print(" task_cr3=");
    serial_print_hex64(proc->task.cr3);
    serial_print(" mm_cr3=");
    serial_print_hex64((uint64_t)(uintptr_t)proc->page_directory);
    serial_print("\n");
    serial_print("[FASE50][TRACE] stage=exec_replace_current-before-userswitch pid=");
    serial_print_hex32((uint32_t)proc->task.pid);
    serial_print(" rip=");
    serial_print_hex64(proc->task.rip);
    serial_print(" rsp=");
    serial_print_hex64(proc->task.rsp);
    serial_print(" task_cr3=");
    serial_print_hex64(proc->task.cr3);
    serial_print(" active_cr3=");
    serial_print_hex64(get_current_page_directory());
    serial_print("\n");
    elf_trace_argv_contract(proc, path, "before-iret");
    elf_trace_entry_stack_layout(proc, header, at_phdr, at_base, "before-userswitch");

    exec_commit_emit("before-userswitch", 0, proc, "EXEC_COMMIT_OK");
    arch_switch_to_user((arch_addr_t)proc->task.rip, (arch_addr_t)proc->task.rsp);
    exec_commit_emit("return-after-userswitch", -1, proc, "EXEC_COMMIT_RETURNED");
    return -1;
}