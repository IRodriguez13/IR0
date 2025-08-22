// ===============================================================================
// IR0 KERNEL PORTABLE ARCHITECTURE INTERFACE
// ===============================================================================
// This file provides a portable interface for all supported architectures
// All architecture-specific code should go through this interface

#pragma once

#include <stdint.h>
#include <stddef.h>
#include "arch_config.h"

// ===============================================================================
// PORTABLE TYPES
// ===============================================================================

typedef uintptr_t arch_addr_t;
typedef uintptr_t arch_size_t;
typedef uint32_t arch_irq_t;
typedef uint32_t arch_flags_t;

// ===============================================================================
// MEMORY MANAGEMENT INTERFACE
// ===============================================================================

/**
 * Initialize architecture-specific memory management
 */
void arch_memory_init(void);

/**
 * Get the start address of available physical memory
 */
arch_addr_t arch_get_memory_start(void);

/**
 * Get the end address of available physical memory
 */
arch_addr_t arch_get_memory_end(void);

/**
 * Allocate a physical page
 */
arch_addr_t arch_alloc_page(void);

/**
 * Free a physical page
 */
void arch_free_page(arch_addr_t addr);

/**
 * Map virtual address to physical address
 */
int arch_map_page(arch_addr_t virt, arch_addr_t phys, arch_flags_t flags);

/**
 * Unmap a virtual page
 */
int arch_unmap_page(arch_addr_t virt);

/**
 * Get page size for current architecture
 */
size_t arch_get_page_size(void);

// ===============================================================================
// INTERRUPT INTERFACE
// ===============================================================================

/**
 * Initialize architecture-specific interrupt system
 */
void arch_interrupt_init(void);

/**
 * Enable interrupts globally
 */
void arch_enable_interrupts(void);

/**
 * Disable interrupts globally
 */
void arch_disable_interrupts(void);

/**
 * Register an interrupt handler
 */
int arch_register_irq(arch_irq_t irq, void (*handler)(void));

/**
 * Unregister an interrupt handler
 */
int arch_unregister_irq(arch_irq_t irq);

/**
 * Send end-of-interrupt signal
 */
void arch_eoi(arch_irq_t irq);

// ===============================================================================
// I/O INTERFACE
// ===============================================================================

/**
 * Read from I/O port (x86) or MMIO address (ARM)
 */
uint8_t arch_io_read8(arch_addr_t addr);

/**
 * Write to I/O port (x86) or MMIO address (ARM)
 */
void arch_io_write8(arch_addr_t addr, uint8_t value);

/**
 * Read 16-bit from I/O port or MMIO address
 */
uint16_t arch_io_read16(arch_addr_t addr);

/**
 * Write 16-bit to I/O port or MMIO address
 */
void arch_io_write16(arch_addr_t addr, uint16_t value);

/**
 * Read 32-bit from I/O port or MMIO address
 */
uint32_t arch_io_read32(arch_addr_t addr);

/**
 * Write 32-bit to I/O port or MMIO address
 */
void arch_io_write32(arch_addr_t addr, uint32_t value);

// ===============================================================================
// CPU CONTROL INTERFACE
// ===============================================================================

/**
 * Halt the CPU (low power mode)
 */
void arch_cpu_halt(void);

/**
 * Get CPU ID
 */
uint32_t arch_get_cpu_id(void);

/**
 * Get number of CPUs
 */
uint32_t arch_get_cpu_count(void);

/**
 * Switch to user mode (if supported)
 */
void arch_switch_to_user(arch_addr_t entry, arch_addr_t stack);

/**
 * Get current CPU mode
 */
uint32_t arch_get_cpu_mode(void);

// ===============================================================================
// TIMER INTERFACE
// ===============================================================================

/**
 * Initialize architecture-specific timer
 */
void arch_timer_init(void);

/**
 * Get current timer value
 */
uint64_t arch_timer_read(void);

/**
 * Set timer frequency
 */
void arch_timer_set_frequency(uint32_t hz);

/**
 * Get timer frequency
 */
uint32_t arch_timer_get_frequency(void);

// ===============================================================================
// DEBUGGING INTERFACE
// ===============================================================================

/**
 * Get fault address (for page faults, etc.)
 */
arch_addr_t arch_get_fault_address(void);

/**
 * Get fault type
 */
uint32_t arch_get_fault_type(void);

/**
 * Get fault error code
 */
uint32_t arch_get_fault_error(void);

/**
 * Dump CPU registers
 */
void arch_dump_registers(void);

// ===============================================================================
// BOOT INTERFACE
// ===============================================================================

/**
 * Early architecture initialization
 */
void arch_early_init(void);

/**
 * Late architecture initialization
 */
void arch_late_init(void);

/**
 * Get boot parameters
 */
void *arch_get_boot_params(void);

/**
 * Get command line arguments
 */
const char *arch_get_cmdline(void);

// ===============================================================================
// UTILITY FUNCTIONS
// ===============================================================================

/**
 * Get architecture name
 */
const char *arch_get_name(void);

/**
 * Get architecture bits (32/64)
 */
uint32_t arch_get_bits(void);

/**
 * Check if architecture supports specific feature
 */
int arch_supports_feature(const char *feature);

/**
 * Get architecture-specific compiler flags
 */
const char *arch_get_cflags(void);

/**
 * Get architecture-specific linker flags
 */
const char *arch_get_ldflags(void);
