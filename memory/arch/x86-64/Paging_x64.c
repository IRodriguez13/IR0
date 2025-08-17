#include <stdint.h>
#include <stddef.h>
#include "Paging_x64.h"
#include <string.h> // Para memset
#include <print.h>  // Para print
#include "../../krnl_memo_layout.h" // Para LAPIC_BASE

// Estructuras para paginación x86_64 (4 niveles)
#define PML4_ENTRIES 512
#define PDPT_ENTRIES 512
#define PD_ENTRIES 512
#define PT_ENTRIES 512
#define PAGE_SIZE 4096

// Tablas alineadas a 4KB
__attribute__((aligned(PAGE_SIZE))) uint64_t PML4[PML4_ENTRIES];
__attribute__((aligned(PAGE_SIZE))) uint64_t PDPT[PDPT_ENTRIES];
__attribute__((aligned(PAGE_SIZE))) uint64_t PD[PD_ENTRIES];
__attribute__((aligned(PAGE_SIZE))) uint64_t PT[PT_ENTRIES];

// Flags comunes
#define PAGE_PRESENT (1ULL << 0)
#define PAGE_WRITE (1ULL << 1)
#define PAGE_USER (1ULL << 2)
#define PAGE_HUGE (1ULL << 7)
#define PAGE_CACHE_DISABLE (1ULL << 4) // Para MMIO
#define PAGE_WRITE_THROUGH (1ULL << 3) // Para MMIO

// Flags específicos para MMIO (LAPIC)
#define PAGE_MMIO (PAGE_PRESENT | PAGE_WRITE | PAGE_CACHE_DISABLE | PAGE_WRITE_THROUGH)

// Implementación local de memset para evitar dependencias
static void *local_memset(void *ptr, int value, size_t num)
{
    unsigned char *p = (unsigned char *)ptr;
    for (size_t i = 0; i < num; i++)
    {
        p[i] = (unsigned char)value;
    }
    return ptr;
}

// Función para llenar una page table
static void fill_page_table(uint64_t *table, uint64_t base_addr, uint64_t flags)
{
    for (int i = 0; i < PT_ENTRIES; i++)
    {
        table[i] = (base_addr + (i * PAGE_SIZE)) | flags;
    }
}

