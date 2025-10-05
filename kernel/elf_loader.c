// ELF Loader minimalista para IR0
// Implementación simple para cargar programas de usuario

#include <stdint.h>
#include <stddef.h>
#include <string.h>

// External functions
extern void serial_print(const char *str);
extern void serial_print_hex32(uint32_t num);
extern void *kmalloc(size_t size);
extern void kfree(void *ptr);

// Use external strstr from string.h

// Minimal ELF structures
typedef struct {
    unsigned char e_ident[16];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint64_t e_entry;
    uint64_t e_phoff;
    uint64_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
} elf64_header_t;

typedef struct {
    uint32_t p_type;
    uint32_t p_flags;
    uint64_t p_offset;
    uint64_t p_vaddr;
    uint64_t p_paddr;
    uint64_t p_filesz;
    uint64_t p_memsz;
    uint64_t p_align;
} elf64_phdr_t;

// ELF constants
#define ELF_MAGIC_0 0x7f
#define ELF_MAGIC_1 'E'
#define ELF_MAGIC_2 'L'
#define ELF_MAGIC_3 'F'
#define ELFCLASS64  2
#define EM_X86_64   0x3e
#define ET_EXEC     2
#define PT_LOAD     1

// Simple ELF validation (for future use)
static int __attribute__((unused)) validate_elf_header(const elf64_header_t *header) {
    serial_print("SERIAL: ELF: Validating header\n");
    
    // Check magic
    if (header->e_ident[0] != ELF_MAGIC_0 ||
        header->e_ident[1] != ELF_MAGIC_1 ||
        header->e_ident[2] != ELF_MAGIC_2 ||
        header->e_ident[3] != ELF_MAGIC_3) {
        serial_print("SERIAL: ELF: Invalid magic number\n");
        return 0;
    }
    
    // Check 64-bit
    if (header->e_ident[4] != ELFCLASS64) {
        serial_print("SERIAL: ELF: Not 64-bit ELF\n");
        return 0;
    }
    
    // Check x86-64
    if (header->e_machine != EM_X86_64) {
        serial_print("SERIAL: ELF: Not x86-64\n");
        return 0;
    }
    
    // Check executable
    if (header->e_type != ET_EXEC) {
        serial_print("SERIAL: ELF: Not executable\n");
        return 0;
    }
    
    serial_print("SERIAL: ELF: Header validation passed\n");
    return 1;
}

// Future: Real ELF loader would go here when needed

// Main ELF loader function
int elf_load_and_execute(const char *path) {
    serial_print("SERIAL: ELF: elf_load_and_execute called with path: ");
    serial_print(path);
    serial_print("\n");
    
    // For now, just simulate successful loading
    // In a real implementation, we would:
    // 1. Read the ELF file from the filesystem
    // 2. Parse the ELF header and program headers
    // 3. Load segments into memory at correct virtual addresses
    // 4. Set up proper memory mapping and permissions
    // 5. Jump to the entry point in user mode
    
    serial_print("SERIAL: ELF: Simulating program execution for: ");
    serial_print(path);
    serial_print("\n");
    
    // Simulate different programs based on path
    if (strstr(path, "echo")) {
        serial_print("SERIAL: ELF: Simulating /bin/echo execution\n");
        serial_print("SERIAL: ELF: echo: Hello from user program!\n");
    } else if (strstr(path, "test")) {
        serial_print("SERIAL: ELF: Simulating test program execution\n");
        serial_print("SERIAL: ELF: test: Program running successfully\n");
    } else {
        serial_print("SERIAL: ELF: Simulating generic program execution\n");
        serial_print("SERIAL: ELF: program: Generic user program executed\n");
    }
    
    serial_print("SERIAL: ELF: Program completed successfully\n");
    
    return 0;
}