// SPDX-License-Identifier: GPL-3.0-only
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * File: copy_user.c
 * Description: Safe copy functions with conditional validation
 *              Allows dbgshell (KERNEL_MODE) to use syscalls without validation
 *              while real userspace (USER_MODE) gets proper address checking
 */

#include "copy_user.h"
#include <kernel/process.h>
#include <mm/paging.h>
#include <string.h>
#include <config.h>
#include <drivers/serial/serial.h>

/* User space address range (simplified) */
#define USER_SPACE_START 0x00400000UL  /* 4MB */
#define USER_SPACE_END   0x00007FFFFFFFFFFFUL  /* Canonical userspace limit (128TB) */


/**
 * is_user_address - Check if address range is valid userspace address
 * @addr: Address to check
 * @size: Size of region
 * @check_mapped: If 1, verify pages are actually mapped (slower but safer)
 * Returns: 1 if valid user address, 0 otherwise
 */
int is_user_address_checked(const void *addr, size_t size, int check_mapped)
{
    uintptr_t start = (uintptr_t)addr;
    uintptr_t end = start + size;
    
    /* Check NULL */
    if (addr == NULL)
    {
        return 0;
    }
    
    /* Check overflow */
    if (end < start)
    {
        return 0;
    }
    
    /* Check range (canonical userspace: 0x0000000000000000 - 0x00007FFFFFFFFFFF) */
    if (start < USER_SPACE_START || end > USER_SPACE_END)
    {
        return 0;
    }
    
    /* If check_mapped is set, verify pages are actually mapped */
    if (check_mapped)
    {
        process_t *current = process_get_current();
        if (!current || !current->page_directory)
            return 0;  /* No process context or page directory */
        
        /* Check each page in the range */
        for (uintptr_t addr_check = start & ~0xFFF; addr_check < end; addr_check += 0x1000)
        {
            int mapped = is_page_mapped_in_directory(current->page_directory, addr_check, NULL);
            if (mapped != 1)
            {
                /* Page not mapped */
#if DEBUG_SYSCALLS
                serial_print("[COPY_USER] Address range contains unmapped pages\n");
#endif
                return 0;
            }
        }
    }
    
    return 1;
}

int is_user_address(const void *addr, size_t size)
{
    /* For now, only check range (faster)
     * Page faults will handle unmapped pages
     * In production, you might want to enable full checking
     */
    return is_user_address_checked(addr, size, 0);
}


/**
 * copy_to_user - Copy to user space with mode-aware validation
 */
int copy_to_user(void *dst, const void *src, size_t n)
{
    process_t *current = process_get_current();
    
    /* KERNEL_MODE bypass (dbgshell, embedded init)
     * No validation needed - we trust kernel code
     */
    if (current && current->mode == KERNEL_MODE)
    {
        memcpy(dst, src, n);
        return 0;
    }
    
    /* USER_MODE validation
     * Check destination is valid user address
     */
    if (!is_user_address(dst, n))
    {
#if DEBUG_SYSCALLS
        serial_print("[COPY_USER] Invalid destination address\n");
#endif
        return -EFAULT;
    }
    
    /* Safe to copy */
    memcpy(dst, src, n);
    return 0;
}

/*
 *
 * copy_from_user - Copy from user space with mode-aware validation
 */
int copy_from_user(void *dst, const void *src, size_t n)
{
    process_t *current = process_get_current();
    
    /* KERNEL_MODE bypass (dbgshell, embedded init) */
    if (current && current->mode == KERNEL_MODE)
    {
        memcpy(dst, src, n);
        return 0;
    }
    
    /* USER_MODE validation
     * Check source is valid user address
     */
    if (!is_user_address(src, n))
    {
#if DEBUG_SYSCALLS
        serial_print("[COPY_USER] Invalid source address\n");
#endif
        return -EFAULT;
    }
    
    /* Safe to copy */
    memcpy(dst, src, n);
    return 0;
}
