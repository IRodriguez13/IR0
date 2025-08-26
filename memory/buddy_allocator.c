// ===============================================================================
// IR0 KERNEL - BUDDY ALLOCATOR IMPLEMENTATION
// ===============================================================================

#include <buddy_allocator.h>
#include <bump_allocator.h>
#include <ir0/print.h>
#include <ir0/panic/panic.h>
#include <string.h>

// ===============================================================================
// CONSTANTS AND CONFIGURATION
// ===============================================================================

#define BUDDY_MIN_ORDER 4   // 2^4 = 16 bytes mínimo
#define BUDDY_MAX_ORDER 20  // 2^20 = 1MB máximo
#define BUDDY_ALIGNMENT 16

// ===============================================================================
// GLOBAL VARIABLES
// ===============================================================================

static buddy_allocator_t *global_buddy = NULL;
static bool buddy_allocator_initialized = false;

// ===============================================================================
// UTILITY FUNCTIONS
// ===============================================================================

uint32_t buddy_get_order(size_t size) {
    if (size == 0) return BUDDY_MIN_ORDER;
    
    // Encontrar el orden mínimo que puede contener el tamaño
    uint32_t order = BUDDY_MIN_ORDER;
    size_t block_size = 1 << order;
    
    while (block_size < size && order < BUDDY_MAX_ORDER) {
        order++;
        block_size = 1 << order;
    }
    
    return order;
}

size_t buddy_get_block_size(uint32_t order) {
    if (order > BUDDY_MAX_ORDER) return 0;
    return 1 << order;
}

bool buddy_is_valid_ptr(buddy_allocator_t *buddy, void *ptr) {
    if (!buddy || !ptr) return false;
    
    uintptr_t addr = (uintptr_t)ptr;
    return (addr >= buddy->start_addr && 
            addr < buddy->start_addr + buddy->total_size);
}

size_t buddy_get_allocated_size(buddy_allocator_t *buddy, void *ptr) {
    if (!buddy_is_valid_ptr(buddy, ptr)) return 0;
    
    // Por ahora retornamos un tamaño estimado
    // En una implementación completa, buscaríamos en el bitmap
    return 1024; // Estimación básica
}

// ===============================================================================
// BUDDY BLOCK MANAGEMENT
// ===============================================================================

static buddy_block_t *buddy_block_create(uintptr_t start_addr, uint32_t order) {
    buddy_block_t *block = kmalloc(sizeof(buddy_block_t));
    if (!block) return NULL;
    
    block->next = NULL;
    block->order = order;
    block->is_free = true;
    block->start_addr = start_addr;
    
    return block;
}

static void buddy_block_destroy(buddy_block_t *block) {
    if (block) {
        kfree(block);
    }
}

static void buddy_list_add(buddy_block_t **list, buddy_block_t *block) {
    if (!list || !block) return;
    
    block->next = *list;
    *list = block;
}

static buddy_block_t *buddy_list_remove(buddy_block_t **list) {
    if (!list || !*list) return NULL;
    
    buddy_block_t *block = *list;
    *list = block->next;
    block->next = NULL;
    
    return block;
}

// ===============================================================================
// BUDDY ALLOCATOR FUNCTIONS
// ===============================================================================

buddy_allocator_t *buddy_allocator_create(uintptr_t start_addr, size_t size) {
    if (size == 0) return NULL;
    
    // Crear el allocator
    buddy_allocator_t *buddy = kmalloc(sizeof(buddy_allocator_t));
    if (!buddy) return NULL;
    
    // Inicializar estructura
    buddy->start_addr = start_addr;
    buddy->total_size = size;
    buddy->max_order = buddy_get_order(size);
    
    // Inicializar listas libres
    for (int i = 0; i < 32; i++) {
        buddy->free_lists[i] = NULL;
    }
    
    // Crear bitmap básico
    size_t bitmap_size = (size / (1 << BUDDY_MIN_ORDER)) / 8;
    buddy->bitmap = kmalloc(bitmap_size);
    if (!buddy->bitmap) {
        kfree(buddy);
        return NULL;
    }
    memset(buddy->bitmap, 0, bitmap_size);
    
    // Crear bloque inicial
    buddy_block_t *initial_block = buddy_block_create(start_addr, buddy->max_order);
    if (!initial_block) {
        kfree(buddy->bitmap);
        kfree(buddy);
        return NULL;
    }
    
    buddy_list_add(&buddy->free_lists[buddy->max_order], initial_block);
    
    return buddy;
}

void buddy_allocator_destroy(buddy_allocator_t *buddy) {
    if (!buddy) return;
    
    // Liberar todos los bloques en las listas
    for (int i = 0; i < 32; i++) {
        while (buddy->free_lists[i]) {
            buddy_block_t *block = buddy_list_remove(&buddy->free_lists[i]);
            buddy_block_destroy(block);
        }
    }
    
    // Liberar bitmap
    if (buddy->bitmap) {
        kfree(buddy->bitmap);
    }
    
    kfree(buddy);
}

