// memory/heap_allocator.c - Implementación del heap híbrido con fallback
#include "memo_interface.h"
#include "heap_allocator.h"
#include "physical_allocator.h"
#include "krnl_memo_layout.h"
#include <print.h>
#include <panic/panic.h>
#include <string.h>
#include <stdbool.h>

// ===============================================================================
// CONFIGURACIÓN Y DEFINICIONES
// ===============================================================================

#define PAGE_SIZE 4096

// Configuración del heap híbrido
#define HEAP_STATIC_SIZE (128 * 1024)          // 128KB inicial estático
#define HEAP_STATIC_FALLBACK_SIZE (512 * 1024) // 512KB fallback estático
#define HEAP_SAFE_RANGE_START 0x00100000       // 1MB (después del kernel)
#define HEAP_SAFE_RANGE_END 0x00200000         // 2MB (límite del mapeo identidad)
#define HEAP_SAFE_PAGES ((HEAP_SAFE_RANGE_END - HEAP_SAFE_RANGE_START) / PAGE_SIZE)

// Configuración de paginación en tiempo real
#define PREALLOC_BATCH_SIZE 8 // Pre-asignar 8 páginas (32KB) por lote
#define MAX_PREALLOC_PAGES 32 // Máximo 32 páginas (128KB) pre-asignadas
#define RECOVERY_BATCH_SIZE 4 // Páginas de recuperación por intento
#define RECOVERY_ATTEMPTS 3   // Intentos de recuperación del heap dinámico

// ===============================================================================
// ESTRUCTURAS DE DATOS
// ===============================================================================

// heap_block_t ya está definido en heap_allocator.h

// Estado de paginación en tiempo real
typedef struct
{
    uintptr_t prealloc_pages[MAX_PREALLOC_PAGES]; // Páginas físicas pre-asignadas
    uintptr_t prealloc_virt[MAX_PREALLOC_PAGES];  // Direcciones virtuales mapeadas
    uint32_t prealloc_count;                      // Número de páginas pre-asignadas
    bool prealloc_active;                         // Si la pre-asignación está activa
    uint32_t prealloc_used;                       // Páginas ya transferidas al heap
    uintptr_t prealloc_virt_start;                // Dirección virtual de inicio del pool
    uintptr_t prealloc_virt_end;                  // Dirección virtual final del pool

    // Sistema de recuperación resiliente
    bool recovery_mode;              // Si estamos en modo de recuperación
    uint32_t recovery_attempts;      // Intentos de recuperación realizados
    uint32_t recovery_pages_created; // Páginas creadas en modo recuperación
    bool recovery_pool_ready;        // Si el pool de recuperación está listo
} realtime_paging_t;

// Estado del heap híbrido
typedef struct
{
    uintptr_t static_start;                    // Inicio del heap estático
    uintptr_t static_end;                      // Fin del heap estático
    uintptr_t fallback_start;                  // Inicio del heap fallback
    uintptr_t fallback_end;                    // Fin del heap fallback
    uintptr_t dynamic_start;                   // Inicio del heap dinámico (en rango seguro)
    uintptr_t dynamic_end;                     // Fin del heap dinámico
    uintptr_t physical_pages[HEAP_SAFE_PAGES]; // Páginas físicas asignadas
    uint32_t page_count;                       // Número de páginas asignadas
    uint32_t max_pages;                        // Máximo número de páginas
    bool dynamic_enabled;                      // Si el heap dinámico está habilitado
    bool fallback_used;                        // Si estamos usando el fallback
    bool initialized;                          // Estado de inicialización
    realtime_paging_t paging;                  // Sistema de paginación en tiempo real
} hybrid_heap_t;

// ===============================================================================
// VARIABLES GLOBALES
// ===============================================================================

static hybrid_heap_t hybrid_heap = {0};
static heap_block_t *heap_start = NULL;
static size_t heap_total_size = 0;
// Definir las variables que están declaradas como extern en heap_allocator.h
size_t heap_used_bytes = 0;
size_t heap_free_bytes = 0;

// Variables externas del physical allocator
extern uint32_t free_pages_count;
extern uint32_t total_pages_count;

// Prototipos de funciones del physical allocator
extern void physical_allocator_init(void);
extern uintptr_t alloc_physical_page(void);
extern void free_physical_page(uintptr_t phys_addr);

// Prototipo de map_page
extern int map_page(uintptr_t virt_addr, uintptr_t phys_addr, uint32_t flags);

// Variables de control del sistema de memoria
static int memory_system_initialized = 0;

// Prototipos de funciones adelantados
static int hybrid_heap_grow(size_t additional_pages);
static int start_realtime_paging(void);
static int transfer_prealloc_to_heap(size_t pages_needed);
static void stop_realtime_paging(void);
static int start_recovery_mode(void);
static int attempt_dynamic_recovery(void);
static int create_recovery_pool(size_t pages_needed);

// ===============================================================================
// PAGINACIÓN EN TIEMPO REAL
// ===============================================================================

// Iniciar paginación en tiempo real
static int start_realtime_paging(void)
{
    if (hybrid_heap.paging.prealloc_active)
    {
        return 0; // Ya está activa
    }

    print("start_realtime_paging: Iniciando paginación en tiempo real\n");

    // Configurar rango virtual para el pool de páginas pre-asignadas
    // Usar un rango diferente al heap dinámico para evitar conflictos
    hybrid_heap.paging.prealloc_virt_start = HEAP_SAFE_RANGE_START + (HEAP_SAFE_PAGES * PAGE_SIZE);
    hybrid_heap.paging.prealloc_virt_end = hybrid_heap.paging.prealloc_virt_start + (MAX_PREALLOC_PAGES * PAGE_SIZE);

    // Verificar que no se solape con el heap dinámico
    if (hybrid_heap.paging.prealloc_virt_end > HEAP_SAFE_RANGE_END)
    {
        LOG_ERR("start_realtime_paging: Rango de pre-asignación fuera de límites");
        return -1;
    }

    hybrid_heap.paging.prealloc_count = 0;
    hybrid_heap.paging.prealloc_used = 0;
    hybrid_heap.paging.prealloc_active = true;

    print("  Pool virtual: 0x");
    print_hex_compact(hybrid_heap.paging.prealloc_virt_start);
    print(" - 0x");
    print_hex_compact(hybrid_heap.paging.prealloc_virt_end);
    print("\n");

    return 0;
}

