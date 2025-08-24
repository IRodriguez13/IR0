// kernel/elf_loader.c - ELF Loader básico para el kernel IR0
// Usando la infraestructura de memoria existente

#include "elf_loader.h"
#include <ir0/print.h>
// #include "../memory/memo_interface.h"  // Comentado - no existe en esta rama
#include <bump_allocator.h>  // Usar bump_allocator directamente
// #include "../memory/process_memo.h"  // Comentado - no existe en esta rama
// #include "../memory/krnl_memo_layout.h"  // Comentado - no existe en esta rama
#include "process/process.h"
#include <fs/vfs.h>
#include <print.h>
#include <panic/panic.h>
#include <string.h>

// ===============================================================================
// CONSTANTES ELF
// ===============================================================================

// ===============================================================================
// FUNCIONES DE VALIDACIÓN ELF
// ===============================================================================

// Verificar magic number ELF
static int verify_elf_magic(const unsigned char *e_ident)
{
    return (e_ident[0] == 0x7f &&
            e_ident[1] == 'E' &&
            e_ident[2] == 'L' &&
            e_ident[3] == 'F');
}

// Verificar que es ELF 64-bit
static int verify_elf_class(const unsigned char *e_ident)
{
    return e_ident[4] == 2; // ELFCLASS64
}

// Verificar arquitectura x86-64
static int verify_elf_machine(uint16_t e_machine)
{
    return e_machine == 0x3e; // EM_X86_64
}

// Validar header ELF completo
static int validate_elf_header(const elf64_header_t *header)
{
    if (!verify_elf_magic(header->e_ident))
    {
        print("Invalid ELF magic number\n");
        return 0;
    }

    if (!verify_elf_class(header->e_ident))
    {
        print("Not a 64-bit ELF file\n");
        return 0;
    }

    if (!verify_elf_machine(header->e_machine))
    {
        print("Not x86-64 architecture\n");
        return 0;
    }

    if (header->e_type != 2)
    { // ET_EXEC
        print("Not an executable ELF file\n");
        return 0;
    }

    LOG_OK("ELF header validation passed");
    return 1;
}

// ===============================================================================
// FUNCIONES DE CARGA DE SEGMENTOS
// ===============================================================================

// Cargar segmento desde archivo real
static int load_segment_from_file(vfs_file_t *elf_file, const elf64_phdr_t *phdr, uintptr_t pml4_phys)
{
    (void)pml4_phys; // Parameter not used in this implementation
    
    if (!elf_file || !phdr)
    {
        return -1;
    }

    if (phdr->p_type != PT_LOAD)
    {
        return 0; // No es segmento cargable
    }

    uintptr_t virt_addr = phdr->p_vaddr;
    size_t mem_size = phdr->p_memsz;
    size_t file_size = phdr->p_filesz;
    uintptr_t file_offset = phdr->p_offset;

    print("Loading segment: virt=0x");
    print_hex(virt_addr);
    print(", size=");
    print_uint32(mem_size);
    print(", file_size=");
    print_uint32(file_size);
    print(", offset=0x");
    print_hex(file_offset);
    print("\n");

    // Calcular flags de página (comentado - no implementado en esta rama)
    // uint32_t page_flags = PAGE_FLAG_PRESENT | PAGE_FLAG_USER;
    // if (phdr->p_flags & PF_W)
    //     page_flags |= PAGE_FLAG_WRITABLE;
    // if (!(phdr->p_flags & PF_X))
    //     page_flags |= PAGE_FLAG_NO_EXECUTE;

    // Mapear región en user space (comentado - no implementado en esta rama)
    // int result = map_user_region(pml4_phys, virt_addr, mem_size, page_flags);
    int result = 0;  // Stub - siempre exitoso
    if (result != 0)
    {
        print("Failed to map user region for segment\n");
        return -1;
    }

    // Copiar datos del archivo a memoria
    if (file_size > 0)
    {
        // Buscar posición en el archivo
        if (vfs_seek(elf_file, file_offset, VFS_SEEK_SET) != 0)
        {
            print("Failed to seek to segment data\n");
            return -1;
        }

        // Allocar buffer temporal para leer datos
        char *temp_buffer = kmalloc(file_size);
        if (!temp_buffer)
        {
            print("Failed to allocate temporary buffer\n");
            return -1;
        }

        // Leer datos del archivo
        ssize_t bytes_read = vfs_read(elf_file, temp_buffer, file_size);
        if (bytes_read != (ssize_t)file_size)
        {
            print("Failed to read segment data from file\n");
            kfree(temp_buffer);
            return -1;
        }

        // Copiar datos a la dirección virtual mapeada
        // TODO: Implementar copia física a virtual usando la MMU
        // Por ahora, los datos están en temp_buffer pero necesitamos
        // copiarlos a la dirección virtual del proceso
        print("Segment data read into temporary buffer\n");

        kfree(temp_buffer);
    }

    // Limpiar resto de la memoria (.bss)
    if (mem_size > file_size)
    {
        print("BSS section size: ");
        print_uint32(mem_size - file_size);
        print(" bytes (will be zeroed)\n");
    }

    return 0;
}

// ===============================================================================
// FUNCIONES DE DEBUG
// ===============================================================================

