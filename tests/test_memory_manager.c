// ===============================================================================
// IR0 KERNEL - MEMORY MANAGER TEST
// ===============================================================================

#include <memory_manager.h>
#include <ir0/print.h>
#include <string.h>

void test_memory_manager_basic(void) {
    print("=== TESTING MEMORY MANAGER BASIC FUNCTIONALITY ===\n");
    
    // Test 1: Inicialización
    print("Test 1: Inicializando Memory Manager...\n");
    int result = memory_manager_init();
    if (result != 0) {
        print("ERROR: Memory Manager initialization failed\n");
        return;
    }
    print("SUCCESS: Memory Manager initialized\n");
    
    // Test 2: Estadísticas iniciales
    print("\nTest 2: Verificando estadísticas iniciales...\n");
    memory_print_stats();
    
    // Test 3: Asignación básica
    print("\nTest 3: Probando asignación básica...\n");
    void *ptr1 = memory_alloc(1024);
    if (!ptr1) {
        print("ERROR: memory_alloc(1024) failed\n");
        return;
    }
    print("SUCCESS: Allocated 1024 bytes at %p\n", ptr1);
    
    // Test 4: Asignación múltiple
    print("\nTest 4: Probando asignación múltiple...\n");
    void *ptr2 = memory_alloc(512);
    void *ptr3 = memory_alloc(2048);
    if (!ptr2 || !ptr3) {
        print("ERROR: Multiple allocation failed\n");
        return;
    }
    print("SUCCESS: Allocated 512 bytes at %p\n", ptr2);
    print("SUCCESS: Allocated 2048 bytes at %p\n", ptr3);
    
    // Test 5: calloc
    print("\nTest 5: Probando calloc...\n");
    void *ptr4 = memory_calloc(10, 100);
    if (!ptr4) {
        print("ERROR: memory_calloc failed\n");
        return;
    }
    print("SUCCESS: calloc allocated 1000 bytes at %p\n", ptr4);
    
    // Verificar que calloc inicializa a cero
    char *test_ptr = (char *)ptr4;
    bool is_zero = true;
    for (int i = 0; i < 1000; i++) {
        if (test_ptr[i] != 0) {
            is_zero = false;
            break;
        }
    }
    if (is_zero) {
        print("SUCCESS: calloc properly initialized memory to zero\n");
    } else {
        print("ERROR: calloc did not initialize memory to zero\n");
    }
    
    // Test 6: realloc
    print("\nTest 6: Probando realloc...\n");
    void *ptr5 = memory_alloc(256);
    if (!ptr5) {
        print("ERROR: memory_alloc(256) failed\n");
        return;
    }
    
    // Llenar con datos
    memset(ptr5, 0xAA, 256);
    
    // Redimensionar
    void *ptr6 = memory_realloc(ptr5, 512);
    if (!ptr6) {
        print("ERROR: memory_realloc failed\n");
        return;
    }
    print("SUCCESS: realloc from 256 to 512 bytes\n");
    
    // Verificar que los datos se preservaron
    char *test_realloc = (char *)ptr6;
    bool data_preserved = true;
    for (int i = 0; i < 256; i++) {
        if (test_realloc[i] != 0xAA) {
            data_preserved = false;
            break;
        }
    }
    if (data_preserved) {
        print("SUCCESS: realloc preserved original data\n");
    } else {
        print("ERROR: realloc did not preserve original data\n");
    }
    
    // Test 7: Estadísticas después de asignaciones
    print("\nTest 7: Estadísticas después de asignaciones...\n");
    memory_print_stats();
    
    // Test 8: Liberación
    print("\nTest 8: Probando liberación...\n");
    memory_free(ptr1);
    memory_free(ptr2);
    memory_free(ptr3);
    memory_free(ptr4);
    memory_free(ptr6);
    print("SUCCESS: All memory freed\n");
    
    // Test 9: Estadísticas después de liberación
    print("\nTest 9: Estadísticas después de liberación...\n");
    memory_print_stats();
    
    // Test 10: Validación de punteros
    print("\nTest 10: Probando validación de punteros...\n");
    void *valid_ptr = memory_alloc(64);
    if (valid_ptr) {
        bool is_valid = memory_validate_ptr(valid_ptr);
        print("Valid pointer validation: %s\n", is_valid ? "SUCCESS" : "ERROR");
        memory_free(valid_ptr);
    }
    
    bool is_invalid = memory_validate_ptr((void *)0x12345678);
    print("Invalid pointer validation: %s\n", !is_invalid ? "SUCCESS" : "ERROR");
    
    print("\n=== MEMORY MANAGER BASIC TEST COMPLETED ===\n");
}

