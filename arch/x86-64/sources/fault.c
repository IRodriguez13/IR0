#include <stdint.h>
#include <ir0/print.h>
#include <includes/ir0/memory/paging.h>
#include <includes/ir0/memory/kmem.h>
#include <panic/oops.h>


void page_fault_handler_x64(uint64_t *stack)
{
    uint64_t fault_addr;
    asm volatile("mov %%cr2, %0" : "=r"(fault_addr));

    uint64_t errcode = stack[1];

    int not_present = !(errcode & 1);
    int write = errcode & 2;
    int user = errcode & 4;

    if (user && not_present)
    {
        void *phys_page = kmalloc(0x1000);
        if (!phys_page)
        {
            panic("[PF] No hay memoria física para usuario");
        }

        uint64_t flags = PAGE_USER;
        if (write)
            flags |= PAGE_RW;

        map_user_page(fault_addr & ~0xFFF, (uintptr_t)phys_page, flags);

        return;
    }

    // Kernel fault o violación de permisos
    print("[PF] Kernel page fault en ");
    print_hex(fault_addr);
    print(" - código: ");
    print_hex(errcode);
    print("\n");
    panic("Unhandled kernel page fault");
}

// -------------------------------------------------------------------
// Double Fault
// -------------------------------------------------------------------
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

// -------------------------------------------------------------------
// Triple Fault
// -------------------------------------------------------------------
void triple_fault_x64()
{
    print_colored("TRIPLE FAULT!\n", 0x0C, 0x00);
    print("FATAL: CPU reset imminent\n");
    panic("Triple fault - System halted");
}

// -------------------------------------------------------------------
// General Protection Fault
// -------------------------------------------------------------------
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

// -------------------------------------------------------------------
// Invalid Opcode
// -------------------------------------------------------------------
void invalid_opcode_x64(uint64_t rip)
{
    print_colored("INVALID OPCODE!\n", 0x0C, 0x00);
    print("RIP: ");
    print_hex(rip);
    print("\n");
    panic("Invalid instruction - Kernel halted");
}

// -------------------------------------------------------------------
// Divide by Zero
// -------------------------------------------------------------------
void divide_by_zero_x64(uint64_t rip)
{
    print_colored("DIVIDE BY ZERO!\n", 0x0C, 0x00);
    print("RIP: ");
    print_hex(rip);
    print("\n");
    panic("Divide by zero - Kernel halted");
}