// Asignar y mapear páginas en tiempo real
static int prealloc_pages_batch(size_t batch_size)
{
    if (!hybrid_heap.paging.prealloc_active)
    {
        return -1;
    }

    if (hybrid_heap.paging.prealloc_count + batch_size > MAX_PREALLOC_PAGES)
    {
        batch_size = MAX_PREALLOC_PAGES - hybrid_heap.paging.prealloc_count;
    }

    if (batch_size == 0)
    {
        return 0; // Pool lleno
    }

    print("prealloc_pages_batch: Asignando ");
    print_uint32(batch_size);
    print(" páginas en tiempo real\n");

    for (uint32_t i = 0; i < batch_size; i++)
    {
        uint32_t idx = hybrid_heap.paging.prealloc_count + i;

        // 1. Asignar página física
        uintptr_t phys_page = alloc_physical_page();
        if (phys_page == 0)
        {
            LOG_ERR("prealloc_pages_batch: No se pudo allocar página física");
            return -1;
        }

        // 2. Calcular dirección virtual
        uintptr_t virt_page = hybrid_heap.paging.prealloc_virt_start + (idx * PAGE_SIZE);

        // 3. Mapear página en tiempo real
        if (arch_map_page(virt_page, phys_page, PAGE_FLAG_PRESENT | PAGE_FLAG_WRITABLE) != 0)
        {
            LOG_ERR("prealloc_pages_batch: No se pudo mapear página virtual");
            free_physical_page(phys_page);
            return -1;
        }

        // 4. Guardar referencias
        hybrid_heap.paging.prealloc_pages[idx] = phys_page;
        hybrid_heap.paging.prealloc_virt[idx] = virt_page;

        print("  Página ");
        print_uint32(idx);
        print(": física=0x");
        print_hex_compact(phys_page);
        print(" → virtual=0x");
        print_hex_compact(virt_page);
        print("\n");
    }

    hybrid_heap.paging.prealloc_count += batch_size;

    LOG_OK("prealloc_pages_batch: Páginas asignadas y mapeadas exitosamente");
    print("  Total pre-asignadas: ");
    print_uint32(hybrid_heap.paging.prealloc_count);
    print(" / ");
    print_uint32(MAX_PREALLOC_PAGES);
    print("\n");

    return 0;
}

// Transferir páginas pre-asignadas al heap dinámico
static int transfer_prealloc_to_heap(size_t pages_needed)
{
    if (!hybrid_heap.paging.prealloc_active || hybrid_heap.paging.prealloc_count == 0)
    {
        return -1;
    }

    if (pages_needed > hybrid_heap.paging.prealloc_count)
    {
        pages_needed = hybrid_heap.paging.prealloc_count;
    }

    print("transfer_prealloc_to_heap: Transfiriendo ");
    print_uint32(pages_needed);
    print(" páginas al heap dinámico\n");

    // Calcular nueva posición en el heap dinámico
    uint32_t new_heap_pages = hybrid_heap.page_count + pages_needed;
    uintptr_t new_heap_end = hybrid_heap.dynamic_start + (new_heap_pages * PAGE_SIZE);

    // Verificar que no exceda el límite
    if (new_heap_end > HEAP_SAFE_RANGE_END)
    {
        LOG_ERR("transfer_prealloc_to_heap: Excedido límite del heap dinámico");
        return -1;
    }

    // Transferir páginas una por una
    for (uint32_t i = 0; i < pages_needed; i++)
    {
        uint32_t prealloc_idx = hybrid_heap.paging.prealloc_used + i;
        uint32_t heap_idx = hybrid_heap.page_count + i;

        // 1. Obtener página pre-asignada
        uintptr_t phys_page = hybrid_heap.paging.prealloc_pages[prealloc_idx];
        uintptr_t old_virt_page = hybrid_heap.paging.prealloc_virt[prealloc_idx];

        // 2. Calcular nueva dirección virtual en el heap
        uintptr_t new_virt_page = hybrid_heap.dynamic_start + (heap_idx * PAGE_SIZE);

        // 3. Desmapear página antigua
        arch_unmap_page(old_virt_page);

        // 4. Mapear en nueva ubicación del heap
        if (arch_map_page(new_virt_page, phys_page, PAGE_FLAG_PRESENT | PAGE_FLAG_WRITABLE) != 0)
        {
            LOG_ERR("transfer_prealloc_to_heap: Error al mapear página en heap");
            // Re-mapear en ubicación original
            arch_map_page(old_virt_page, phys_page, PAGE_FLAG_PRESENT | PAGE_FLAG_WRITABLE);
            return -1;
        }

        // 5. Actualizar registro del heap dinámico
        hybrid_heap.physical_pages[heap_idx] = phys_page;

        print("  Transferida página ");
        print_uint32(prealloc_idx);
        print(": 0x");
        print_hex_compact(old_virt_page);
        print(" → 0x");
        print_hex_compact(new_virt_page);
        print("\n");
    }

    // Actualizar estado
    hybrid_heap.page_count += pages_needed;
    hybrid_heap.dynamic_end = hybrid_heap.dynamic_start + (hybrid_heap.page_count * PAGE_SIZE);
    hybrid_heap.paging.prealloc_used += pages_needed;

    // Mover páginas restantes al inicio del pool
    if (hybrid_heap.paging.prealloc_used < hybrid_heap.paging.prealloc_count)
    {
        uint32_t remaining = hybrid_heap.paging.prealloc_count - hybrid_heap.paging.prealloc_used;
        for (uint32_t i = 0; i < remaining; i++)
        {
            hybrid_heap.paging.prealloc_pages[i] = hybrid_heap.paging.prealloc_pages[hybrid_heap.paging.prealloc_used + i];
            hybrid_heap.paging.prealloc_virt[i] = hybrid_heap.paging.prealloc_virt[hybrid_heap.paging.prealloc_used + i];
        }
        hybrid_heap.paging.prealloc_count = remaining;
        hybrid_heap.paging.prealloc_used = 0;
    }
    else
    {
        // Pool vacío, reiniciar
        hybrid_heap.paging.prealloc_count = 0;
        hybrid_heap.paging.prealloc_used = 0;
    }

    LOG_OK("transfer_prealloc_to_heap: Transferencia completada exitosamente");
    print("  Heap dinámico: ");
    print_uint32(hybrid_heap.page_count);
    print(" páginas (");
    print_uint32(hybrid_heap.page_count * PAGE_SIZE / 1024);
    print(" KB)\n");
    print("  Pool restante: ");
    print_uint32(hybrid_heap.paging.prealloc_count);
    print(" páginas\n");

    return 0;
}

// Detener paginación en tiempo real
static void stop_realtime_paging(void)
{
    if (!hybrid_heap.paging.prealloc_active)
    {
        return;
    }

    print("stop_realtime_paging: Limpiando pool de páginas pre-asignadas\n");

    // Liberar todas las páginas pre-asignadas
    for (uint32_t i = 0; i < hybrid_heap.paging.prealloc_count; i++)
    {
        uintptr_t phys_page = hybrid_heap.paging.prealloc_pages[i];
        uintptr_t virt_page = hybrid_heap.paging.prealloc_virt[i];

        // Desmapear y liberar
        arch_unmap_page(virt_page);
        free_physical_page(phys_page);

        print("  Liberada página ");
        print_uint32(i);
        print(": 0x");
        print_hex_compact(virt_page);
        print(" (física: 0x");
        print_hex_compact(phys_page);
        print(")\n");
    }

    // Resetear estado
    hybrid_heap.paging.prealloc_count = 0;
    hybrid_heap.paging.prealloc_used = 0;
    hybrid_heap.paging.prealloc_active = false;

    LOG_OK("stop_realtime_paging: Pool de páginas pre-asignadas limpiado");
}

