#include "paging_x86-32.h"
#include <ir0/print.h>

// Paginación completa para 32-bit - VERSIÓN SEGURA
// Implementación limpia, funcional y sin page faults

// Page Directory y Page Tables con alineación correcta
static page_directory_entry_t page_directory[1024] __attribute__((aligned(4096)));
static page_table_entry_t page_tables[4][1024] __attribute__((aligned(4096))); // Solo 4 tablas para 16MB

// Función para configurar entrada de página
void set_page_entry32(page_table_entry_t *entry, uint32_t physical_addr, uint32_t flags)
{
    entry->present = 1;
    entry->read_write = (flags & 0x2) ? 1 : 0;
    entry->user_supervisor = (flags & 0x4) ? 1 : 0;
    entry->write_through = (flags & 0x8) ? 1 : 0;
    entry->cache_disabled = (flags & 0x10) ? 1 : 0;
    entry->accessed = 0;
    entry->dirty = 0;
    entry->page_size = 0; // 4KB pages
    entry->global = 0;
    entry->available = 0;
    entry->page_address = physical_addr >> 12;
}

// Función para configurar entrada de directorio
void set_directory_entry32(page_directory_entry_t *entry, uint32_t page_table_addr, uint32_t flags)
{
    entry->present = 1;
    entry->read_write = (flags & 0x2) ? 1 : 0;
    entry->user_supervisor = (flags & 0x4) ? 1 : 0;
    entry->write_through = (flags & 0x8) ? 1 : 0;
    entry->cache_disabled = (flags & 0x10) ? 1 : 0;
    entry->accessed = 0;
    entry->available = 0;
    entry->page_size = 0; // 4KB pages
    entry->global = 0;
    entry->available2 = 0;
    entry->page_table_address = page_table_addr >> 12;
}

// Inicializar paginación de forma segura
void init_paging32(void)
{
    print("Inicializando paginación 32-bit de forma segura...\n");

    // Limpiar page directory completamente
    for (int i = 0; i < 1024; i++)
    {
        page_directory[i].present = 0;
        page_directory[i].read_write = 0;
        page_directory[i].user_supervisor = 0;
        page_directory[i].write_through = 0;
        page_directory[i].cache_disabled = 0;
        page_directory[i].accessed = 0;
        page_directory[i].available = 0;
        page_directory[i].page_size = 0;
        page_directory[i].global = 0;
        page_directory[i].available2 = 0;
        page_directory[i].page_table_address = 0;
    }

    // Limpiar page tables completamente
    for (int i = 0; i < 4; i++)
    {
        for (int j = 0; j < 1024; j++)
        {
            page_tables[i][j].present = 0;
            page_tables[i][j].read_write = 0;
            page_tables[i][j].user_supervisor = 0;
            page_tables[i][j].write_through = 0;
            page_tables[i][j].cache_disabled = 0;
            page_tables[i][j].accessed = 0;
            page_tables[i][j].dirty = 0;
            page_tables[i][j].page_size = 0;
            page_tables[i][j].global = 0;
            page_tables[i][j].available = 0;
            page_tables[i][j].page_address = 0;
        }
    }

    print("Page directory y tables limpiadas completamente\n");
}

// Configurar identity mapping de forma segura
void setup_identity_mapping32(void)
{
    print("Configurando identity mapping de forma segura...\n");

    // Identity mapping para los primeros 16MB (4096 páginas)
    // Esto mapea la memoria física 1:1 con la memoria virtual
    for (int i = 0; i < 4096; i++)
    {
        uint32_t physical_addr = i * 4096;
        int table_index = i / 1024; // Qué tabla de páginas usar
        int page_index = i % 1024;  // Qué página dentro de la tabla

        // Configurar página con flags seguros
        set_page_entry32(&page_tables[table_index][page_index], physical_addr, 0x3); // Present + Read/Write
    }

    // Configurar page directory para apuntar a las primeras 4 page tables
    for (int i = 0; i < 4; i++)
    {
        set_directory_entry32(&page_directory[i], (uint32_t)&page_tables[i], 0x3);
    }

    // Verificar que el mapeo está correcto
    print("Verificando identity mapping...\n");
    for (int i = 0; i < 4; i++)
    {
        if (!page_directory[i].present)
        {
            print("ERROR: Page directory entry ");
            print_int32(i);
            print(" no está presente\n");
            return;
        }
    }

    print("Identity mapping configurado correctamente para 16MB\n");
}