void test_memory_manager_zones(void) {
    print("=== TESTING MEMORY MANAGER ZONES ===\n");
    
    // Test 1: Obtener zonas
    print("Test 1: Obteniendo zonas de memoria...\n");
    
    memory_zone_t *dma_zone = memory_get_zone(MEMORY_ZONE_DMA);
    if (dma_zone) {
        print("SUCCESS: DMA zone - Start: 0x%lx, End: 0x%lx, Size: %zu MB\n",
              dma_zone->start_addr, dma_zone->end_addr, dma_zone->total_size / (1024*1024));
    } else {
        print("ERROR: Could not get DMA zone\n");
    }
    
    memory_zone_t *normal_zone = memory_get_zone(MEMORY_ZONE_NORMAL);
    if (normal_zone) {
        print("SUCCESS: Normal zone - Start: 0x%lx, End: 0x%lx, Size: %zu MB\n",
              normal_zone->start_addr, normal_zone->end_addr, normal_zone->total_size / (1024*1024));
    } else {
        print("ERROR: Could not get Normal zone\n");
    }
    
    memory_zone_t *highmem_zone = memory_get_zone(MEMORY_ZONE_HIGHMEM);
    if (highmem_zone) {
        print("SUCCESS: HighMem zone - Start: 0x%lx, End: 0x%lx, Size: %zu MB\n",
              highmem_zone->start_addr, highmem_zone->end_addr, highmem_zone->total_size / (1024*1024));
    } else {
        print("ERROR: Could not get HighMem zone\n");
    }
    
    // Test 2: Búsqueda de zona por dirección
    print("\nTest 2: Búsqueda de zona por dirección...\n");
    
    memory_zone_t *zone1 = memory_get_zone_for_addr(0x00000000);
    if (zone1 && zone1->type == MEMORY_ZONE_DMA) {
        print("SUCCESS: Address 0x00000000 maps to DMA zone\n");
    } else {
        print("ERROR: Address 0x00000000 zone mapping failed\n");
    }
    
    memory_zone_t *zone2 = memory_get_zone_for_addr(0x10000000);
    if (zone2 && zone2->type == MEMORY_ZONE_NORMAL) {
        print("SUCCESS: Address 0x10000000 maps to Normal zone\n");
    } else {
        print("ERROR: Address 0x10000000 zone mapping failed\n");
    }
    
    memory_zone_t *zone3 = memory_get_zone_for_addr(0x40000000);
    if (zone3 && zone3->type == MEMORY_ZONE_HIGHMEM) {
        print("SUCCESS: Address 0x40000000 maps to HighMem zone\n");
    } else {
        print("ERROR: Address 0x40000000 zone mapping failed\n");
    }
    
    // Test 3: Asignación en zonas específicas
    print("\nTest 3: Asignación en zonas específicas...\n");
    
    void *dma_ptr = memory_alloc_in_zone(MEMORY_ZONE_DMA, 1024);
    if (dma_ptr) {
        print("SUCCESS: Allocated 1024 bytes in DMA zone at %p\n", dma_ptr);
        memory_free_in_zone(MEMORY_ZONE_DMA, dma_ptr);
    } else {
        print("ERROR: DMA zone allocation failed\n");
    }
    
    void *normal_ptr = memory_alloc_in_zone(MEMORY_ZONE_NORMAL, 1024);
    if (normal_ptr) {
        print("SUCCESS: Allocated 1024 bytes in Normal zone at %p\n", normal_ptr);
        memory_free_in_zone(MEMORY_ZONE_NORMAL, normal_ptr);
    } else {
        print("ERROR: Normal zone allocation failed\n");
    }
    
    print("\n=== MEMORY MANAGER ZONES TEST COMPLETED ===\n");
}

