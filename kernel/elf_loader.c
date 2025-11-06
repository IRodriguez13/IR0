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
#include "process.h"

/* Compiler optimization hints */
#define likely(x)	__builtin_expect(!!(x), 1)
#define unlikely(x)	__builtin_expect(!!(x), 0)

/* External functions */
extern void serial_print(const char *str);
extern void serial_print_hex32(uint32_t num);
extern void *kmalloc(size_t size);
extern void kfree(void *ptr);

// Use external strstr from string.h

// Minimal ELF structures
typedef struct {
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

typedef struct {
    uint32_t p_type;
    uint32_t p_flags;
    uint64_t p_offset;
    uint64_t p_vaddr;
    uint64_t p_paddr;
    uint64_t p_filesz;
    uint64_t p_memsz;
    uint64_t p_align;
} elf64_phdr_t;

// ELF constants
#define ELF_MAGIC_0 0x7f
#define ELF_MAGIC_1 'E'
#define ELF_MAGIC_2 'L'
#define ELF_MAGIC_3 'F'
#define ELFCLASS64  2
#define ET_EXEC     2
#define PT_LOAD     1
#define EM_X86_64   0x3e
#define ET_EXEC     2
#define PT_LOAD     1

static int validate_elf_header(const elf64_header_t *header) {
    // Check ELF magic number
    if (header->e_ident[0] != ELF_MAGIC_0 ||
        header->e_ident[1] != ELF_MAGIC_1 ||
        header->e_ident[2] != ELF_MAGIC_2 ||
        header->e_ident[3] != ELF_MAGIC_3) {
        return 0;
    }
    
    // Check 64-bit and x86-64 architecture
    if (header->e_ident[4] != ELFCLASS64 || header->e_machine != EM_X86_64) {
        return 0;
    }
    
    // Check executable type
    if (header->e_type != ET_EXEC) {
        return 0;
    }
    
    return 1;
}

// Load ELF segments into memory at correct virtual addresses
static int elf_load_segments(elf64_header_t *header, uint8_t *file_data, process_t *process) {
    elf64_phdr_t *phdr = (elf64_phdr_t *)(file_data + header->e_phoff);
    
    serial_print("SERIAL: ELF: Loading ");
    serial_print_hex32(header->e_phnum);
    serial_print(" program segments\n");
    
    for (int i = 0; i < header->e_phnum; i++) {
        if (phdr[i].p_type == PT_LOAD) {
            serial_print("SERIAL: ELF: Loading segment ");
            serial_print_hex32(i);
            serial_print(" at vaddr 0x");
            serial_print_hex32((uint32_t)phdr[i].p_vaddr);
            serial_print(" size 0x");
            serial_print_hex32((uint32_t)phdr[i].p_memsz);
            serial_print("\n");
            
            // Map virtual address to physical memory
            uint64_t memsz = phdr[i].p_memsz;
            
            // Allocate physical memory for the segment
            void *phys_mem = kmalloc(memsz);
            if (!phys_mem) {
                serial_print("SERIAL: ELF: Failed to allocate memory for segment\n");
                return -1;
            }
            
            // Copy segment data from file
            if (phdr[i].p_filesz > 0) {
                memcpy(phys_mem, file_data + phdr[i].p_offset, phdr[i].p_filesz);
                serial_print("SERIAL: ELF: Copied ");
                serial_print_hex32((uint32_t)phdr[i].p_filesz);
                serial_print(" bytes from file\n");
            }
            
            // Zero out BSS section if needed
            if (phdr[i].p_memsz > phdr[i].p_filesz) {
                memset((uint8_t *)phys_mem + phdr[i].p_filesz, 0, 
                       phdr[i].p_memsz - phdr[i].p_filesz);
                serial_print("SERIAL: ELF: Zeroed BSS section\n");
            }
            

            serial_print("SERIAL: ELF: Segment loaded at physical 0x");
            serial_print_hex32((uint32_t)(uintptr_t)phys_mem);
            serial_print("\n");
            
            // Store the mapping for the process (simplified)
            if (i == 0) {
                process->memory_base = (uintptr_t)phys_mem;
                process->memory_size = memsz;
            }
        }
    }
    
    return 0;
}

// Dummy entry function for ELF processes (will be overridden)
static void elf_dummy_entry(void) {
    // This should never be called as we override RIP
    while(1);
}

// Create a new process for the ELF program
static process_t *elf_create_process(elf64_header_t *header, const char *path) {
    extern process_t *process_create(void (*entry)(void));
    
    serial_print("SERIAL: ELF: Creating process for ");
    serial_print(path);
    serial_print(" with entry point 0x");
    serial_print_hex32((uint32_t)header->e_entry);
    serial_print("\n");
    
    // Create user process with dummy entry (we'll override RIP)
    process_t *process = process_create(elf_dummy_entry);
    if (!process) {
        serial_print("SERIAL: ELF: Failed to create process\n");
        return NULL;
    }
    
    // Set up user mode execution

    process->task.rip = process->memory_base + (header->e_entry - 0x400000);
    process->task.cs = 0x1B;  // User code segment (GDT entry 3, RPL=3)
    process->task.ss = 0x23;  // User data segment (GDT entry 4, RPL=3)
    process->task.ds = 0x23;  // User data segment
    process->task.es = 0x23;  // User data segment
    
    // Set up user stack in a safe area (8MB mark)
    process->task.rsp = 0x800000 - 0x1000;
    process->task.rbp = process->task.rsp;
    
    // Enable interrupts in user mode
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

// Main ELF loader function
int elf_load_and_execute(const char *path) {
    serial_print("SERIAL: ELF: ========================================\n");
    serial_print("SERIAL: ELF: Loading ELF file: ");
    serial_print(path);
    serial_print("\n");
    
    // Step 1: Read the ELF file from filesystem
    extern int vfs_read_file(const char *path, void **data, size_t *size);
    void *file_data = NULL;
    size_t file_size = 0;
    
    int result = vfs_read_file(path, &file_data, &file_size);
    if (result != 0 || !file_data) {
        serial_print("SERIAL: ELF: ERROR - Failed to read file from filesystem\n");
        return -1;
    }
    
    serial_print("SERIAL: ELF: File loaded successfully, size: ");
    serial_print_hex32(file_size);
    serial_print(" bytes\n");
    
    // Step 2: Validate ELF header
    if (!validate_elf_header((elf64_header_t *)file_data)) {
        serial_print("SERIAL: ELF: ERROR - Invalid ELF header\n");
        kfree(file_data);
        return -1;
    }
    
    elf64_header_t *header = (elf64_header_t *)file_data;
    serial_print("SERIAL: ELF: Header validation passed\n");
    
    // Step 3: Create process first
    process_t *process = elf_create_process(header, path);
    if (!process) {
        serial_print("SERIAL: ELF: ERROR - Failed to create process\n");
        kfree(file_data);
        return -1;
    }
    
    // Step 4: Load segments into memory
    if (elf_load_segments(header, (uint8_t *)file_data, process) != 0) {
        serial_print("SERIAL: ELF: ERROR - Failed to load segments\n");
        kfree(file_data);
        return -1;
    }
    
    // Step 5: Add process to scheduler
    extern void rr_add_process(process_t *proc);
    rr_add_process(process);
    serial_print("SERIAL: ELF: Process added to scheduler\n");
    
    // Step 6: Clean up file data
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