void init_paging_x64()
{
    print("init_paging_x64: INICIANDO PAGINACIÓN x86-64\n");
    
    // 1. Limpiar todas las entradas
    local_memset(PML4, 0, PAGE_SIZE);
    local_memset(PDPT, 0, PAGE_SIZE);
    local_memset(PD, 0, PAGE_SIZE);
    local_memset(PT, 0, PAGE_SIZE);
    
    print("init_paging_x64: Tablas limpiadas\n");
    
    // 2. Mapeo identidad simple de los primeros 2MB (para código/tablas)
    PML4[0] = ((uint64_t)PDPT) | PAGE_PRESENT | PAGE_WRITE;
    PDPT[0] = ((uint64_t)PD) | PAGE_PRESENT | PAGE_WRITE;
    PD[0] = 0x0 | PAGE_PRESENT | PAGE_WRITE | PAGE_HUGE;
    
    print("init_paging_x64: Identity mapping básico configurado\n");
    
    // 3. Mapear más memoria para el kernel (hasta 256MB)
    for (int i = 1; i < 128; i++)
    {
        PD[i] = (i * 0x200000) | PAGE_PRESENT | PAGE_WRITE | PAGE_HUGE;
    }
    
    print("init_paging_x64: Identity mapping extendido a 256MB\n");
    
    // 4. AGREGADO: Mapeo LAPIC (0xfee00000 - 0xfef00000) usando PTs consistentes
    print("init_paging_x64: Configurando mapeo LAPIC con PTs...\n");
    
    // Calcular índices para LAPIC en PML4, PDPT, PD, PT
    uint64_t lapic_virt = LAPIC_BASE; // 0x00000000fee00000ULL
    uint64_t pml4_index = (lapic_virt >> 39) & 0x1FF;  // bits 39-47: 0x0
    uint64_t pdpt_index = (lapic_virt >> 30) & 0x1FF;  // bits 30-38: 0x0
    uint64_t pd_index = (lapic_virt >> 21) & 0x1FF;    // bits 21-29: 0x1F7
    uint64_t pt_index = (lapic_virt >> 12) & 0x1FF;    // bits 12-20: 0x0EE
    
    print("init_paging_x64: LAPIC address: 0x");
    print_hex64(lapic_virt);
    print("\n");
    print("init_paging_x64: LAPIC indices - PML4:");
    print_hex64(pml4_index);
    print(", PDPT:");
    print_hex64(pdpt_index);
    print(", PD:");
    print_hex64(pd_index);
    print(", PT:");
    print_hex64(pt_index);
    print("\n");
    
    // VERIFICACIÓN: Los índices correctos para 0x00000000fee00000ULL:
    // PML4: 0x0, PDPT: 0x0, PD: 0x1F7, PT: 0x0EE
    if (pml4_index != 0x0 || pdpt_index != 0x0 || pd_index != 0x1F7 || pt_index != 0x0EE) {
        print("init_paging_x64: ERROR - Índices LAPIC incorrectos!\n");
        print("init_paging_x64: Esperados - PML4:0x0, PDPT:0x0, PD:0x1F7, PT:0x0EE\n");
    } else {
        print("init_paging_x64: Índices LAPIC correctos\n");
    }
    
    // 🔍 DIAGNÓSTICO: Verificar estado actual de PD[0x1F7] antes del mapeo
    print("init_paging_x64: Verificando PD[");
    print_hex64(pd_index);
    print("] antes del mapeo LAPIC...\n");
    print("init_paging_x64: PD[");
    print_hex64(pd_index);
    print("] = 0x");
    print_hex64(PD[pd_index]);
    print("\n");
    
    // Verificar si tiene PAGE_HUGE (bit 7)
    if (PD[pd_index] & PAGE_HUGE) {
        print("init_paging_x64: ERROR - PD[");
        print_hex64(pd_index);
        print("] tiene PAGE_HUGE activado!\n");
        print("init_paging_x64: Esto causará que la CPU ignore la PT y use huge page\n");
    } else {
        print("init_paging_x64: PD[");
        print_hex64(pd_index);
        print("] NO tiene PAGE_HUGE - correcto\n");
    }
    
    // Verificar si tiene PAGE_PRESENT (bit 0)
    if (PD[pd_index] & PAGE_PRESENT) {
        print("init_paging_x64: PD[");
        print_hex64(pd_index);
        print("] tiene PAGE_PRESENT - ya está mapeado\n");
    } else {
        print("init_paging_x64: PD[");
        print_hex64(pd_index);
        print("] NO tiene PAGE_PRESENT - necesita mapeo\n");
    }
    
    // Crear PT específica para LAPIC si no existe
    // PD[0x1F7] debe apuntar a una PT, no ser una huge page
    static __attribute__((aligned(PAGE_SIZE))) uint64_t LAPIC_PT[PT_ENTRIES];
    
    if ((PD[pd_index] & PAGE_PRESENT) == 0) {
        // Crear nueva PT para LAPIC
        local_memset(LAPIC_PT, 0, PAGE_SIZE);
        
        print("init_paging_x64: Creando nueva PT para LAPIC en PD[");
        print_hex64(pd_index);
        print("] = ");
        print_hex64((uint64_t)LAPIC_PT);
        print("\n");
        
        PD[pd_index] = ((uint64_t)LAPIC_PT) | PAGE_PRESENT | PAGE_WRITE;
    } else {
        // Usar PT existente
        print("init_paging_x64: Usando PT existente en PD[");
        print_hex64(pd_index);
        print("]\n");
    }
    
    // Obtener la PT correspondiente (debe ser LAPIC_PT)
    uint64_t *lapic_pt = (uint64_t *)(PD[pd_index] & ~0xFFFULL);
    
    print("init_paging_x64: PT address: ");
    print_hex64((uint64_t)lapic_pt);
    print(", LAPIC_PT address: ");
    print_hex64((uint64_t)LAPIC_PT);
    print("\n");
    
    // Verificar que apunta a LAPIC_PT
    if (lapic_pt != LAPIC_PT) {
        print("init_paging_x64: ERROR - lapic_pt no apunta a LAPIC_PT!\n");
    } else {
        print("init_paging_x64: lapic_pt apunta correctamente a LAPIC_PT\n");
    }
    
    // Mapear páginas LAPIC (1MB = 256 páginas de 4KB)
    print("init_paging_x64: Mapeando páginas LAPIC desde PT[");
    print_hex64(pt_index);
    print("] hasta PT[");
    print_hex64(pt_index + 255);
    print("]\n");
    
    for (int i = 0; i < 256; i++)
    {
        uint64_t phys_addr = LAPIC_BASE + (i * PAGE_SIZE);
        lapic_pt[pt_index + i] = phys_addr | PAGE_MMIO;
        
        // Debug: mostrar algunas entradas
        if (i == 0 || i == 255) {
            print("init_paging_x64: PT[");
            print_hex64(pt_index + i);
            print("] = 0x");
            print_hex64(lapic_pt[pt_index + i]);
            print("\n");
        }
    }
    
    print("init_paging_x64: LAPIC mapping configurado (1MB, PTs, MMIO flags)\n");
    
    // 5. Llamar a la función que carga CR3
    paging_set_cpu_x64((uint64_t)PML4);
    
    print("init_paging_x64: Paginación x86-64 inicializada con LAPIC mapping\n");
}

void paging_set_cpu_x64(uint64_t pml4_addr)
{
    asm volatile("mov %0, %%cr3" : : "r"(pml4_addr) : "memory");
    asm volatile("mov %%cr0, %%rax\n" "bts $31, %%rax\n" "mov %%rax, %%cr0" : : : "rax", "memory");
    print("paging_set_cpu_x64: CR3 cargado y paginación habilitada\n");
}