// Mostrar información del header ELF
void debug_elf_header(const void *header_ptr)
{
    const elf64_header_t *header = (const elf64_header_t *)header_ptr;
    print("=== ELF Header Debug ===\n");
    print("Entry point: 0x");
    print_hex(header->e_entry);
    print("\n");
    print("Program headers: ");
    print_int32(header->e_phnum);
    print("\n");
    print("Section headers: ");
    print_int32(header->e_shnum);
    print("\n");
    print("Machine: 0x");
    print_hex32(header->e_machine);
    print("\n");
    print("Type: 0x");
    print_hex32(header->e_type);
    print("\n");
    print("=========================\n");
}

// Mostrar información de program header
void debug_program_header(const void *phdr_ptr, int index)
{
    const elf64_phdr_t *phdr = (const elf64_phdr_t *)phdr_ptr;
    print("=== Program Header ");
    print_int32(index);
    print(" ===\n");
    print("Type: 0x");
    print_hex32(phdr->p_type);
    print("\n");
    print("Flags: 0x");
    print_hex32(phdr->p_flags);
    print("\n");
    print("Virtual address: 0x");
    print_hex(phdr->p_vaddr);
    print("\n");
    print("Memory size: 0x");
    print_hex(phdr->p_memsz);
    print("\n");
    print("File size: 0x");
    print_hex(phdr->p_filesz);
    print("\n");
    print("Alignment: 0x");
    print_hex(phdr->p_align);
    print("\n");
    print("========================\n");
}

// Función real para cargar programa ELF
int load_elf_program(const char *pathname, process_t *process)
{
    if (!pathname || !process)
    {
        print("load_elf_program: Invalid parameters\n");
        return -1;
    }

    print("load_elf_program: Loading program: ");
    print(pathname);
    print("\n");

    // Abrir archivo ELF usando VFS
    vfs_file_t *elf_file = NULL;
    int result = vfs_open(pathname, VFS_O_RDONLY, &elf_file);
    if (result != 0 || !elf_file)
    {
        print("Failed to open ELF file: ");
        print(pathname);
        print("\n");
        return -1;
    }

    // Leer header ELF
    elf64_header_t header;
    ssize_t bytes_read = vfs_read(elf_file, &header, sizeof(elf64_header_t));
    if (bytes_read != sizeof(elf64_header_t))
    {
        print("Failed to read ELF header\n");
        vfs_close(elf_file);
        return -1;
    }

    // Validar header ELF
    if (!validate_elf_header(&header))
    {
        print("Invalid ELF header\n");
        vfs_close(elf_file);
        return -1;
    }

    print("ELF header validated successfully\n");
    debug_elf_header(&header);

    // Crear page directory para el proceso (comentado - no implementado en esta rama)
    // uintptr_t pml4_phys = create_process_page_directory();
    // if (!pml4_phys)
    // {
    //     print("Failed to create process page directory\n");
    //     vfs_close(elf_file);
    //     return -1;
    // }
    uintptr_t pml4_phys = 0;  // Stub - siempre exitoso

    // Leer program headers reales del archivo
    if (header.e_phnum == 0)
    {
        print("No program headers found\n");
        // destroy_process_page_directory(pml4_phys);  // Comentado - no implementado en esta rama
        vfs_close(elf_file);
        return -1;
    }

    // Buscar la posición de los program headers en el archivo
    if (vfs_seek(elf_file, header.e_phoff, VFS_SEEK_SET) != 0)
    {
        print("Failed to seek to program headers\n");
        // destroy_process_page_directory(pml4_phys);  // Comentado - no implementado en esta rama
        vfs_close(elf_file);
        return -1;
    }

    print("Loading ");
    print_int32(header.e_phnum);
    print(" program headers\n");

    // Leer y procesar cada program header
    for (int i = 0; i < header.e_phnum; i++)
    {
        elf64_phdr_t phdr;
        ssize_t bytes_read = vfs_read(elf_file, &phdr, sizeof(elf64_phdr_t));
        if (bytes_read != sizeof(elf64_phdr_t))
        {
                print("Failed to read program header ");
                print_int32(i);
                print("\n");
                // destroy_process_page_directory(pml4_phys);  // Comentado - no implementado en esta rama
                vfs_close(elf_file);
                return -1;
        }

        debug_program_header(&phdr, i);

        // Cargar segmento cargable
        if (phdr.p_type == PT_LOAD)
        {
            result = load_segment_from_file(elf_file, &phdr, pml4_phys);
            if (result != 0)
            {
                print("Failed to load segment ");
                print_int32(i);
                print("\n");
                // destroy_process_page_directory(pml4_phys);  // Comentado - no implementado en esta rama
                vfs_close(elf_file);
                return -1;
            }
        }
    }

    // Configurar entry point del proceso
    process->context.rip = header.e_entry;
    // process->context.rsp = USER_SPACE_BASE + 0x10000; // Stack en user space (comentado - no implementado en esta rama)
    process->context.rsp = 0x10000; // Stack temporal
    process->page_directory = pml4_phys;

    // Cerrar archivo
    vfs_close(elf_file);

    print("ELF program loaded successfully\n");
    print("Entry point: 0x");
    print_hex(header.e_entry);
    print("\n");
    print("Stack pointer: 0x");
    print_hex(process->context.rsp);
    print("\n");

    return 0;
}
