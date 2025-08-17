// memory/ondemand_paging.c - Implementación de paginación on-demand
#include "ondemand-paging.h"
#include "memo_interface.h"
#include "physical_allocator.h"
#include "../arch/common/arch_interface.h"
#include <print.h>
#include <panic/panic.h>
#include <string.h>

// Estructura para trackear regiones de memoria virtual
typedef struct vm_area
{
    uintptr_t start;
    uintptr_t end;
    uint32_t flags;
    struct vm_area *next;
} vm_area_t;

// Lista global de áreas de memoria virtual.
static vm_area_t *vm_areas = NULL;
static int ondemand_initialized = 0;

// Estadísticas
static uint32_t on_demand_faults = 0;
static uint32_t pages_allocated_on_demand = 0;

// ===============================================================================
// INICIALIZACIÓN DEL SISTEMA ON-DEMAND
// ===============================================================================

void ondemand_paging_init(void)
{
    if (ondemand_initialized)
    {
        return;
    }

    LOG_OK("Inicializando paginación on-demand");

    // Registrar toda la memoria mapeada estáticamente (0-256MB)
    // Esto permite que el on-demand paging maneje page faults en esta región
    vm_area_register(0x00000000, 0x10000000, // 0-256MB
                     PAGE_FLAG_PRESENT | PAGE_FLAG_WRITABLE);

    // Reservar área para heap del kernel (ejemplo: 16MB starting at 0x10000000)
    vm_area_register(0x10000000, 0x11000000,
                     PAGE_FLAG_PRESENT | PAGE_FLAG_WRITABLE);

    // Reservar área para stack de kernel (ejemplo: 1MB starting at 0x20000000)
    vm_area_register(0x20000000, 0x20100000,
                     PAGE_FLAG_PRESENT | PAGE_FLAG_WRITABLE);

    // Registrar área de memoria superior para x86-64
    vm_area_register(0x8000000000000000, 0x8000000000000000 + 0x10000000,
                     PAGE_FLAG_PRESENT | PAGE_FLAG_WRITABLE);

    ondemand_initialized = 1;
    LOG_OK("Paginación on-demand inicializada (cubre 0-256MB + áreas especiales + memoria superior)");
}

// ===============================================================================
// MANEJO DE ÁREAS DE MEMORIA VIRTUAL
// ===============================================================================

int vm_area_register(uintptr_t start, uintptr_t end, uint32_t flags)
{
    // Validaciones
    if (!IS_PAGE_ALIGNED(start) || !IS_PAGE_ALIGNED(end))
    {
        LOG_ERR("vm_area_register: Direcciones no alineadas");
        return -1;
    }

    if (start >= end)
    {
        LOG_ERR("vm_area_register: Rango inválido");
        return -2;
    }

    // Crear nueva área
    vm_area_t *new_area = (vm_area_t *)kmalloc(sizeof(vm_area_t));
    if (!new_area)
    {
        LOG_ERR("vm_area_register: No memory for vm_area");
        return -3;
    }

    new_area->start = start;
    new_area->end = end;
    new_area->flags = flags;
    new_area->next = vm_areas;
    vm_areas = new_area;

    LOG_OK("Área VM registrada: 0x%08X - 0x%08X");
    print_hex64(start);
    print_hex64(end);
    return 0;
}

vm_area_t *find_vm_area(uintptr_t virt_addr)
{
    vm_area_t *current = vm_areas;

    while (current)
    {
        if (virt_addr >= current->start && virt_addr < current->end)
        {
            return current;
        }
        current = current->next;
    }

    return NULL;
}

// ===============================================================================
// PAGE FAULT HANDLER MEJORADO
// ===============================================================================

int handle_page_fault_ondemand(uintptr_t fault_addr, uint32_t error_code)
{
    on_demand_faults++;

    LOG_OK("Page fault on-demand: 0x%08X (error: 0x%X)");
    print_hex64(fault_addr);
    print_hex64(error_code);

    // 1. Encontrar el área VM que contiene esta dirección
    vm_area_t *vm_area = find_vm_area(fault_addr);
    if (!vm_area)
    {
        LOG_ERR("Page fault outside registered VM areas");
        return -1; // Fault inválido - debería hacer panic
    }

    // 2. Verificar permisos basado en el error code
    if (!validate_page_fault_permissions(vm_area, error_code))
    {
        LOG_ERR("Page fault: Permission violation");
        return -2; // Violación de permisos
    }

    // 3. Alinear dirección a página
    uintptr_t page_addr = PAGE_ALIGN_DOWN(fault_addr);

    // 4. Verificar si ya existe mapping (double fault)
    uintptr_t existing_phys = arch_virt_to_phys(page_addr);
    if (existing_phys != 0)
    {
        LOG_WARN("Page fault en página ya mapeada - possible TLB issue");
        arch_invalidate_page(page_addr);
        return 0; // Ya estaba mapeada, probablemente TLB stale
    }

    // 5. Allocar página física
    uintptr_t phys_page = alloc_physical_page();
    if (phys_page == 0)
    {
        LOG_ERR("Page fault: Out of physical memory!");
        return -3;
    }

    // 6. Limpiar página (importante para seguridad)
    memset((void *)phys_page, 0, PAGE_SIZE);

    // 7. Mapear página usando la función mejorada
    int result = arch_map_page(page_addr, phys_page, vm_area->flags);
    if (result != 0)
    {
        LOG_ERR("Page fault: Failed to map page");
        free_physical_page(phys_page);
        return -4;
    }

    pages_allocated_on_demand++;
    LOG_OK("Page allocated on-demand");

    return 0; // Éxito
}