// ===============================================================================
// SISTEMA DE RECUPERACIÓN RESILIENTE
// ===============================================================================

// Iniciar modo de recuperación cuando el heap dinámico falla
static int start_recovery_mode(void)
{
    if (hybrid_heap.paging.recovery_mode)
    {
        return 0; // Ya en modo recuperación
    }

    print("start_recovery_mode: Activando recuperación resiliente del heap dinámico\n");
    print("  🔧 El heap dinámico falló, pero NO nos rendimos!\n");
    print("  🚀 Creando pool de páginas para reintento\n");

    // Activar modo de recuperación
    hybrid_heap.paging.recovery_mode = true;
    hybrid_heap.paging.recovery_attempts = 0;
    hybrid_heap.paging.recovery_pages_created = 0;
    hybrid_heap.paging.recovery_pool_ready = false;

    // Inicializar paginación si no está activa
    if (!hybrid_heap.paging.prealloc_active)
    {
        if (start_realtime_paging() != 0)
        {
            LOG_ERR("start_recovery_mode: No se pudo iniciar paginación para recuperación");
            return -1;
        }
    }

    LOG_OK("start_recovery_mode: Modo de recuperación activado");
    print("  📦 Preparando pool de páginas para alimentar heap dinámico\n");
    print("  🎯 Objetivo: Volver al heap dinámico lo antes posible\n");

    return 0;
}

// Crear pool de páginas para recuperación (mapeadas y verificadas)
static int create_recovery_pool(size_t pages_needed)
{
    if (!hybrid_heap.paging.recovery_mode)
    {
        return -1;
    }

    print("create_recovery_pool: Creando pool de recuperación (");
    print_uint32(pages_needed);
    print(" páginas)\n");
    print("  🏗️  Asignando páginas que NO apunten a sistemas estelares lejanos\n");

    size_t pages_to_create = pages_needed;

    // Asignar páginas en lotes, con verificación exhaustiva
    while (pages_to_create > 0 && hybrid_heap.paging.prealloc_count < MAX_PREALLOC_PAGES)
    {
        size_t batch_size = (pages_to_create > RECOVERY_BATCH_SIZE) ? RECOVERY_BATCH_SIZE : pages_to_create;

        print("  📦 Lote de recuperación: ");
        print_uint32(batch_size);
        print(" páginas\n");

        // Crear lote con verificación anti-galaxia
        for (uint32_t i = 0; i < batch_size; i++)
        {
            if (hybrid_heap.paging.prealloc_count >= MAX_PREALLOC_PAGES)
            {
                break;
            }

            // 1. Asignar página física
            uintptr_t phys_page = alloc_physical_page();
            if (phys_page == 0)
            {
                LOG_WARN("create_recovery_pool: Página física agotada, continuando con las disponibles");
                break;
            }

            // 2. VERIFICACIÓN ANTI-ESPACIO: ¿Es una dirección terrestre?
            if (phys_page > 0x100000000ULL)
            { // > 4GB = sospechoso
                LOG_WARN("create_recovery_pool: Página física sospechosa, liberando");
                free_physical_page(phys_page);
                continue;
            }

            // 3. Calcular dirección virtual segura
            uint32_t idx = hybrid_heap.paging.prealloc_count;
            uintptr_t virt_page = hybrid_heap.paging.prealloc_virt_start + (idx * PAGE_SIZE);

            // 4. VERIFICACIÓN ANTI-GALAXIA: ¿La virtual es terrestre?
            if (virt_page >= HEAP_SAFE_RANGE_END)
            {
                LOG_ERR("create_recovery_pool: Dirección virtual fuera del rango seguro!");
                free_physical_page(phys_page);
                break;
            }

            // 5. Mapear con verificación
            if (arch_map_page(virt_page, phys_page, PAGE_FLAG_PRESENT | PAGE_FLAG_WRITABLE) != 0)
            {
                LOG_WARN("create_recovery_pool: Error al mapear, intentando siguiente página");
                free_physical_page(phys_page);
                continue;
            }

            // 6. VERIFICACIÓN FINAL: ¿La página mapeada es accesible?
            // Escribir y leer un valor de prueba
            volatile uint32_t *test_ptr = (volatile uint32_t *)virt_page;
            *test_ptr = 0xDEADBEEF; // Valor de prueba terrestre (no galáctico)

            if (*test_ptr != 0xDEADBEEF)
            {
                LOG_ERR("create_recovery_pool: Página no accesible después del mapeo!");
                arch_unmap_page(virt_page);
                free_physical_page(phys_page);
                continue;
            }

            // 7. ¡Éxito! Guardar página verificada
            hybrid_heap.paging.prealloc_pages[idx] = phys_page;
            hybrid_heap.paging.prealloc_virt[idx] = virt_page;
            hybrid_heap.paging.prealloc_count++;
            hybrid_heap.paging.recovery_pages_created++;

            print("    ✅ Página ");
            print_uint32(idx);
            print(": V=0x");
            print_hex_compact(virt_page);
            print(" F=0x");
            print_hex_compact(phys_page);
            print(" [TERRESTRE]\n");
        }

        pages_to_create -= batch_size;
    }

    // Marcar pool como listo si tenemos páginas
    if (hybrid_heap.paging.recovery_pages_created > 0)
    {
        hybrid_heap.paging.recovery_pool_ready = true;

        LOG_OK("create_recovery_pool: Pool de recuperación creado exitosamente");
        print("  📊 Páginas creadas: ");
        print_uint32(hybrid_heap.paging.recovery_pages_created);
        print("\n");
        print("  🌍 Todas las direcciones verificadas como terrestres\n");
        print("  🚀 Pool listo para alimentar heap dinámico\n");

        return 0;
    }

    LOG_ERR("create_recovery_pool: No se pudieron crear páginas de recuperación");
    return -1;
}

