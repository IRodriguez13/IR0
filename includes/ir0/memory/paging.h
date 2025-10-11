// ===============================================================================
// IR0 KERNEL - SIMPLE X64 PAGING SYSTEM
// ===============================================================================

#pragma once

#include <stdint.h>
#include <stddef.h>

// ===============================================================================
// PAGE FLAGS
// ===============================================================================

#define PAGE_PRESENT 0x1
#define PAGE_RW 0x2
#define PAGE_USER 0x4
#define PAGE_WRITETHROUGH 0x8
#define PAGE_CACHE_DISABLE 0x10
#define PAGE_ACCESSED 0x20
#define PAGE_DIRTY 0x40
#define PAGE_SIZE_2MB_FLAG 0x80
#define PAGE_GLOBAL 0x100

// ===============================================================================
// PAGE SIZES
// ===============================================================================

#define PAGE_SIZE_4KB (4 * 1024)
#define PAGE_SIZE_2MB (2 * 1024 * 1024)
#define PAGE_SIZE_1GB (1024 * 1024 * 1024)

// ===============================================================================
// PAGING FUNCTIONS
// ===============================================================================

/**
 * Setup identity mapping for 16MB using 2MB pages
 * Maps physical addresses 0-16MB to virtual addresses 0-16MB
 */
void setup_paging_identity_16mb(void);

/**
 * Enable paging (sets CR0.PG bit)
 */
void enable_paging(void);

/**
 * Complete paging setup: configure tables and enable paging
 * This function handles all paging configuration from C code
 */
void setup_and_enable_paging(void);

/**
 * Load page directory into CR3
 */
void load_page_directory(uint64_t pml4_addr);

/**
 * Get current page directory address from CR3
 */
uint64_t get_current_page_directory(void);

/**
 * Check if paging is enabled
 */
int is_paging_enabled(void);

/**
 * Map a virtual address to a physical address
 * Note: This is a simple implementation for testing
 */
int map_page(uint64_t virt_addr, uint64_t phys_addr, uint64_t flags);

/**
 * Unmap a virtual address
 */
int unmap_page(uint64_t virt_addr);

// ===============================================================================
// USER MEMORY MAPPING FUNCTIONS
// ===============================================================================

// Mapear página de usuario con permisos U/S=1
int map_user_page(uintptr_t virtual_addr, uintptr_t physical_addr, uint64_t flags);

// Mapear región de memoria de usuario
int map_user_region(uintptr_t virtual_start, size_t size, uint64_t flags);

// ===============================================================================
// DEBUG FUNCTIONS
// ===============================================================================

/**
 * Print current paging status
 */
void print_paging_status(void);

/**
 * Dump page tables for debugging
 */
void dump_page_tables(void);

/**
 * Verify paging system integrity
 */
int verify_paging_integrity(void);

/**
 * Test page fault protection by accessing unmapped memory
 */
void test_page_fault_protection(void);

/**
 * Safe post-paging verification (can use print/log)
 * Only call AFTER paging is completely configured
 */
void verify_paging_setup_safe(void);
// ===============================================================================
// PROCESS PAGE DIRECTORY MANAGEMENT
// ===============================================================================

// Forward declaration for process_t
struct process;

/**
 * Create a new page directory for a user process
 * Returns physical address of the new PML4 table
 */
uint64_t create_process_page_directory(void);

/**
 * Destroy a process page directory
 * Frees the PML4 table and associated user pages
 */
void destroy_process_page_directory(uint64_t *pml4);

/**
 * Copy memory from parent process to child process
 * Used for fork() implementation
 */
int copy_process_memory(struct process *parent, struct process *child);