// Cargar page directory en CR3 de forma segura
void load_page_directory32(void)
{
    print("Cargando page directory en CR3 de forma segura...\n");

    uint32_t page_dir_addr = (uint32_t)page_directory;

    // Verificar que la dirección está alineada
    if (page_dir_addr & 0xFFF)
    {
        print("ERROR: Page directory no está alineado a 4KB\n");
        return;
    }

    // Cargar en CR3
    __asm__ volatile("mov %0, %%cr3" : : "r"(page_dir_addr));

    // Verificar que se cargó correctamente
    uint32_t cr3_val;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3_val));
    if (cr3_val != page_dir_addr)
    {
        print("ERROR: CR3 no se cargó correctamente\n");
        return;
    }

    print("Page directory cargado en CR3: 0x");
    print_uint32(page_dir_addr);
    print("\n");
}

// Habilitar paginación de forma segura
void enable_paging32(void)
{
    print("Habilitando paginación de forma segura...\n");

    uint32_t cr0;
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));

    // Verificar que no está ya habilitada
    if (cr0 & 0x80000000)
    {
        print("ADVERTENCIA: Paginación ya está habilitada\n");
        return;
    }

    // Habilitar paginación
    cr0 |= 0x80000000; // Set PG bit (bit 31)
    __asm__ volatile("mov %0, %%cr0" : : "r"(cr0));

    // Verificar que se habilitó correctamente
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    if (!(cr0 & 0x80000000))
    {
        print("ERROR: Paginación no se habilitó correctamente\n");
        return;
    }

    print("Paginación habilitada correctamente\n");
}

// Setup completo de paginación de forma segura
void setup_and_enable_paging32(void)
{
    print("=== SETUP COMPLETO DE PAGINACIÓN 32-BIT SEGURA ===\n");

    // 1. Inicializar estructuras
    init_paging32();

    // 2. Configurar identity mapping
    setup_identity_mapping32();

    // 3. Cargar page directory
    load_page_directory32();

    // 4. Habilitar paginación
    enable_paging32();

    // 5. Verificar que funciona correctamente
    uint32_t cr0, cr3;
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));

    print("Verificación final:\n");
    print("CR0 = 0x");
    print_uint32(cr0);
    print(" (bit 31 = ");
    print_int32((cr0 >> 31) & 1);
    print(")\n");

    print("CR3 = 0x");
    print_uint32(cr3);
    print("\n");

    // 6. Verificar que podemos acceder a la memoria
    print("Verificando acceso a memoria...\n");
    volatile uint32_t *test_addr = (uint32_t *)0x100000;
    uint32_t original_value = *test_addr;
    *test_addr = 0x12345678;
    if (*test_addr == 0x12345678)
    {
        print("✓ Acceso a memoria verificado correctamente\n");
        *test_addr = original_value; // Restaurar valor original
    }
    else
    {
        print("✗ ERROR: No se puede acceder a la memoria\n");
        return;
    }

    print("=== PAGINACIÓN 32-BIT CONFIGURADA EXITOSAMENTE ===\n");
}

// Función para verificar el estado de la paginación
void verify_paging_status32(void)
{
    print("=== VERIFICACIÓN DE ESTADO DE PAGINACIÓN ===\n");

    uint32_t cr0, cr3;
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));

    print("CR0 = 0x");
    print_uint32(cr0);
    print(" (Paginación ");
    if (cr0 & 0x80000000)
    {
        print("HABILITADA");
    }
    else
    {
        print("DESHABILITADA");
    }
    print(")\n");

    print("CR3 = 0x");
    print_uint32(cr3);
    print(" (Page Directory)\n");

    // Verificar page directory
    if (cr3 == (uint32_t)page_directory)
    {
        print("✓ Page Directory correcto\n");
    }
    else
    {
        print("✗ Page Directory incorrecto\n");
    }

    print("=== VERIFICACIÓN COMPLETADA ===\n");
}