// Intentar recuperar el heap dinámico usando el pool creado
static int attempt_dynamic_recovery(void)
{
    if (!hybrid_heap.paging.recovery_mode || !hybrid_heap.paging.recovery_pool_ready)
    {
        return -1;
    }

    if (hybrid_heap.paging.recovery_attempts >= RECOVERY_ATTEMPTS)
    {
        LOG_WARN("attempt_dynamic_recovery: Máximo de intentos de recuperación alcanzado");
        return -1;
    }

    hybrid_heap.paging.recovery_attempts++;

    print("attempt_dynamic_recovery: Intento ");
    print_uint32(hybrid_heap.paging.recovery_attempts);
    print(" de ");
    print_uint32(RECOVERY_ATTEMPTS);
    print("\n");
    print("  🎯 Intentando rehabilitar heap dinámico con pool verificado\n");

    // Verificar que tenemos páginas disponibles
    if (hybrid_heap.paging.prealloc_count == 0)
    {
        LOG_ERR("attempt_dynamic_recovery: No hay páginas en el pool");
        return -1;
    }

    // Intentar reactivar el heap dinámico
    hybrid_heap.dynamic_enabled = true;

    // Transferir algunas páginas del pool al heap dinámico
    size_t pages_to_transfer = (hybrid_heap.paging.prealloc_count > 4) ? 4 : hybrid_heap.paging.prealloc_count;

    print("  🔄 Transfiriendo ");
    print_uint32(pages_to_transfer);
    print(" páginas del pool al heap dinámico\n");

    if (transfer_prealloc_to_heap(pages_to_transfer) == 0)
    {
        // ¡Éxito! El heap dinámico está funcionando de nuevo
        LOG_OK("attempt_dynamic_recovery: ¡RECUPERACIÓN EXITOSA!");
        print("  🎉 Heap dinámico rehabilitado con páginas del pool\n");
        print("  ⚡ Volviendo a usar memoria dinámica como principal\n");
        print("  🌍 Todas las direcciones siguen siendo terrestres\n");

        // Salir del modo de recuperación
        hybrid_heap.paging.recovery_mode = false;

        return 0;
    }
    else
    {
        // Falló el intento, deshabilitar dinámico de nuevo
        hybrid_heap.dynamic_enabled = false;

        LOG_WARN("attempt_dynamic_recovery: Intento de recuperación falló");
        print("  ⏳ Continuando con heap estático mientras creamos más páginas\n");

        return -1;
    }
}

// ===============================================================================
// FUNCIONES AUXILIARES
// ===============================================================================

// Verificar si un puntero es válido
bool is_valid_heap_pointer(void *ptr)
{
    if (!ptr || !heap_start)
        return false;

    heap_block_t *block = (heap_block_t *)((uint8_t *)ptr - sizeof(heap_block_t));

    // Verificar que esté dentro del rango del heap
    if ((void *)block < (void *)heap_start)
        return false;

    // Verificar que no exceda el final del heap
    uintptr_t heap_end = (uintptr_t)heap_start + heap_total_size;
    if ((uintptr_t)block >= heap_end)
        return false;

    if (block->magic != HEAP_MAGIC)
        return false;

    return true;
}

// Dividir un bloque en dos
void split_block(heap_block_t *block, size_t wanted_size)
{
    if (block->size < wanted_size + sizeof(heap_block_t) + MIN_BLOCK_SIZE)
    {
        return; // No se puede dividir, demasiado pequeño
    }

    heap_block_t *new_block = (heap_block_t *)((uint8_t *)block + sizeof(heap_block_t) + wanted_size);
    new_block->size = block->size - wanted_size - sizeof(heap_block_t);
    new_block->next = block->next;
    new_block->prev = block;
    new_block->is_free = true;
    new_block->magic = HEAP_MAGIC;

    if (block->next)
    {
        block->next->prev = new_block;
    }

    block->next = new_block;
    block->size = wanted_size;
}

// ===============================================================================
// INICIALIZACIÓN DEL HEAP HÍBRIDO
// ===============================================================================

void heap_allocator_init(void)
{
    if (hybrid_heap.initialized)
    {
        LOG_WARN("heap_allocator_init: Heap ya inicializado");
        return;
    }

    print("heap_allocator_init: Inicializando heap híbrido (prioridad: dinámico→estático→fallback)\n");

    // Inicializar estructura del heap híbrido
    memset(&hybrid_heap, 0, sizeof(hybrid_heap_t));
    hybrid_heap.max_pages = HEAP_SAFE_PAGES;
    hybrid_heap.dynamic_enabled = true; // PRIORIDAD 1: heap dinámico
    hybrid_heap.fallback_used = false;

    // 1. Preparar heap estático (128KB en stack) - FALLBACK 1
    static uint8_t static_heap[HEAP_STATIC_SIZE];
    hybrid_heap.static_start = (uintptr_t)static_heap;
    hybrid_heap.static_end = (uintptr_t)static_heap + HEAP_STATIC_SIZE;

    // 2. Preparar heap fallback (512KB en stack) - FALLBACK 2
    static uint8_t fallback_heap[HEAP_STATIC_FALLBACK_SIZE];
    hybrid_heap.fallback_start = (uintptr_t)fallback_heap;
    hybrid_heap.fallback_end = (uintptr_t)fallback_heap + HEAP_STATIC_FALLBACK_SIZE;

    // 3. Configurar heap dinámico (en rango seguro 1MB-2MB) - PRINCIPAL
    hybrid_heap.dynamic_start = HEAP_SAFE_RANGE_START;
    hybrid_heap.dynamic_end = HEAP_SAFE_RANGE_START; // Se expandirá

    // ESTRATEGIA: Empezar con heap dinámico directamente
    print("heap_allocator_init: Intentando inicializar heap dinámico como principal\n");

    // Intentar asignar páginas iniciales para el heap dinámico
    if (hybrid_heap_grow(4) == 0)
    { // Iniciar con 16KB dinámicos
        print("  ✅ Heap dinámico inicializado como principal\n");

        // El heap_start ya fue configurado por hybrid_heap_grow
        heap_total_size = 4 * PAGE_SIZE; // 16KB dinámicos
    }
    else
    {
        print("  ⚠️  Heap dinámico falló, usando heap estático como fallback\n");

        // Fallback al heap estático
        heap_start = (heap_block_t *)static_heap;
        heap_start->size = HEAP_STATIC_SIZE - sizeof(heap_block_t);
        heap_start->magic = HEAP_MAGIC;
        heap_start->is_free = true;
        heap_start->next = NULL;
        heap_start->prev = NULL;

        heap_total_size = HEAP_STATIC_SIZE;
        heap_free_bytes = heap_start->size;
        heap_used_bytes = 0;

        hybrid_heap.dynamic_enabled = false; // Deshabilitar dinámico
    }

    // Marcar como inicializado
    hybrid_heap.initialized = true;

    LOG_OK("heap_allocator_init: Heap híbrido inicializado exitosamente");
    print("  Prioridad 1 (dinámico): 0x");
    print_hex_compact(hybrid_heap.dynamic_start);
    print(" - 0x");
    print_hex_compact(HEAP_SAFE_RANGE_END);
    print(" ");
    print(hybrid_heap.dynamic_enabled ? "[ACTIVO]" : "[FALLIDO]");
    print("\n");
    print("  Fallback 1 (estático): 128KB @ 0x");
    print_hex_compact(hybrid_heap.static_start);
    print(" [PREPARADO]\n");
    print("  Fallback 2 (extra): 512KB @ 0x");
    print_hex_compact(hybrid_heap.fallback_start);
    print(" [RESERVA]\n");
}