void test_memory_manager_configuration(void) {
    print("=== TESTING MEMORY MANAGER CONFIGURATION ===\n");
    
    // Test 1: Configuración de allocator
    print("Test 1: Configurando allocator por defecto...\n");
    memory_set_default_allocator(ALLOCATOR_BUMP);
    print("SUCCESS: Default allocator set to BUMP\n");
    
    // Test 2: Configuración de zonas
    print("\nTest 2: Configurando allocator de zona...\n");
    memory_set_zone_allocator(MEMORY_ZONE_DMA, ALLOCATOR_BUMP);
    memory_set_zone_allocator(MEMORY_ZONE_NORMAL, ALLOCATOR_BUMP);
    memory_set_zone_allocator(MEMORY_ZONE_HIGHMEM, ALLOCATOR_BUMP);
    print("SUCCESS: Zone allocators configured\n");
    
    // Test 3: Habilitar/deshabilitar características
    print("\nTest 3: Configurando características...\n");
    memory_enable_slabs(true);
    memory_enable_buddy(true);
    memory_enable_debug(true);
    print("SUCCESS: Features configured\n");
    
    // Test 4: Callbacks
    print("\nTest 4: Configurando callbacks...\n");
    memory_set_debug_callback(NULL);
    memory_set_error_callback(NULL);
    print("SUCCESS: Callbacks configured\n");
    
    print("\n=== MEMORY MANAGER CONFIGURATION TEST COMPLETED ===\n");
}

void test_memory_manager_stress(void) {
    print("=== TESTING MEMORY MANAGER STRESS ===\n");
    
    const int num_allocations = 100;
    void *ptrs[num_allocations];
    
    // Test 1: Asignación masiva
    print("Test 1: Asignación masiva (%d bloques)...\n", num_allocations);
    
    for (int i = 0; i < num_allocations; i++) {
        size_t size = (i % 10 + 1) * 64; // Tamaños variados
        ptrs[i] = memory_alloc(size);
        if (!ptrs[i]) {
            print("ERROR: Failed to allocate %zu bytes at iteration %d\n", size, i);
            return;
        }
        
        // Llenar con datos únicos
        memset(ptrs[i], i % 256, size);
    }
    print("SUCCESS: All %d allocations completed\n", num_allocations);
    
    // Test 2: Verificación de datos
    print("\nTest 2: Verificando datos...\n");
    bool data_correct = true;
    for (int i = 0; i < num_allocations; i++) {
        size_t size = (i % 10 + 1) * 64;
        char *data = (char *)ptrs[i];
        
        for (size_t j = 0; j < size; j++) {
            if (data[j] != (i % 256)) {
                data_correct = false;
                print("ERROR: Data corruption at allocation %d, byte %zu\n", i, j);
                break;
            }
        }
        if (!data_correct) break;
    }
    
    if (data_correct) {
        print("SUCCESS: All data verified correctly\n");
    }
    
    // Test 3: Reasignación
    print("\nTest 3: Reasignación de bloques...\n");
    for (int i = 0; i < num_allocations; i += 2) { // Solo bloques pares
        size_t new_size = ((i + 5) % 10 + 1) * 128;
        void *new_ptr = memory_realloc(ptrs[i], new_size);
        if (new_ptr) {
            ptrs[i] = new_ptr;
            // Llenar con nuevos datos
            memset(new_ptr, (i + 100) % 256, new_size);
        } else {
            print("ERROR: Realloc failed for allocation %d\n", i);
        }
    }
    print("SUCCESS: Reallocation completed\n");
    
    // Test 4: Estadísticas intermedias
    print("\nTest 4: Estadísticas intermedias...\n");
    memory_print_stats();
    
    // Test 5: Liberación masiva
    print("\nTest 5: Liberación masiva...\n");
    for (int i = 0; i < num_allocations; i++) {
        memory_free(ptrs[i]);
    }
    print("SUCCESS: All %d blocks freed\n", num_allocations);
    
    // Test 6: Estadísticas finales
    print("\nTest 6: Estadísticas finales...\n");
    memory_print_stats();
    
    print("\n=== MEMORY MANAGER STRESS TEST COMPLETED ===\n");
}

void run_memory_manager_tests(void) {
    print("========================================\n");
    print("IR0 KERNEL - MEMORY MANAGER TEST SUITE\n");
    print("========================================\n\n");
    
    test_memory_manager_basic();
    test_memory_manager_zones();
    test_memory_manager_configuration();
    test_memory_manager_stress();
    
    print("\n========================================\n");
    print("ALL MEMORY MANAGER TESTS COMPLETED\n");
    print("========================================\n");
}
