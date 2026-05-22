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
#include <mm/paging.h>
#include <mm/pmm.h>
#include <ir0/copy_user.h>
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
    process->task.rflags = 0x202; // IF=1, reserved bit=1

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
                           uint64_t at_base)
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
    
    return 0;
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
    if (elf_setup_stack(process, argv, envp, header, at_phdr, at_base) != 0)
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

    if (!proc || proc->mode != USER_MODE || !path)
        return -1;

    serial_print("SERIAL: ELF: exec_replace_current: ");
    serial_print(path);
    serial_print("\n");

    if (vfs_read_file(path, &file_data, &file_size) != 0 || !file_data)
        return -1;

    if (!validate_elf_header((elf64_header_t *)file_data))
    {
        kfree(file_data);
        return -1;
    }

    header = (elf64_header_t *)file_data;
    at_phdr = elf_compute_at_phdr(header, (uint8_t *)file_data);
    at_base = elf_compute_load_base(header, (uint8_t *)file_data);

    process_unmap_user_address_space(proc);

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
        return -1;
    }

    if (elf_load_segments(header, (uint8_t *)file_data, file_size, proc) != 0)
    {
        kfree(file_data);
        return -1;
    }

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
    proc->task.cs = USER_CODE_SEL;
    proc->task.ss = USER_DATA_SEL;
    proc->task.ds = USER_DATA_SEL;
    proc->task.es = USER_DATA_SEL;
    proc->task.fs = USER_DATA_SEL;
    proc->task.gs = USER_DATA_SEL;
    proc->task.rflags = 0x202;

    if (elf_setup_stack(proc, argv, envp, header, at_phdr, at_base) != 0)
    {
        kfree(file_data);
        return -1;
    }

    kfree(file_data);

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

    /* --- FASE 8A: dump kernel-intent task state pre arch_switch_to_user --- */
    serial_print("FASE8A pid="); serial_print_hex32((uint32_t)proc->task.pid);
    serial_print(" rip="); serial_print_hex64(proc->task.rip);
    serial_print(" rsp="); serial_print_hex64(proc->task.rsp);
    serial_print(" rflags="); serial_print_hex64(proc->task.rflags);
    serial_print("\n");
    serial_print("FASE8A rax="); serial_print_hex64(proc->task.rax);
    serial_print(" rbx="); serial_print_hex64(proc->task.rbx);
    serial_print(" rcx="); serial_print_hex64(proc->task.rcx);
    serial_print(" rdx="); serial_print_hex64(proc->task.rdx);
    serial_print("\n");
    serial_print("FASE8A rsi="); serial_print_hex64(proc->task.rsi);
    serial_print(" rdi="); serial_print_hex64(proc->task.rdi);
    serial_print(" rbp="); serial_print_hex64(proc->task.rbp);
    serial_print("\n");
    serial_print("FASE8A r8="); serial_print_hex64(proc->task.r8);
    serial_print(" r9="); serial_print_hex64(proc->task.r9);
    serial_print(" r10="); serial_print_hex64(proc->task.r10);
    serial_print(" r11="); serial_print_hex64(proc->task.r11);
    serial_print("\n");
    serial_print("FASE8A r12="); serial_print_hex64(proc->task.r12);
    serial_print(" r13="); serial_print_hex64(proc->task.r13);
    serial_print(" r14="); serial_print_hex64(proc->task.r14);
    serial_print(" r15="); serial_print_hex64(proc->task.r15);
    serial_print("\n");

    arch_switch_to_user((arch_addr_t)proc->task.rip, (arch_addr_t)proc->task.rsp);
    return -1;
}