// ===============================================================================
// CRECIMIENTO DEL HEAP HÍBRIDO
// ===============================================================================

static int hybrid_heap_grow(size_t additional_pages)
{
    if (!hybrid_heap.initialized && additional_pages > 0)
    {
        // Caso especial: inicialización del heap dinámico
        print("hybrid_heap_grow: Inicializando heap dinámico\n");
    }

    // PRIORIDAD 1: Heap dinámico (principal)
    if (hybrid_heap.dynamic_enabled)
    {
        // ESTRATEGIA: Paginación en tiempo real + transferencia
        print("hybrid_heap_grow: Usando estrategia de paginación en tiempo real\n");

        // 1. Iniciar paginación en tiempo real si no está activa
        if (!hybrid_heap.paging.prealloc_active)
        {
            if (start_realtime_paging() != 0)
            {
                LOG_ERR("hybrid_heap_grow: No se pudo iniciar paginación en tiempo real");
                hybrid_heap.dynamic_enabled = false;
                goto fallback_static;
            }
        }

        // 2. Verificar si tenemos páginas pre-asignadas suficientes
        if (hybrid_heap.paging.prealloc_count >= additional_pages)
        {
            print("hybrid_heap_grow: Usando páginas pre-asignadas existentes\n");

            // Transferir páginas pre-asignadas al heap
            if (transfer_prealloc_to_heap(additional_pages) == 0)
            {
                // Crear o expandir bloque en el heap dinámico
                if (heap_start == NULL)
                {
                    // Primera vez: crear heap_start en el heap dinámico
                    heap_start = (heap_block_t *)hybrid_heap.dynamic_start;
                    heap_start->size = (hybrid_heap.page_count * PAGE_SIZE) - sizeof(heap_block_t);
                    heap_start->magic = HEAP_MAGIC;
                    heap_start->is_free = true;
                    heap_start->next = NULL;
                    heap_start->prev = NULL;

                    heap_total_size = hybrid_heap.page_count * PAGE_SIZE;
                    heap_free_bytes = heap_start->size;
                    heap_used_bytes = 0;
                }
                else
                {
                    // Expandir heap existente
                    uintptr_t new_block_addr = hybrid_heap.dynamic_start + ((hybrid_heap.page_count - additional_pages) * PAGE_SIZE);
                    heap_block_t *new_block = (heap_block_t *)new_block_addr;

                    new_block->size = (additional_pages * PAGE_SIZE) - sizeof(heap_block_t);
                    new_block->magic = HEAP_MAGIC;
                    new_block->is_free = true;
                    new_block->next = NULL;

                    // Conectar con la lista de bloques
                    heap_block_t *last_block = heap_start;
                    while (last_block && last_block->next)
                    {
                        last_block = last_block->next;
                    }

                    if (last_block)
                    {
                        last_block->next = new_block;
                    }

                    heap_total_size += additional_pages * PAGE_SIZE;
                    heap_free_bytes += new_block->size;
                }

                LOG_OK("hybrid_heap_grow: Heap dinámico expandido con páginas pre-asignadas");
                print("  Páginas transferidas: ");
                print_uint32(additional_pages);
                print(" (");
                print_uint32(heap_total_size / 1024);
                print(" KB total)\n");

                return 0;
            }
        }

        // 3. Si no hay suficientes páginas pre-asignadas, asignar más
        print("hybrid_heap_grow: Asignando páginas en tiempo real\n");

        size_t pages_to_alloc = additional_pages;
        if (hybrid_heap.paging.prealloc_count < additional_pages)
        {
            pages_to_alloc = additional_pages - hybrid_heap.paging.prealloc_count;
        }

        // Asignar en lotes para optimizar
        while (pages_to_alloc > 0)
        {
            size_t batch_size = (pages_to_alloc > PREALLOC_BATCH_SIZE) ? PREALLOC_BATCH_SIZE : pages_to_alloc;

            if (prealloc_pages_batch(batch_size) != 0)
            {
                LOG_ERR("hybrid_heap_grow: Error en asignación de páginas en tiempo real");
                break;
            }

            pages_to_alloc -= batch_size;
        }

        // 4. Intentar transferir las páginas asignadas
        if (hybrid_heap.paging.prealloc_count > 0)
        {
            size_t transfer_size = (hybrid_heap.paging.prealloc_count < additional_pages) ? hybrid_heap.paging.prealloc_count : additional_pages;

            if (transfer_prealloc_to_heap(transfer_size) == 0)
            {
                // Crear o expandir bloque similar al caso anterior
                if (heap_start == NULL)
                {
                    heap_start = (heap_block_t *)hybrid_heap.dynamic_start;
                    heap_start->size = (hybrid_heap.page_count * PAGE_SIZE) - sizeof(heap_block_t);
                    heap_start->magic = HEAP_MAGIC;
                    heap_start->is_free = true;
                    heap_start->next = NULL;
                    heap_start->prev = NULL;

                    heap_total_size = hybrid_heap.page_count * PAGE_SIZE;
                    heap_free_bytes = heap_start->size;
                    heap_used_bytes = 0;
                }
                else
                {
                    uintptr_t new_block_addr = hybrid_heap.dynamic_start + ((hybrid_heap.page_count - transfer_size) * PAGE_SIZE);
                    heap_block_t *new_block = (heap_block_t *)new_block_addr;

                    new_block->size = (transfer_size * PAGE_SIZE) - sizeof(heap_block_t);
                    new_block->magic = HEAP_MAGIC;
                    new_block->is_free = true;
                    new_block->next = NULL;

                    heap_block_t *last_block = heap_start;
                    while (last_block && last_block->next)
                    {
                        last_block = last_block->next;
                    }

                    if (last_block)
                    {
                        last_block->next = new_block;
                    }

                    heap_total_size += transfer_size * PAGE_SIZE;
                    heap_free_bytes += new_block->size;
                }

                LOG_OK("hybrid_heap_grow: Heap dinámico expandido con paginación en tiempo real");
                print("  Páginas transferidas: ");
                print_uint32(transfer_size);
                print(" (");
                print_uint32(heap_total_size / 1024);
                print(" KB total)\n");

                return 0;
            }
        }

        // Si llegamos aquí, la paginación en tiempo real falló
        LOG_WARN("hybrid_heap_grow: Paginación en tiempo real falló, activando recuperación resiliente");

        // ESTRATEGIA DE RECUPERACIÓN: No deshabilitar dinámico inmediatamente
        if (start_recovery_mode() == 0)
        {
            print("hybrid_heap_grow: 🔄 Iniciando creación de pool de recuperación en segundo plano\n");

            // Crear pool de páginas mientras usamos fallback
            if (create_recovery_pool(additional_pages + 4) == 0)
            {
                print("hybrid_heap_grow: 🎯 Pool de recuperación listo, intentando rehabilitar heap dinámico\n");

                // Intentar recuperar el heap dinámico
                if (attempt_dynamic_recovery() == 0)
                {
                    print("hybrid_heap_grow: 🎉 ¡RECUPERACIÓN EXITOSA! Volviendo a heap dinámico\n");

                    // Procesar el resto de páginas necesarias con el heap dinámico recuperado
                    if (hybrid_heap.paging.prealloc_count > 0)
                    {
                        size_t transferred_pages = 4; // Páginas que se transfirieron en attempt_dynamic_recovery
                        size_t remaining_pages = (additional_pages > transferred_pages) ? (additional_pages - transferred_pages) : 0;

                        if (remaining_pages > 0 && remaining_pages <= hybrid_heap.paging.prealloc_count)
                        {
                            print("hybrid_heap_grow: 🚀 Transfiriendo páginas restantes (");
                            print_uint32(remaining_pages);
                            print(")\n");

                            if (transfer_prealloc_to_heap(remaining_pages) == 0)
                            {
                                LOG_OK("hybrid_heap_grow: Todas las páginas transferidas con recuperación resiliente");
                                return 0;
                            }
                        }
                    }

                    return 0; // Recuperación exitosa
                }
            }

            print("hybrid_heap_grow: ⏳ Recuperación en progreso, usando fallback temporal\n");
        }

        // Temporalmente deshabilitar dinámico mientras se crea el pool
        hybrid_heap.dynamic_enabled = false;
    }

fallback_static:

    // FALLBACK 1: Usar heap estático
    if (heap_start == NULL || ((uintptr_t)heap_start < hybrid_heap.static_start ||
                               (uintptr_t)heap_start >= hybrid_heap.static_end))
    {
        print("hybrid_heap_grow: Activando heap estático como fallback principal\n");

        // Migrar a heap estático
        heap_block_t *static_block = (heap_block_t *)hybrid_heap.static_start;
        static_block->size = HEAP_STATIC_SIZE - sizeof(heap_block_t);
        static_block->magic = HEAP_MAGIC;
        static_block->is_free = true;
        static_block->next = heap_start; // Conectar con heap dinámico si existe
        static_block->prev = NULL;

        if (heap_start)
        {
            heap_start->prev = static_block;
        }

        heap_start = static_block;
        heap_total_size += HEAP_STATIC_SIZE;
        heap_free_bytes += static_block->size;

        LOG_OK("hybrid_heap_grow: Heap estático activado como fallback");
        return 0;
    }

    // FALLBACK 2: Usar heap fallback extra
    if (!hybrid_heap.fallback_used)
    {
        print("hybrid_heap_grow: Activando heap fallback extra (512KB)\n");

        // Crear nuevo bloque en el heap fallback
        heap_block_t *fallback_block = (heap_block_t *)hybrid_heap.fallback_start;
        fallback_block->size = HEAP_STATIC_FALLBACK_SIZE - sizeof(heap_block_t);
        fallback_block->magic = HEAP_MAGIC;
        fallback_block->is_free = true;
        fallback_block->next = NULL;

        // Conectar con la lista de bloques
        heap_block_t *last_block = heap_start;
        while (last_block && last_block->next)
        {
            last_block = last_block->next;
        }

        if (last_block)
        {
            last_block->next = fallback_block;
        }

        // Actualizar estadísticas
        heap_total_size += HEAP_STATIC_FALLBACK_SIZE;
        heap_free_bytes += fallback_block->size;

        hybrid_heap.fallback_used = true;

        LOG_OK("hybrid_heap_grow: Heap fallback extra activado exitosamente");
        print("  Tamaño total del sistema: ");
        print_uint32(heap_total_size / 1024);
        print(" KB\n");

        return 0;
    }

    // Si llegamos aquí, no hay más opciones
    LOG_ERR("hybrid_heap_grow: No hay más memoria disponible en el sistema");
    print("  Heap dinámico: ");
    print(hybrid_heap.dynamic_enabled ? "ACTIVO" : "AGOTADO");
    print("\n  Heap estático: USADO\n  Heap fallback: USADO\n");
    return -1;
}

