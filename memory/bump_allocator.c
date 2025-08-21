#include "bump_allocator.h"

extern char _end; // definido en el linker

#define HEAP_SIZE 0x100000 // 1MB para empezar

static uint8_t *heap_base = (uint8_t *)&_end;
static uint8_t *heap_end = (uint8_t *)&_end + HEAP_SIZE;
static uint8_t *heap_ptr = (uint8_t *)&_end;


void *kmalloc(size_t size)
{
    // alineamos a 16 bytes
    uintptr_t addr = (uintptr_t)heap_ptr;
    addr = (addr + 15) & ~(uintptr_t)15;

    if ((uint8_t *)(addr + size) > heap_end)
    {
        // sin memoria -> NULL o panic
        panic("Memory run out :-(");
    }

    void *result = (void *)addr;
    heap_ptr = (uint8_t *)(addr + size);
    return result;
}

void kfree(void *ptr)
{
    // en este heap inicial no liberamos memoria
    (void)ptr;
}

// Stubs para variables de memoria que otros archivos esperan
uint32_t free_pages_count = 1000;  // Valor fijo para el scheduler
uint32_t total_pages_count = 1024; // Valor fijo para el scheduler
