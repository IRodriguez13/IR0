#include <stdint.h>
#include <ir0/vga.h>
#include <mm/paging.h>
#include <ir0/kmem.h>
#include <ir0/oops.h>
#include <kernel/process.h>
#include <string.h>
#include <mm/pmm.h>
#include <ir0/signals.h>
#include <mm/pmm.h>


void page_fault_handler_x64(uint64_t *stack)
{
    uint64_t fault_addr;
    asm volatile("mov %%cr2, %0" : "=r"(fault_addr));

    uint64_t errcode = stack[1];

    int not_present = !(errcode & 1);  /* Page not present */
    int write = errcode & 2;            /* Write access */
    int user = errcode & 4;             /* User mode access */
    int reserved = errcode & 8;         /* Reserved bit set */
    int instruction_fetch = errcode & 16; /* Instruction fetch */

    /* Validate fault address is in userspace range (only for user mode faults) */
    const uint64_t USER_SPACE_START = 0x00400000UL;  /* 4MB */
    const uint64_t USER_SPACE_END = 0x00007FFFFFFFFFFFUL;  /* Canonical userspace limit */
    
    if (user && not_present)
    {
        /* Validate fault address is in valid userspace range */
        if (fault_addr < USER_SPACE_START || fault_addr > USER_SPACE_END)
        {
            /* Invalid userspace address - send SIGSEGV to process */
            process_t *current = process_get_current();
            if (current)
            {
                send_signal(current->task.pid, 11);  /* SIGSEGV */
                return;
            }
            panic("[PF] Invalid userspace address");
        }

        /* Get current process page directory */
        process_t *current = process_get_current();
        if (!current || !current->page_directory)
        {
            panic("[PF] No process context for user page fault");
        }

        /* Allocate physical frame using PMM (correct!) */
        uintptr_t phys_addr = pmm_alloc_frame();
        if (phys_addr == 0)
        {
            /* Out of memory - send SIGSEGV to process */
            if (current)
            {
                send_signal(current->task.pid, 11);  /* SIGSEGV */
                return;
            }
            panic("[PF] No hay memoria física para usuario");
        }

        /* Determine page flags */
        uint64_t flags = PAGE_USER;  /* Always user mode */
        if (write)
            flags |= PAGE_RW;  /* Make writable if write fault */

        /* Map page in process page directory */
        uint64_t vaddr_aligned = fault_addr & ~0xFFF;
        if (map_page_in_directory(current->page_directory, vaddr_aligned, phys_addr, flags) != 0)
        {
            /* Mapping failed - free frame and send SIGSEGV */
            pmm_free_frame(phys_addr);
            if (current)
            {
                send_signal(current->task.pid, 11);  /* SIGSEGV */
                return;
            }
            panic("[PF] Failed to map user page");
        }

        /* Zero out the newly allocated page
         * We must switch to process page directory to write to userspace
         */
        uint64_t old_cr3 = get_current_page_directory();
        load_page_directory((uint64_t)current->page_directory);
        memset((void *)vaddr_aligned, 0, 0x1000);
        load_page_directory(old_cr3);  /* Restore kernel CR3 */

        return;
    }

    /* Handle write protection fault (page present but not writable) */
    if (user && !not_present && write)
    {
        /* Page exists but not writable - check if we should make it writable */
        /* For now, send SIGSEGV (page should have been mapped with correct flags) */
        process_t *current = process_get_current();
        if (current)
        {
            send_signal(current->task.pid, 11);  /* SIGSEGV */
            return;
        }
    }

    /* Kernel fault, reserved bit set, or other error - fatal */
    print("[PF] Kernel page fault en ");
    print_hex(fault_addr);
    print(" - código: ");
    print_hex(errcode);
    print(" not_present=");
    print_hex(not_present);
    print(" write=");
    print_hex(write);
    print(" user=");
    print_hex(user);
    print("\n");
    panic("Unhandled kernel page fault");
}

// Double Fault
void double_fault_x64(uint64_t error_code, uint64_t rip)
{
    print_colored("DOUBLE FAULT!\n", 0x0C, 0x00);
    print("Error code: ");
    print_hex(error_code);
    print("\n");
    print("RIP: ");
    print_hex(rip);
    print("\n");
    panic("Double fault - Kernel halted");
}

// Triple Fault
void triple_fault_x64()
{
    print_colored("TRIPLE FAULT!\n", 0x0C, 0x00);
    print("FATAL: CPU reset imminent\n");
    panic("Triple fault - System halted");
}

void general_protection_fault_x64(uint64_t error_code, uint64_t rip, uint64_t cs, uint64_t rsp)
{
    print_colored("GENERAL PROTECTION FAULT!\n", 0x0C, 0x00);
    print("Error code: ");
    print_hex(error_code);
    print("\n");
    print("RIP: ");
    print_hex(rip);
    print("\n");
    print("CS: ");
    print_hex(cs);
    print("\n");
    print("RSP: ");
    print_hex(rsp);
    print("\n");
    panic("GPF - Kernel halted");
}

void invalid_opcode_x64(uint64_t rip)
{
    print_colored("INVALID OPCODE!\n", 0x0C, 0x00);
    print("RIP: ");
    print_hex(rip);
    print("\n");
    panic("Invalid instruction - Kernel halted");
}

void divide_by_zero_x64(uint64_t rip)
{
    print_colored("DIVIDE BY ZERO!\n", 0x0C, 0x00);
    print("RIP: ");
    print_hex(rip);
    print("\n");
    panic("Divide by zero - Kernel halted");
}
