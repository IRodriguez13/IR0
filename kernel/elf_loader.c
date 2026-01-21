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

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <ir0/kmem.h>
#include "process.h"
#include "rr_sched.h"
#include <fs/vfs.h>
#include <drivers/serial/serial.h>
#include <mm/paging.h>
#include <mm/pmm.h>

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
#define PT_LOAD 1
#define EM_X86_64 0x3e
#define ET_EXEC 2
#define PT_LOAD 1

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

    /* Check executable type */
    if (header->e_type != ET_EXEC)
    {
        return 0;
    }

    return 1;
}

/* Load ELF segments into memory at correct virtual addresses */
static int elf_load_segments(elf64_header_t *header, uint8_t *file_data, process_t *process)
{
    elf64_phdr_t *phdr = (elf64_phdr_t *)(file_data + header->e_phoff);

    serial_print("SERIAL: ELF: Loading ");
    serial_print_hex32(header->e_phnum);
    serial_print(" program segments\n");

    /* Get process page directory */
    uint64_t *pml4 = process->page_directory;
    if (!pml4)
    {
        serial_print("SERIAL: ELF: Process has no page directory\n");
        return -1;
    }

    /* Temporarily switch to process page directory to map pages */
    uint64_t old_cr3 = get_current_page_directory();
    load_page_directory((uint64_t)pml4);

    for (int i = 0; i < header->e_phnum; i++)
    {
        if (phdr[i].p_type == PT_LOAD)
        {
            serial_print("SERIAL: ELF: Loading segment ");
            serial_print_hex32(i);
            serial_print(" at vaddr 0x");
            serial_print_hex32((uint32_t)phdr[i].p_vaddr);
            serial_print(" size 0x");
            serial_print_hex32((uint32_t)phdr[i].p_memsz);
            serial_print("\n");

            /* Get segment size and virtual address */
            uint64_t memsz = phdr[i].p_memsz;
            uintptr_t vaddr = phdr[i].p_vaddr;
            
            /* Align to page boundaries */
            uintptr_t vaddr_aligned = vaddr & ~0xFFF;
            size_t size_aligned = ((vaddr + memsz + 0xFFF) & ~0xFFF) - vaddr_aligned;

            /* Determine page flags based on segment flags */
            uint64_t flags = PAGE_USER;  /* Always user mode */
            if (phdr[i].p_flags & 2)  /* PF_W (writable) */
                flags |= PAGE_RW;

            /* Map user memory region in process page directory */
            if (map_user_region_in_directory(pml4, vaddr_aligned, size_aligned, flags) != 0)
            {
                serial_print("SERIAL: ELF: Failed to map user memory region\n");
                load_page_directory(old_cr3);  /* Restore original CR3 */
                return -1;
            }

            /* Now copy segment data to the mapped virtual addresses */
            /* We need to access the mapped virtual addresses */
            if (phdr[i].p_filesz > 0)
            {
                /* Copy from file to virtual address */
                memcpy((void *)vaddr, file_data + phdr[i].p_offset, phdr[i].p_filesz);
                serial_print("SERIAL: ELF: Copied ");
                serial_print_hex32((uint32_t)phdr[i].p_filesz);
                serial_print(" bytes from file to vaddr 0x");
                serial_print_hex32((uint32_t)vaddr);
                serial_print("\n");
            }

            /* Zero out BSS section if needed */
            if (phdr[i].p_memsz > phdr[i].p_filesz)
            {
                memset((void *)(vaddr + phdr[i].p_filesz), 0,
                       phdr[i].p_memsz - phdr[i].p_filesz);
                serial_print("SERIAL: ELF: Zeroed BSS section\n");
            }

            /* Store the mapping info for the process */
            if (i == 0)
            {
                process->memory_base = vaddr;
                process->memory_size = memsz;
            }
        }
    }

    /* Restore original page directory */
    load_page_directory(old_cr3);

    return 0;
}

/* Dummy entry function for ELF processes (will be overridden) */
static void elf_dummy_entry(void)
{
    /* This should never be called as we override RIP */
    while (1)
        ;
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
 * kexecve - Load and execute ELF binary (kernel-level exec)
 * @path: Path to ELF executable file
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
 * 5. Set up entry point, stack, and registers
 * 6. Add process to scheduler for execution
 *
 * Returns: 0 on success, -1 on error
 *
 * Thread safety: NOT thread-safe - should be called from process context
 */
int kexecve(const char *path)
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
    if (elf_load_segments(header, (uint8_t *)file_data, process) != 0)
    {
        serial_print("SERIAL: ELF: ERROR - Failed to load segments\n");
        kfree(file_data);
        return -1;
    }

    /* Step 5: Add process to scheduler */
    rr_add_process(process);
    serial_print("SERIAL: ELF: Process added to scheduler\n");

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