int validate_page_fault_permissions(vm_area_t *vm_area, uint32_t error_code)
{
    // Análisis del error code x86:
    // Bit 0: Present (0 = page not present, 1 = protection violation)
    // Bit 1: Write (0 = read, 1 = write)
    // Bit 2: User (0 = supervisor, 1 = user mode)

    int present = error_code & 0x1;
    int write = (error_code & 0x2) >> 1;
    int user = (error_code & 0x4) >> 2;

    // Si página está presente, es violación de permisos
    if (present)
    {
        LOG_ERR("Page fault: Protection violation (present=1)");
        return 0;
    }

    // Si intenta escribir pero área no es writable
    if (write && !(vm_area->flags & PAGE_FLAG_WRITABLE))
    {
        LOG_ERR("Page fault: Write to non-writable area");
        return 0;
    }

    // Si acceso desde user mode pero área no es user
    if (user && !(vm_area->flags & PAGE_FLAG_USER))
    {
        LOG_ERR("Page fault: User access to kernel area");
        return 0;
    }

    return 1; // Permisos válidos
}

// ===============================================================================
// INTEGRACIÓN CON EL SISTEMA EXISTENTE
// ===============================================================================

// Esta función reemplaza tu page_fault_handler actual en interrupt/isr_handlers.c
void page_fault_handler_improved(void)
{
    print_colored("\n[ISR] *** PAGE FAULT ON-DEMAND ***\n", VGA_COLOR_YELLOW, VGA_COLOR_BLACK);

    uintptr_t fault_addr = read_fault_address();

    // Obtener error code del stack (lo pushea la CPU automáticamente)
    uint32_t error_code;
    asm volatile("mov 4(%%esp), %0" : "=r"(error_code));

    print("Fault address: 0x");
    print_hex_compact(fault_addr);
    print("\n");

    print("Error code: 0x");
    print_hex_compact(error_code);
    print("\n");

    // Intentar manejar con on-demand paging
    int result = handle_page_fault_ondemand(fault_addr, error_code);

    if (result == 0)
    {
        print_success("Page fault resuelto con on-demand allocation\n");
        return; // Continúa ejecución normal
    }

    // Si no se pudo resolver, es un fault real
    print_error("Page fault no se pudo resolver - error crítico\n");

    switch (result)
    {
    case -1:
        print_error("Acceso a memoria no mapeada\n");
        break;
    case -2:
        print_error("Violación de permisos de memoria\n");
        break;
    case -3:
        print_error("Sin memoria física disponible\n");
        break;
    case -4:
        print_error("Error en mapeo de página\n");
        break;
    default:
        print_error("Error desconocido en page fault\n");
    }

    panic("Page fault crítico no recuperable");
}

// ===============================================================================
// FUNCIONES DE ALTO NIVEL PARA USAR EN EL KERNEL
// ===============================================================================

// NOTA: valloc y vfree están implementados en vallocator.c
// para evitar duplicación de código

// ===============================================================================
// DEBUGGING Y ESTADÍSTICAS
// ===============================================================================

void debug_ondemand_paging(void)
{
    print_colored("=== ON-DEMAND PAGING STATE ===\n", VGA_COLOR_CYAN, VGA_COLOR_BLACK);

    print("Initialized: ");
    print(ondemand_initialized ? "YES" : "NO");
    print("\n");

    print("Total page faults: ");
    print_hex_compact(on_demand_faults);
    print("\n");

    print("Pages allocated on-demand: ");
    print_hex_compact(pages_allocated_on_demand);
    print("\n");

    print("VM Areas:\n");
    vm_area_t *current = vm_areas;
    int area_count = 0;

    while (current && area_count < 10)
    {
        print("  Area ");
        print_hex_compact(area_count);
        print(": 0x");
        print_hex_compact(current->start);
        print(" - 0x");
        print_hex_compact(current->end);
        print(" (flags: 0x");
        print_hex_compact(current->flags);
        print(")\n");

        current = current->next;
        area_count++;
    }

    print("\n");
}