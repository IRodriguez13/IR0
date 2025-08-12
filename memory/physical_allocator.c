#include "physical_allocator.h"
#include "memo_interface.h"
#include <string.h>
#include <print.h>

// Configuración del allocator
#define PHYS_MEM_START 0x100000 // 1MB (después del kernel)
#define PHYS_MEM_END 0x8000000  // 128MB por ahora
#define BITMAP_SIZE ((PHYS_MEM_END - PHYS_MEM_START) / PAGE_SIZE / 8)

// Bitmap para trackear páginas libres (1 bit por página)
static uint8_t page_bitmap[BITMAP_SIZE];
static int allocator_initialized = 0;

// Variables globales para panic.c (ahora tienen implementación)
uint32_t free_pages_count = 0;
uint32_t total_pages_count = 0;

// Funciones helper para el bitmap
static inline void set_page_used(uintptr_t phys_addr)
{
    uint32_t page_idx = (phys_addr - PHYS_MEM_START) / PAGE_SIZE;
    uint32_t byte_idx = page_idx / 8;
    uint32_t bit_idx = page_idx % 8;

    if (byte_idx < BITMAP_SIZE)
    {
        page_bitmap[byte_idx] |= (1 << bit_idx);
    }
}

static inline void set_page_free(uintptr_t phys_addr)
{
    uint32_t page_idx = (phys_addr - PHYS_MEM_START) / PAGE_SIZE;
    uint32_t byte_idx = page_idx / 8;
    uint32_t bit_idx = page_idx % 8;

    if (byte_idx < BITMAP_SIZE)
    {
        page_bitmap[byte_idx] &= ~(1 << bit_idx);
    }
}

static inline int is_page_used(uintptr_t phys_addr)
{
    uint32_t page_idx = (phys_addr - PHYS_MEM_START) / PAGE_SIZE;
    uint32_t byte_idx = page_idx / 8;
    uint32_t bit_idx = page_idx % 8;

    if (byte_idx >= BITMAP_SIZE)
    {
        return 1; // Fuera de rango = usado
    }

    return (page_bitmap[byte_idx] & (1 << bit_idx)) != 0;
}

void physical_allocator_init(void)
{
    if (allocator_initialized)
    {
        return;
    }

    // Limpiar el bitmap (todas las páginas libres)
    memset(page_bitmap, 0, BITMAP_SIZE);

    // Calcular estadísticas
    total_pages_count = (PHYS_MEM_END - PHYS_MEM_START) / PAGE_SIZE;
    free_pages_count = total_pages_count;

    // Marcar las primeras páginas como usadas (kernel, stack, etc.)
    // Reservamos los primeros 4MB para el kernel
    for (uintptr_t addr = PHYS_MEM_START; addr < PHYS_MEM_START + 0x400000; addr += PAGE_SIZE)
    {
        set_page_used(addr);
        free_pages_count--;
    }

    allocator_initialized = 1;

    LOG_OK("Physical allocator inicializado");
    print("Memoria física: ");
    print_hex_compact(PHYS_MEM_START);
    print(" - ");
    print_hex_compact(PHYS_MEM_END);
    print(" (");
    print_hex_compact(total_pages_count);
    print(" páginas)\n");
}

uintptr_t alloc_physical_page(void)
{
    if (!allocator_initialized)
    {
        physical_allocator_init();
    }

    if (free_pages_count == 0)
    {
        LOG_ERR("alloc_physical_page: Sin memoria física!");
        return 0;
    }

    // Buscar primera página libre en el bitmap
    for (uint32_t byte_idx = 0; byte_idx < BITMAP_SIZE; byte_idx++)
    {
        if (page_bitmap[byte_idx] != 0xFF)
        { // Hay al menos un bit libre que no sea 0
            for (int bit_idx = 0; bit_idx < 8; bit_idx++)
            {
                if (!(page_bitmap[byte_idx] & (1 << bit_idx)))
                {
                    // Encontramos página libre
                    uint32_t page_idx = byte_idx * 8 + bit_idx;
                    uintptr_t phys_addr = PHYS_MEM_START + (page_idx * PAGE_SIZE);

                    set_page_used(phys_addr);
                    free_pages_count--; // Si marco la página como usada, entonces hay menos páginas libres.

                    // Limpiar la página antes de retornarla
                    memset((void *)phys_addr, 0, PAGE_SIZE);

                    return phys_addr;
                }
            }
        }
    }

    LOG_ERR("alloc_physical_page: No se encontró página libre!");
    return 0;
}

void free_physical_page(uintptr_t phys_addr)
{
    if (!allocator_initialized) // No puedo liberar nada si no se reservó memoria antes.
    {
        LOG_ERR("free_physical_page: Allocator no inicializado");
        return;
    }

    // Validar rango para no acceder a memoria inexistente.
    if (phys_addr < PHYS_MEM_START || phys_addr >= PHYS_MEM_END)
    {
        LOG_ERR("free_physical_page: Dirección fuera de rango");
        return;
    }

    if (!IS_PAGE_ALIGNED(phys_addr))
    {
        LOG_ERR("free_physical_page: Dirección no alineada");
        return;
    }

    // Verificar que la página esté marcada como usada
    if (!is_page_used(phys_addr))
    {
        LOG_WARN("free_physical_page: Intentando liberar página ya libre");
        return;
    }

    set_page_free(phys_addr);
    free_pages_count++;
}

void debug_physical_allocator(void)
{
    print_colored("=== PHYSICAL ALLOCATOR STATE ===\n", VGA_COLOR_CYAN, VGA_COLOR_BLACK);

    print("Memory range: ");
    print_hex_compact(PHYS_MEM_START);
    print(" - ");
    print_hex_compact(PHYS_MEM_END);
    print("\n");

    print("Total pages: ");
    print_hex_compact(total_pages_count);
    print("\n");

    print("Free pages: ");
    print_hex_compact(free_pages_count);
    print("\n");

    print("Used pages: ");
    print_hex_compact(total_pages_count - free_pages_count);
    print("\n");

    uint32_t usage_percent = ((total_pages_count - free_pages_count) * 100) / total_pages_count;
    print("Usage: ");
    print_hex_compact(usage_percent);
    print("%\n\n");
}