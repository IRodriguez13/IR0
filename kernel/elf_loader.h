// kernel/elf_loader.h - Header para ELF Loader básico

#pragma once

#include <stdint.h>
#include <stddef.h>

// Forward declaration
struct process;

// ===============================================================================
// FUNCIONES PRINCIPALES
// ===============================================================================

/**
 * Cargar programa ELF en memoria de proceso
 * @param path Ruta del archivo ELF
 * @param process Proceso donde cargar el programa
 * @return 0 en éxito, -1 en error
 */
int load_elf_program(const char *path, struct process *process);

/* Backwards-compatible wrapper used by callers in the kernel
 * Historically some files call elf_load_and_execute(path) which
 * loads and schedules a process. Provide the same symbol here.
 */
int elf_load_and_execute(const char *path);

// ===============================================================================
// FUNCIONES DE DEBUG
// ===============================================================================

/**
 * Mostrar información del header ELF
 * @param header Puntero al header ELF
 */
void debug_elf_header(const void *header);

/**
 * Mostrar información de program header
 * @param phdr Puntero al program header
 * @param index Índice del program header
 */
void debug_program_header(const void *phdr, int index);

// ===============================================================================
// CONSTANTES ELF
// ===============================================================================

#define ELF_MAGIC 0x464C457F // "\x7fELF"
#define PT_LOAD 1            // Segmento cargable
#define PF_R 4               // Readable
#define PF_W 2               // Writable
#define PF_X 1               // Executable

// ===============================================================================
// ESTRUCTURAS ELF (para uso externo)
// ===============================================================================

// ELF Header (64-bit)
typedef struct
{
    unsigned char e_ident[16]; // Magic number y info
    uint16_t e_type;           // Tipo de archivo
    uint16_t e_machine;        // Arquitectura
    uint32_t e_version;        // Versión ELF
    uint64_t e_entry;          // Entry point
    uint64_t e_phoff;          // Offset de program headers
    uint64_t e_shoff;          // Offset de section headers
    uint32_t e_flags;          // Flags
    uint16_t e_ehsize;         // Tamaño del header
    uint16_t e_phentsize;      // Tamaño de program header
    uint16_t e_phnum;          // Número de program headers
    uint16_t e_shentsize;      // Tamaño de section header
    uint16_t e_shnum;          // Número de section headers
    uint16_t e_shstrndx;       // Índice de string table
} elf64_header_t;

// Program Header (64-bit)
typedef struct
{
    uint32_t p_type;   // Tipo de segmento
    uint32_t p_flags;  // Flags
    uint64_t p_offset; // Offset en archivo
    uint64_t p_vaddr;  // Dirección virtual
    uint64_t p_paddr;  // Dirección física
    uint64_t p_filesz; // Tamaño en archivo
    uint64_t p_memsz;  // Tamaño en memoria
    uint64_t p_align;  // Alineación
} elf64_phdr_t;
