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

// External functions
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

// Simple ELF validation (for future use)
static int validate_elf_header(const elf64_header_t *header) {
    serial_print("SERIAL: ELF: Validating header\n");
    
    // Check magic
    if (header->e_ident[0] != ELF_MAGIC_0 ||
        header->e_ident[1] != ELF_MAGIC_1 ||
        header->e_ident[2] != ELF_MAGIC_2 ||
        header->e_ident[3] != ELF_MAGIC_3) {
        serial_print("SERIAL: ELF: Invalid magic number\n");
        return 0;
    }
    
    // Check 64-bit
    if (header->e_ident[4] != ELFCLASS64) {
        serial_print("SERIAL: ELF: Not 64-bit ELF\n");
        return 0;
    }
    
    // Check x86-64
    if (header->e_machine != EM_X86_64) {
        serial_print("SERIAL: ELF: Not x86-64\n");
        return 0;
    }
    
    // Check executable
    if (header->e_type != ET_EXEC) {
        serial_print("SERIAL: ELF: Not executable\n");
        return 0;
    }
    
    serial_print("SERIAL: ELF: Header validation passed\n");
    return 1;
}

// Future: Real ELF loader would go here when needed

// Load ELF segments into memory
static int elf_load_segments(elf64_header_t *header, uint8_t *file_data) {
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
            serial_print("\n");
            
            // Allocate memory for the segment
            void *segment_mem = kmalloc(phdr[i].p_memsz);
            if (!segment_mem) {
                serial_print("SERIAL: ELF: Failed to allocate memory for segment\n");
                return -1;
            }
            
            // Copy segment data from file
            if (phdr[i].p_filesz > 0) {
                memcpy(segment_mem, file_data + phdr[i].p_offset, phdr[i].p_filesz);
            }
            
            // Zero out BSS section if needed
            if (phdr[i].p_memsz > phdr[i].p_filesz) {
                memset((uint8_t *)segment_mem + phdr[i].p_filesz, 0, 
                       phdr[i].p_memsz - phdr[i].p_filesz);
            }
            
            serial_print("SERIAL: ELF: Segment loaded successfully\n");
        }
    }
    
    return 0;
}

// Create a new process for the ELF program
static int elf_create_process(elf64_header_t *header, const char *path) {
    extern int process_create_user(const char *name, uint64_t entry_point);
    
    serial_print("SERIAL: ELF: Creating process for ");
    serial_print(path);
    serial_print(" with entry point 0x");
    serial_print_hex32((uint32_t)header->e_entry);
    serial_print("\n");
    
    // Create user process
    int pid = process_create_user(path, header->e_entry);
    if (pid < 0) {
        serial_print("SERIAL: ELF: Failed to create process\n");
        return -1;
    }
    
    serial_print("SERIAL: ELF: Process created with PID ");
    serial_print_hex32(pid);
    serial_print("\n");
    
    return pid;
}

// Main ELF loader function
int elf_load_and_execute(const char *path) {
    serial_print("SERIAL: ELF: Loading ELF file: ");
    serial_print(path);
    serial_print("\n");
    
    // Step 1: Read the ELF file from filesystem
    extern int vfs_read_file(const char *path, void **data, size_t *size);
    void *file_data = NULL;
    size_t file_size = 0;
    
    int result = vfs_read_file(path, &file_data, &file_size);
    if (result != 0 || !file_data) {
        serial_print("SERIAL: ELF: Failed to read file from filesystem\n");
        return -1;
    }
    
    serial_print("SERIAL: ELF: File loaded, size: ");
    serial_print_hex32(file_size);
    serial_print(" bytes\n");
    
    // Step 2: Validate ELF header
    if (!validate_elf_header((elf64_header_t *)file_data)) {
        serial_print("SERIAL: ELF: Invalid ELF header\n");
        kfree(file_data);
        return -1;
    }
    
    elf64_header_t *header = (elf64_header_t *)file_data;
    
    // Step 3: Load segments into memory
    if (elf_load_segments(header, (uint8_t *)file_data) != 0) {
        serial_print("SERIAL: ELF: Failed to load segments\n");
        kfree(file_data);
        return -1;
    }
    
    // Step 4: Create process and set up execution
    int pid = elf_create_process(header, path);
    if (pid < 0) {
        serial_print("SERIAL: ELF: Failed to create process\n");
        kfree(file_data);
        return -1;
    }
    
    // Step 5: Clean up file data
    kfree(file_data);
    
    serial_print("SERIAL: ELF: Program loaded and ready for execution\n");
    return pid;
}