// ===============================================================================
// IMPLEMENTACIÓN DE KALLOC
// ===============================================================================

void *kmalloc_impl(size_t size)
{
    if (!hybrid_heap.initialized)
    {
        LOG_ERR("kmalloc_impl: Heap no inicializado");
        return NULL;
    }

    if (size == 0)
    {
        return NULL;
    }

    // Alinear tamaño a múltiplo de 8
    size = (size + 7) & ~7;

    // RECUPERACIÓN RESILIENTE: Si estamos en modo de recuperación, intentar rehabilitar heap dinámico
    if (hybrid_heap.paging.recovery_mode && hybrid_heap.paging.recovery_pool_ready)
    {
        print("kmalloc_impl: 🔄 Detectado modo de recuperación, intentando rehabilitar heap dinámico\n");

        if (attempt_dynamic_recovery() == 0)
        {
            print("kmalloc_impl: 🎉 ¡Heap dinámico rehabilitado! Usando memoria dinámica\n");
            // Continuar con la lógica normal, ahora con heap dinámico activo
        }
        else
        {
            print("kmalloc_impl: ⏳ Intento de recuperación falló, continuando con fallback\n");

            // Crear más páginas para futuros intentos
            if (hybrid_heap.paging.prealloc_count < MAX_PREALLOC_PAGES / 2)
            {
                print("kmalloc_impl: 📦 Creando más páginas para futuras recuperaciones\n");
                create_recovery_pool(RECOVERY_BATCH_SIZE);
            }
        }
    }

    // Verificar si necesitamos crecer el heap
    if (heap_free_bytes < size)
    {
        size_t needed_pages = (size / PAGE_SIZE) + 1;
        if (needed_pages < 4)
        { // Mínimo 4 páginas (16KB)
            needed_pages = 4;
        }

        if (hybrid_heap_grow(needed_pages) != 0)
        {
            LOG_ERR("kmalloc_impl: No se pudo crecer el heap");
            return NULL;
        }
    }

    // Buscar bloque libre
    heap_block_t *current = heap_start;
    while (current)
    {
        if (current->is_free && current->size >= size)
        {
            // Dividir bloque si es necesario
            if (current->size > size + sizeof(heap_block_t) + MIN_BLOCK_SIZE)
            {
                split_block(current, size);
            }

            // Marcar como usado
            current->is_free = false;

            // Actualizar estadísticas
            heap_used_bytes += current->size;
            heap_free_bytes -= current->size;

            // Retornar puntero a la memoria usable
            return (void *)((uint8_t *)current + sizeof(heap_block_t));
        }
        current = current->next;
    }

    LOG_ERR("kmalloc_impl: No se encontró bloque libre");
    return NULL;
}

