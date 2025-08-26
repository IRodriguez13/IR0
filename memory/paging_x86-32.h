#ifndef PAGING_X86_32_H
#define PAGING_X86_32_H

#include <stdint.h>
#include <stdbool.h>

// Estructuras ultra simples para paginación 32-bit
typedef struct {
    uint32_t present : 1;
    uint32_t read_write : 1;
    uint32_t user_supervisor : 1;
    uint32_t write_through : 1;
    uint32_t cache_disabled : 1;
    uint32_t accessed : 1;
    uint32_t dirty : 1;
    uint32_t page_size : 1;
    uint32_t global : 1;
    uint32_t available : 3;
    uint32_t page_address : 20;
} __attribute__((packed)) page_table_entry_t;

typedef struct {
    uint32_t present : 1;
    uint32_t read_write : 1;
    uint32_t user_supervisor : 1;
    uint32_t write_through : 1;
    uint32_t cache_disabled : 1;
    uint32_t accessed : 1;
    uint32_t available : 1;
    uint32_t page_size : 1;
    uint32_t global : 1;
    uint32_t available2 : 3;
    uint32_t page_table_address : 20;
} __attribute__((packed)) page_directory_entry_t;

// Funciones ultra simples para paginación 32-bit
void init_paging32(void);
void setup_identity_mapping32(void);
void load_page_directory32(void);
void enable_paging32(void);
void setup_and_enable_paging32(void);

// Funciones auxiliares
void set_page_entry32(page_table_entry_t *entry, uint32_t physical_addr, uint32_t flags);
void set_directory_entry32(page_directory_entry_t *entry, uint32_t page_table_addr, uint32_t flags);

// Función de verificación de estado
void verify_paging_status32(void);

#endif // PAGING_X86_32_H
