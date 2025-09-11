#include <stdint.h>
#include <ir0/print.h>
#include <bump_allocator.h>
#include <string.h>
#include "tss_x64.h"
#include <panic/panic.h>
// Declaraciones externas de tus funciones
extern int map_user_region(uintptr_t virtual_start, size_t size, uint64_t flags);
extern int map_user_page(uintptr_t virtual_addr, uintptr_t physical_addr, uint64_t flags);

// Definir los flags (si no están definidos ya)
#ifndef PAGE_PRESENT
#define PAGE_PRESENT 0x01
#endif
#ifndef PAGE_RW
#define PAGE_RW 0x02
#endif
#ifndef PAGE_USER
#define PAGE_USER 0x04
#endif
#define PAGE_SIZE 0x1000 // 4KB de page

// invalidar TLB para una direccion
static inline void invlpg_addr(uintptr_t addr)
{
    asm volatile("invlpg (%0)" : : "r"(addr) : "memory");
}

// Identity-map [start .. start+len) page-by-page with USER/RW/PRESENT.
// Retorna 0 ok, -1 error (y hace prints de debug).
int map_user_identity(uintptr_t start, size_t len)
{
    if (len == 0)
        return 0;
    uintptr_t a = start & ~(PAGE_SIZE - 1);
    uintptr_t end = (start + len + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);

    for (uintptr_t va = a; va < end; va += PAGE_SIZE)
    {
        // identity: physical == virtual (DEBE usarse solo en debug)
        int r = map_user_page(va, va, PAGE_PRESENT | PAGE_RW | PAGE_USER);
        if (r != 0)
        {
            print("[map_user_identity] map_user_page failed at 0x");
            print_hex64(va);
            print("\n");
            return -1;
        }
        invlpg_addr(va);
    }
    return 0;
}

// User mode transition function
void switch_to_user_mode(void *entry_point)
{
    print("Switching to user mode at 0x");
    print_hex64((uintptr_t)entry_point);
    print("\n");

    // USAR TUS FUNCIONES PARA MAPEAR MEMORIA DE USUARIO CORRECTAMENTE
    uintptr_t user_stack_base = 0x10000000; // 256MB - dirección alta pero segura
    size_t stack_size = 16384;              // 16KB stack

    // Stack crece hacia abajo - usar el final de la región
    uintptr_t stack_top = user_stack_base + stack_size - 16;
    stack_top &= ~0xF; // 16-byte align

    print("User stack top at 0x");
    print_hex64(stack_top);
    print("\n");
    delay_ms(7000);

    // identity-map el código del entry (debug)
    uintptr_t code_base = ((uintptr_t)entry_point) & ~0xFFF;
    if (map_user_identity(code_base, 0x1000) != 0)
    {
        panic("map_user_identity(code) failed\n");
    }

    // identity-map el stack de usuario
    if (map_user_identity(user_stack_base, stack_size) != 0)
    {
        panic("map_user_identity(stack) failed\n");
    }


    // Switch to user mode using assembly
    __asm__ volatile(
        "mov $0x23, %%ax\n" // User data segment
        "mov %%ax, %%ds\n"
        "mov %%ax, %%es\n"
        "mov %%ax, %%fs\n"
        "mov %%ax, %%gs\n"

        "pushq $0x23\n"  // SS
        "pushq %0\n"     // RSP
        "pushq $0x202\n" // RFLAGS
        "pushq $0x1B\n"  // CS
        "pushq %1\n"     // RIP
        "iretq\n"        // retorno con interrupcion para transicionar a user mode
        :
        : "r"(stack_top), "r"((uintptr_t)entry_point)
        : "memory");

    // No deberíamos llegar aquí
    panic("ERROR: Returned from user mode transition unexpectedly\n");
}

// System call handler - con más debug info
void syscall_handler_c(void)
{
    print("SUCCESS: System call received from USER MODE!\n");
    print("[OK] User mode transition worked correctly!\n");
    print(" Returning to kernel mode...\n");
}

// Test user function - más simple para evitar problemas
void test_user_function(void)
{
    // Este código se ejecuta en Ring 3 (user mode)

    // Contador simple para verificar que el código se ejecuta
    volatile int counter = 0;

    // Loop corto para no complicar
    for (int i = 0; i < 3; i++)
    {
        counter++;

        // Busy wait más corto
        for (volatile int j = 0; j < 10000; j++)
        {
            // Esperar
        }
    }

    // syscall para volver al kernel
    __asm__ volatile("int $0x80");
}