// ===============================================================================
// IMPLEMENTACIÓN DE KFREE
// ===============================================================================

void kfree_impl(void *ptr)
{
    if (!is_valid_heap_pointer(ptr))
    {
        LOG_ERR("kfree_impl: Puntero inválido");
        return;
    }

    heap_block_t *block = (heap_block_t *)((uint8_t *)ptr - sizeof(heap_block_t));

    if (block->magic != HEAP_MAGIC)
    {
        LOG_ERR("kfree_impl: Bloque corrupto");
        return;
    }

    // Marcar como libre
    block->is_free = true;

    // Actualizar estadísticas
    heap_used_bytes -= block->size;
    heap_free_bytes += block->size;

    // Intentar fusionar con el siguiente bloque libre
    if (block->next && block->next->is_free)
    {
        block->size += sizeof(heap_block_t) + block->next->size;
        block->next = block->next->next;
        if (block->next)
        {
            block->next->prev = block;
        }
    }

    // Intentar fusionar con el bloque anterior libre
    if (block->prev && block->prev->is_free)
    {
        block->prev->size += sizeof(heap_block_t) + block->size;
        block->prev->next = block->next;
        if (block->next)
        {
            block->next->prev = block->prev;
        }
    }
}

// ===============================================================================
// IMPLEMENTACIÓN DE KREALLOC
// ===============================================================================

void *krealloc(void *ptr, size_t new_size)
{
    // Caso 1: ptr es NULL → actúa como malloc
    if (!ptr)
    {
        return kmalloc_impl(new_size);
    }

    // Caso 2: new_size es 0 → actúa como free
    if (new_size == 0)
    {
        kfree_impl(ptr);
        return NULL;
    }

    // Validar puntero
    if (!is_valid_heap_pointer(ptr))
    {
        LOG_ERR("krealloc: Puntero inválido");
        return NULL;
    }

    // Obtener bloque actual
    heap_block_t *current_block = (heap_block_t *)((uint8_t *)ptr - sizeof(heap_block_t));

    if (current_block->magic != HEAP_MAGIC)
    {
        LOG_ERR("krealloc: Bloque corrupto");
        return NULL;
    }

    size_t old_size = current_block->size;

    // Alinear nuevo tamaño
    new_size = (new_size + 7) & ~7;

    // Caso 3: El nuevo tamaño cabe en el bloque actual
    if (new_size <= old_size)
    {
        // Si es mucho más chico, dividir el bloque
        if (old_size > new_size + sizeof(heap_block_t) + MIN_BLOCK_SIZE)
        {
            split_block(current_block, new_size);

            // Actualizar estadísticas
            size_t freed_bytes = old_size - new_size;
            heap_used_bytes -= freed_bytes;
            heap_free_bytes += freed_bytes;
        }
        return ptr; // Retornar el mismo puntero
    }

    // Caso 4: Intentar expandir el bloque actual
    // Verificar si el siguiente bloque está libre y es suficiente
    heap_block_t *next_block = current_block->next;
    if (next_block && next_block->is_free)
    {
        size_t combined_size = old_size + sizeof(heap_block_t) + next_block->size;

        if (combined_size >= new_size)
        {
            // Fusionar con el siguiente bloque
            current_block->size += sizeof(heap_block_t) + next_block->size;
            current_block->next = next_block->next;

            if (next_block->next)
            {
                next_block->next->prev = current_block;
            }

            // Actualizar estadísticas
            heap_used_bytes += (current_block->size - old_size);
            heap_free_bytes -= (current_block->size - old_size);

            // Si el bloque combinado es muy grande, dividirlo
            if (current_block->size > new_size + sizeof(heap_block_t) + MIN_BLOCK_SIZE)
            {
                split_block(current_block, new_size);

                size_t excess = current_block->size - new_size;
                heap_used_bytes -= excess;
                heap_free_bytes += excess;
            }

            return ptr; // Retornar el mismo puntero
        }
    }

    // Caso 5: No se puede expandir → allocar nuevo bloque y copiar
    void *new_ptr = kmalloc_impl(new_size);
    if (!new_ptr)
    {
        return NULL; // No hay memoria
    }

    // Copiar datos del bloque viejo al nuevo
    size_t copy_size = (old_size < new_size) ? old_size : new_size;
    memcpy(new_ptr, ptr, copy_size);

    // Liberar bloque viejo
    kfree_impl(ptr);

    return new_ptr;
}

// ===============================================================================
// FUNCIONES DE DEBUG
// ===============================================================================

void debug_heap_allocator(void)
{
    if (!hybrid_heap.initialized)
    {
        print_colored("=== HEAP ALLOCATOR STATE ===\n", VGA_COLOR_RED, VGA_COLOR_BLACK);
        print("Heap no inicializado\n");
        return;
    }

    print_colored("=== HEAP ALLOCATOR STATE ===\n", VGA_COLOR_CYAN, VGA_COLOR_BLACK);

    print("Heap híbrido:\n");
    print("  Heap estático: 0x");
    print_hex_compact(hybrid_heap.static_start);
    print(" - 0x");
    print_hex_compact(hybrid_heap.static_end);
    print(" (128KB)\n");

    print("  Heap fallback: 0x");
    print_hex_compact(hybrid_heap.fallback_start);
    print(" - 0x");
    print_hex_compact(hybrid_heap.fallback_end);
    print(" (512KB) ");
    print(hybrid_heap.fallback_used ? "[ACTIVO]" : "[RESERVA]");
    print("\n");

    print("  Heap dinámico: 0x");
    print_hex_compact(hybrid_heap.dynamic_start);
    print(" - 0x");
    print_hex_compact(hybrid_heap.dynamic_end);
    print(" ");
    print(hybrid_heap.dynamic_enabled ? "[HABILITADO]" : "[DESHABILITADO]");
    print("\n");

    print("  Páginas asignadas: ");
    print_uint32(hybrid_heap.page_count);
    print(" / ");
    print_uint32(hybrid_heap.max_pages);
    print(" (");
    print_uint32(HEAP_SAFE_PAGES);
    print(" disponibles)\n");

    print("  Tamaño total: ");
    print_uint32(heap_total_size / 1024);
    print(" KB\n");

    print("  Bytes usados: ");
    print_uint32(heap_used_bytes);
    print(" (");
    print_uint32((heap_used_bytes * 100) / heap_total_size);
    print("%)\n");

    print("  Bytes libres: ");
    print_uint32(heap_free_bytes);
    print(" (");
    print_uint32((heap_free_bytes * 100) / heap_total_size);
    print("%)\n");

    // Mostrar bloques (limitado a 10 para no saturar)
    print("\nBloques de memoria:\n");
    heap_block_t *current = heap_start;
    int block_count = 0;

    while (current && block_count < 10)
    {
        print("  Bloque ");
        print_uint32(block_count);
        print(": ");
        print_uint32(current->size);
        print(" bytes ");
        print(current->is_free ? "[LIBRE]" : "[USADO]");
        print(" @ 0x");
        print_hex_compact((uintptr_t)current);

        // Indicar si es estático, fallback o dinámico
        if ((uintptr_t)current >= hybrid_heap.static_start &&
            (uintptr_t)current < hybrid_heap.static_end)
        {
            print(" [ESTÁTICO]");
        }
        else if ((uintptr_t)current >= hybrid_heap.fallback_start &&
                 (uintptr_t)current < hybrid_heap.fallback_end)
        {
            print(" [FALLBACK]");
        }
        else
        {
            print(" [DINÁMICO]");
        }
        print("\n");

        current = current->next;
        block_count++;
    }

    if (current)
    {
        print("  ... (más bloques)\n");
    }

    print("\n");
}