void *buddy_alloc(buddy_allocator_t *buddy, size_t size) {
    if (!buddy || size == 0) return NULL;
    
    uint32_t required_order = buddy_get_order(size);
    
    // Buscar un bloque del orden requerido o mayor
    uint32_t order = required_order;
    buddy_block_t *block = NULL;
    
    while (order <= buddy->max_order && !block) {
        if (buddy->free_lists[order]) {
            block = buddy_list_remove(&buddy->free_lists[order]);
            break;
        }
        order++;
    }
    
    if (!block) {
        // No hay bloques disponibles
        return NULL;
    }
    
    // Si el bloque es más grande de lo necesario, dividirlo
    while (block->order > required_order) {
        uint32_t new_order = block->order - 1;
        size_t half_size = buddy_get_block_size(new_order);
        
        // Crear el buddy del bloque actual
        buddy_block_t *buddy_block = buddy_block_create(
            block->start_addr + half_size, new_order);
        
        if (!buddy_block) {
            // Si no podemos crear el buddy, devolver el bloque original
            buddy_list_add(&buddy->free_lists[block->order], block);
            return NULL;
        }
        
        // Reducir el orden del bloque actual
        block->order = new_order;
        
        // Agregar el buddy a la lista libre
        buddy_list_add(&buddy->free_lists[new_order], buddy_block);
    }
    
    // Marcar el bloque como ocupado
    block->is_free = false;
    
    return (void *)block->start_addr;
}

void buddy_free(buddy_allocator_t *buddy, void *ptr) {
    if (!buddy || !ptr) return;
    
    uintptr_t addr = (uintptr_t)ptr;
    
    // Encontrar el bloque correspondiente
    // Por simplicidad, asumimos que conocemos el orden
    // En una implementación completa, buscaríamos en el bitmap
    
    uint32_t order = BUDDY_MIN_ORDER; // Orden por defecto
    size_t block_size = buddy_get_block_size(order);
    
    // Calcular la dirección de inicio del bloque
    uintptr_t block_start = (addr / block_size) * block_size;
    
    // Crear un nuevo bloque libre
    buddy_block_t *block = buddy_block_create(block_start, order);
    if (!block) return;
    
    // Intentar coalescer con buddies
    bool coalesced = false;
    while (order < buddy->max_order && !coalesced) {
        // Calcular la dirección del buddy
        uintptr_t buddy_addr = block_start ^ block_size;
        
        // Buscar el buddy en la lista libre
        buddy_block_t *buddy_block = NULL;
        buddy_block_t **list = &buddy->free_lists[order];
        
        while (*list) {
            if ((*list)->start_addr == buddy_addr) {
                buddy_block = buddy_list_remove(list);
                break;
            }
            list = &(*list)->next;
        }
        
        if (buddy_block) {
            // Coalescer los bloques
            buddy_block_destroy(block);
            buddy_block_destroy(buddy_block);
            
            // Crear un bloque más grande
            order++;
            block_size = buddy_get_block_size(order);
            block_start = (block_start / block_size) * block_size;
            block = buddy_block_create(block_start, order);
            
            if (!block) return;
        } else {
            coalesced = true;
        }
    }
    
    // Agregar el bloque a la lista libre
    buddy_list_add(&buddy->free_lists[order], block);
}

void buddy_get_stats(buddy_allocator_t *buddy, size_t *total, size_t *free) {
    if (total) *total = buddy ? buddy->total_size : 0;
    if (free) {
        size_t free_size = 0;
        if (buddy) {
            for (int i = 0; i < 32; i++) {
                buddy_block_t *block = buddy->free_lists[i];
                while (block) {
                    free_size += buddy_get_block_size(block->order);
                    block = block->next;
                }
            }
        }
        *free = free_size;
    }
}

void buddy_print_info(buddy_allocator_t *buddy) {
    if (!buddy) {
        print("Buddy allocator is NULL\n");
        return;
    }
    
    print("Buddy Allocator Info:\n");
    print("Start address: ");
    print_uint64(buddy->start_addr);
    print("\n");
    print("Total size: ");
    print_uint64(buddy->total_size);
    print(" bytes\n");
    print("Max order: ");
    print_uint64(buddy->max_order);
    print("\n");
    
    // Mostrar bloques libres por orden
    for (uint32_t i = 0; i <= buddy->max_order; i++) {
        if (buddy->free_lists[i]) {
            uint32_t count = 0;
            buddy_block_t *block = buddy->free_lists[i];
            while (block) {
                count++;
                block = block->next;
            }
            if (count > 0) {
                print("Order ");
                print_uint64(i);
                print(": ");
                print_uint64(count);
                print(" blocks (");
                print_uint64(buddy_get_block_size(i));
                print(" bytes each)\n");
            }
        }
    }
}

// ===============================================================================
// GLOBAL BUDDY ALLOCATOR FUNCTIONS
// ===============================================================================

int buddy_allocator_init(void) {
    if (buddy_allocator_initialized) {
        return 0;
    }
    
    print("Initializing buddy allocator...\n");
    
    // Crear buddy allocator global con 1MB de memoria
    global_buddy = buddy_allocator_create(0x1000000, 1024 * 1024);
    if (!global_buddy) {
        print("ERROR: Failed to create global buddy allocator\n");
        return -1;
    }
    
    buddy_allocator_initialized = true;
    print("Buddy allocator initialized successfully\n");
    
    return 0;
}

void buddy_allocator_cleanup(void) {
    if (!buddy_allocator_initialized) {
        return;
    }
    
    if (global_buddy) {
        buddy_allocator_destroy(global_buddy);
        global_buddy = NULL;
    }
    
    buddy_allocator_initialized = false;
    print("Buddy allocator cleaned up\n");
}

buddy_allocator_t *buddy_get_global_allocator(void) {
    return global_buddy;
}