// ===============================================================================
// FUNCIONES DE LIMPIEZA
// ===============================================================================

void heap_allocator_cleanup(void)
{
    if (!hybrid_heap.initialized)
    {
        return;
    }

    print("heap_allocator_cleanup: Limpiando heap híbrido\n");

    // Liberar todas las páginas físicas del heap dinámico
    for (uint32_t i = 0; i < hybrid_heap.page_count; i++)
    {
        if (hybrid_heap.physical_pages[i] != 0)
        {
            free_physical_page(hybrid_heap.physical_pages[i]);
            hybrid_heap.physical_pages[i] = 0;
        }
    }

    // Resetear estado
    memset(&hybrid_heap, 0, sizeof(hybrid_heap_t));
    heap_start = NULL;
    heap_total_size = 0;
    heap_used_bytes = 0;
    heap_free_bytes = 0;

    LOG_OK("heap_allocator_cleanup: Heap híbrido limpiado exitosamente");
}

// ===============================================================================
// FUNCIONES DE COMPATIBILIDAD
// ===============================================================================

// Función wrapper para compatibilidad
void *realloc(void *ptr, size_t size)
{
    return krealloc(ptr, size);
}

// Función wrapper para compatibilidad
void *malloc(size_t size)
{
    return kmalloc_impl(size);
}

// Función wrapper para compatibilidad
void free(void *ptr)
{
    kfree_impl(ptr);
}

// ===============================================================================
// FUNCIONES DE ESTADÍSTICAS
// ===============================================================================

void heap_get_stats(uint32_t *total_pages, uint32_t *used_pages,
                    size_t *total_bytes, size_t *used_bytes, size_t *free_bytes)
{
    if (!hybrid_heap.initialized)
    {
        if (total_pages)
            *total_pages = 0;
        if (used_pages)
            *used_pages = 0;
        if (total_bytes)
            *total_bytes = 0;
        if (used_bytes)
            *used_bytes = 0;
        if (free_bytes)
            *free_bytes = 0;
        return;
    }

    if (total_pages)
        *total_pages = hybrid_heap.page_count;
    if (used_pages)
        *used_pages = hybrid_heap.page_count; // All pages are "used" by the heap
    if (total_bytes)
        *total_bytes = heap_total_size;
    if (used_bytes)
        *used_bytes = heap_used_bytes;
    if (free_bytes)
        *free_bytes = heap_free_bytes;
}

// ===============================================================================
// FUNCIONES DE INTERFAZ DEL SISTEMA DE MEMORIA
// ===============================================================================

void memory_init(void)
{
    if (memory_system_initialized)
    {
        LOG_WARN("Memory system already initialized");
        return;
    }

    print_colored("=== INICIALIZANDO SISTEMA DE MEMORIA ===\n", VGA_COLOR_CYAN, VGA_COLOR_BLACK);

    // 1. Inicializar el allocator de páginas físicas
    physical_allocator_init();
    LOG_OK("Physical allocator inicializado");

    // 2. Inicializar el heap allocator (kmalloc/kfree)
    heap_allocator_init();
    LOG_OK("Heap allocator inicializado");

    memory_system_initialized = 1;
    print_success("Sistema de memoria completamente inicializado\n");
}

// Función wrapper para kmalloc
void *kmalloc(size_t size)
{
    if (!memory_system_initialized)
    {
        memory_init();
    }

    return kmalloc_impl(size);
}

// Función wrapper para kfree
void kfree(void *ptr)
{
    if (!memory_system_initialized)
    {
        LOG_ERR("kfree: Memory system not initialized");
        return;
    }

    kfree_impl(ptr);
}

// Función wrapper para krealloc
void *krealloc_wrapper(void *ptr, size_t size)
{
    if (!memory_system_initialized)
    {
        memory_init();
    }

    return krealloc(ptr, size);
}

// Función wrapper para map_page
int map_page(uintptr_t virt_addr, uintptr_t phys_addr, uint32_t flags)
{
    if (!memory_system_initialized)
    {
        LOG_ERR("map_page: Memory system not initialized");
        return -1;
    }

    // Si phys_addr es 0, allocar una página física automáticamente
    if (phys_addr == 0)
    {
        phys_addr = alloc_physical_page();
        if (phys_addr == 0)
        {
            LOG_ERR("map_page: No se pudo allocar página física");
            return -2;
        }
    }

    // Llamar a la función específica de arquitectura
    int result = arch_map_page(virt_addr, phys_addr, flags);

    if (result != 0)
    {
        // Si falló el mapeo y nosotros allocamos la página, liberarla
        if (phys_addr != 0)
        {
            free_physical_page(phys_addr);
        }
        LOG_ERR("map_page: Error en mapeo específico de arquitectura");
        return result;
    }

    return 0;
}

// Función wrapper para unmap_page
int unmap_page(uintptr_t virt_addr)
{
    if (!memory_system_initialized)
    {
        LOG_ERR("unmap_page: Memory system not initialized");
        return -1;
    }

    // Obtener dirección física antes de unmapear
    uintptr_t phys_addr = arch_virt_to_phys(virt_addr);

    // Unmapear usando función específica de arquitectura
    int result = arch_unmap_page(virt_addr);

    if (result == 0 && phys_addr != 0)
    {
        // Si el unmapeo fue exitoso, liberar la página física
        free_physical_page(phys_addr);
    }

    return result;
}

// Función wrapper para virt_to_phys
uintptr_t virt_to_phys(uintptr_t virt_addr)
{
    if (!memory_system_initialized)
    {
        return 0;
    }

    return arch_virt_to_phys(virt_addr);
}