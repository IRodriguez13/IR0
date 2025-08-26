// ===============================================================================
// IR0 KERNEL - MINIX FILESYSTEM IMPLEMENTATION
// ===============================================================================

#include "minix_fs.h"
#include <ir0/print.h>
#include <ir0/panic/panic.h>
#include <string.h>
#include <bump_allocator.h>

// ===============================================================================
// MINIX FILESYSTEM CONSTANTS
// ===============================================================================

#define MINIX_MAGIC 0x137F
#define MINIX_ROOT_INODE 1
#define MINIX_MAX_INODES 1024
#define MINIX_MAX_ZONES 1024
#define MINIX_ZONE_SIZE 1024

// ===============================================================================
// MINIX FILESYSTEM STRUCTURES
// ===============================================================================

typedef struct minix_fs_info {
    minix_superblock_t superblock;
    uint8_t *inode_bitmap;
    uint8_t *zone_bitmap;
    minix_inode_t *inode_table;
    uint8_t *zone_table;
    bool initialized;
} minix_fs_info_t;

// ===============================================================================
// GLOBAL VARIABLES
// ===============================================================================

static minix_fs_info_t minix_fs;
static bool minix_fs_initialized = false;

// ===============================================================================
// DISK I/O FUNCTIONS (using ATA driver)
// ===============================================================================

// Declaraciones externas del driver ATA
extern bool ata_read_sectors(uint8_t drive, uint32_t lba, uint8_t num_sectors, void* buffer);
extern bool ata_write_sectors(uint8_t drive, uint32_t lba, uint8_t num_sectors, const void* buffer);

static int minix_read_block(uint32_t block_num, void *buffer) {
    uint32_t lba = block_num * 2; // 2 sectores de 512 bytes = 1 bloque de 1024 bytes
    uint8_t num_sectors = 2;
    
    bool success = ata_read_sectors(0, lba, num_sectors, buffer);
    
    if (success) {
        return 0;
    } else {
        print("MINIX: Failed to read block ");
        print_uint64(block_num);
        print("\n");
        // Delay para ver mejor los logs de error
        delay_ms(2000);
        return -1;
    }
}

static int minix_write_block(uint32_t block_num, const void *buffer) {
    uint32_t lba = block_num * 2; // 2 sectores de 512 bytes = 1 bloque de 1024 bytes
    uint8_t num_sectors = 2;
    
    bool success = ata_write_sectors(0, lba, num_sectors, buffer);
    
    if (success) {
        return 0;
    } else {
        print("MINIX: Write block ");
        print_uint64(block_num);
        print(" failed\n");
        return -1;
    }
}

// ===============================================================================
// BITMAP FUNCTIONS
// ===============================================================================

static bool minix_is_inode_free(uint32_t inode_num) 
{
    if (inode_num >= MINIX_MAX_INODES) return false;
    
    uint32_t byte = inode_num / 8;
    uint32_t bit = inode_num % 8;
    
    return !(minix_fs.inode_bitmap[byte] & (1 << bit));
}

static void minix_mark_inode_used(uint32_t inode_num) {
    if (inode_num >= MINIX_MAX_INODES) return;
    
    uint32_t byte = inode_num / 8;
    uint32_t bit = inode_num % 8;
    
    minix_fs.inode_bitmap[byte] |= (1 << bit);
}

static void minix_mark_inode_free(uint32_t inode_num) {
    if (inode_num >= MINIX_MAX_INODES) return;
    
    uint32_t byte = inode_num / 8;
    uint32_t bit = inode_num % 8;
    
    minix_fs.inode_bitmap[byte] &= ~(1 << bit);
}

static bool minix_is_zone_free(uint32_t zone_num) {
    if (zone_num >= MINIX_MAX_ZONES) return false;
    
    uint32_t byte = zone_num / 8;
    uint32_t bit = zone_num % 8;
    
    return !(minix_fs.zone_bitmap[byte] & (1 << bit));
}

static void minix_mark_zone_used(uint32_t zone_num) {
    if (zone_num >= MINIX_MAX_ZONES) return;
    
    uint32_t byte = zone_num / 8;
    uint32_t bit = zone_num % 8;
    
    minix_fs.zone_bitmap[byte] |= (1 << bit);
}

static void minix_mark_zone_free(uint32_t zone_num) {
    if (zone_num >= MINIX_MAX_ZONES) return;
    
    uint32_t byte = zone_num / 8;
    uint32_t bit = zone_num % 8;
    
    minix_fs.zone_bitmap[byte] &= ~(1 << bit);
}

// ===============================================================================
// INODE FUNCTIONS
// ===============================================================================

static int minix_read_inode(uint32_t inode_num, minix_inode_t *inode) {
    print("MINIX: Reading inode ");
    print_uint64(inode_num);
    print("\n");
    
    if (inode_num >= MINIX_MAX_INODES || !inode) {
        print("MINIX: Invalid inode number or null pointer\n");
        return -1;
    }
    
    // Calcular posición del inode en el disco
    uint32_t inode_block = minix_fs.superblock.s_imap_blocks + 1 + (inode_num * sizeof(minix_inode_t)) / MINIX_BLOCK_SIZE;
    uint32_t inode_offset = (inode_num * sizeof(minix_inode_t)) % MINIX_BLOCK_SIZE;
    
    uint8_t block_buffer[MINIX_BLOCK_SIZE];
    int result = minix_read_block(inode_block, block_buffer);
    if (result != 0) {
        return -1;
    }
    
    // Copiar inode del buffer
    memcpy(inode, block_buffer + inode_offset, sizeof(minix_inode_t));
    
    return 0;
}

static int minix_write_inode(uint32_t inode_num, const minix_inode_t *inode) {
    if (inode_num >= MINIX_MAX_INODES || !inode) {
        return -1;
    }
    
    // Calcular posición del inode en el disco
    uint32_t inode_block = minix_fs.superblock.s_imap_blocks + 1 + (inode_num * sizeof(minix_inode_t)) / MINIX_BLOCK_SIZE;
    uint32_t inode_offset = (inode_num * sizeof(minix_inode_t)) % MINIX_BLOCK_SIZE;
    
    uint8_t block_buffer[MINIX_BLOCK_SIZE];
    int result = minix_read_block(inode_block, block_buffer);
    if (result != 0) {
        return -1;
    }
    
    // Copiar inode al buffer
    memcpy(block_buffer + inode_offset, inode, sizeof(minix_inode_t));
    
    // Escribir bloque de vuelta al disco
    result = minix_write_block(inode_block, block_buffer);
    
    return result;
}

// ===============================================================================
// ZONE ALLOCATION FUNCTIONS
// ===============================================================================

static uint32_t minix_alloc_zone(void) {
    for (uint32_t i = minix_fs.superblock.s_firstdatazone; i < MINIX_MAX_ZONES; i++) {
        if (minix_is_zone_free(i)) {
            minix_mark_zone_used(i);
            return i;
        }
    }
    return 0; // No hay zonas libres
}

static void minix_free_zone(uint32_t zone_num) {
    if (zone_num >= minix_fs.superblock.s_firstdatazone && zone_num < MINIX_MAX_ZONES) {
        minix_mark_zone_free(zone_num);
    }
}

static uint32_t minix_alloc_inode(void) {
    print("MINIX: Allocating inode...\n");
    for (uint32_t i = 1; i < MINIX_MAX_INODES; i++) { // Inode 0 no se usa
        if (minix_is_inode_free(i)) {
            print("MINIX: Found free inode ");
            print_uint64(i);
            print("\n");
            minix_mark_inode_used(i);
            return i;
        }
    }
    print("MINIX: No free inodes available\n");
    return 0; // No hay inodes libres
}

static void minix_free_inode(uint32_t inode_num) {
    if (inode_num > 0 && inode_num < MINIX_MAX_INODES) {
        minix_mark_inode_free(inode_num);
    }
}

// ===============================================================================
// DIRECTORY FUNCTIONS
// ===============================================================================

static int minix_read_dir_entry(minix_inode_t *dir_inode, uint32_t offset, minix_dir_entry_t *entry) {
    if (!dir_inode || !entry) {
        return -1;
    }
    
    // Calcular qué zona contiene esta entrada
    uint32_t zone_index = offset / MINIX_BLOCK_SIZE;
    uint32_t zone_offset = offset % MINIX_BLOCK_SIZE;
    
    if (zone_index >= 7) { // Solo soportamos bloques directos por ahora
        return -1;
    }
    
    uint32_t zone_num = dir_inode->i_zone[zone_index];
    if (zone_num == 0) {
        return -1;
    }
    
    uint8_t block_buffer[MINIX_BLOCK_SIZE];
    int result = minix_read_block(zone_num, block_buffer);
    if (result != 0) {
        return -1;
    }
    
    // Copiar entrada del directorio
    memcpy(entry, block_buffer + zone_offset, sizeof(minix_dir_entry_t));
    
    return 0;
}

static int minix_write_dir_entry(minix_inode_t *dir_inode, uint32_t offset, const minix_dir_entry_t *entry) {
    if (!dir_inode || !entry) {
        return -1;
    }
    
    // Calcular qué zona contiene esta entrada
    uint32_t zone_index = offset / MINIX_BLOCK_SIZE;
    uint32_t zone_offset = offset % MINIX_BLOCK_SIZE;
    
    if (zone_index >= 7) { // Solo soportamos bloques directos por ahora
        return -1;
    }
    
    uint32_t zone_num = dir_inode->i_zone[zone_index];
    if (zone_num == 0) {
        // Asignar nueva zona si es necesario
        zone_num = minix_alloc_zone();
        if (zone_num == 0) {
            return -1;
        }
        dir_inode->i_zone[zone_index] = zone_num;
    }
    
    uint8_t block_buffer[MINIX_BLOCK_SIZE];
    int result = minix_read_block(zone_num, block_buffer);
    if (result != 0) {
        return -1;
    }
    
    // Copiar entrada del directorio
    memcpy(block_buffer + zone_offset, entry, sizeof(minix_dir_entry_t));
    
    // Escribir bloque de vuelta al disco
    result = minix_write_block(zone_num, block_buffer);
    
    return result;
}

// ===============================================================================
// PUBLIC MINIX FILESYSTEM FUNCTIONS
// ===============================================================================

int minix_fs_init(void) {
    if (minix_fs_initialized) {
        return 0;
    }
    
    // Leer superblock del disco
    int result = minix_read_block(1, &minix_fs.superblock); // Superblock está en el bloque 1
    if (result != 0) {
        print("MINIX: Formatting disk...\n");
        return minix_fs_format();
    }
    
    // Verificar número mágico
    if (minix_fs.superblock.s_magic != MINIX_MAGIC) {
        print("MINIX: Formatting disk...\n");
        return minix_fs_format();
    }
    
    // Inicializar bitmaps en memoria
    minix_fs.inode_bitmap = kmalloc(MINIX_MAX_INODES / 8);
    minix_fs.zone_bitmap = kmalloc(MINIX_MAX_ZONES / 8);
    
    if (!minix_fs.inode_bitmap || !minix_fs.zone_bitmap) {
        print("MINIX: Failed to allocate bitmaps\n");
        return -1;
    }
    
    // Leer bitmaps del disco
    result = minix_read_block(2, minix_fs.inode_bitmap); // Bitmap de inodes
    if (result != 0) {
        print("MINIX: Failed to read inode bitmap\n");
        return -1;
    }
    
    result = minix_read_block(3, minix_fs.zone_bitmap); // Bitmap de zonas
    if (result != 0) {
        print("MINIX: Failed to read zone bitmap\n");
        return -1;
    }
    
    minix_fs_initialized = true;
    return 0;
}

int minix_fs_format(void) {
    print("MINIX: Formatting disk with Minix filesystem...\n");
    
    // Inicializar superblock
    memset(&minix_fs.superblock, 0, sizeof(minix_superblock_t));
    minix_fs.superblock.s_ninodes = MINIX_MAX_INODES;
    minix_fs.superblock.s_nzones = MINIX_MAX_ZONES;
    minix_fs.superblock.s_imap_blocks = 1;
    minix_fs.superblock.s_zmap_blocks = 1;
    minix_fs.superblock.s_firstdatazone = 4;
    minix_fs.superblock.s_log_zone_size = 0;
    minix_fs.superblock.s_max_size = 268435456; // 256MB
    minix_fs.superblock.s_magic = MINIX_MAGIC;
    
    // Escribir superblock
    int result = minix_write_block(1, &minix_fs.superblock);
    if (result != 0) {
        print("MINIX: Failed to write superblock\n");
        return -1;
    }
    
    // Inicializar bitmaps
    minix_fs.inode_bitmap = kmalloc(MINIX_MAX_INODES / 8);
    minix_fs.zone_bitmap = kmalloc(MINIX_MAX_ZONES / 8);
    
    if (!minix_fs.inode_bitmap || !minix_fs.zone_bitmap) {
        print("MINIX: Failed to allocate bitmaps\n");
        return -1;
    }
    
    memset(minix_fs.inode_bitmap, 0, MINIX_MAX_INODES / 8);
    memset(minix_fs.zone_bitmap, 0, MINIX_MAX_ZONES / 8);
    
    // Marcar inodes y zonas del sistema como usados
    for (int i = 0; i < 4; i++) { // Primeros 4 inodes
        minix_mark_inode_used(i);
    }
    
    for (int i = 0; i < 4; i++) { // Primeras 4 zonas
        minix_mark_zone_used(i);
    }
    
    // Escribir bitmaps
    result = minix_write_block(2, minix_fs.inode_bitmap);
    if (result != 0) {
        print("MINIX: Failed to write inode bitmap\n");
        return -1;
    }
    
    result = minix_write_block(3, minix_fs.zone_bitmap);
    if (result != 0) {
        print("MINIX: Failed to write zone bitmap\n");
        return -1;
    }
    
    // Crear directorio raíz
    minix_inode_t root_inode;
    memset(&root_inode, 0, sizeof(minix_inode_t));
    root_inode.i_mode = MINIX_IFDIR | MINIX_IRUSR | MINIX_IWUSR | MINIX_IXUSR | MINIX_IRGRP | MINIX_IXGRP | MINIX_IROTH | MINIX_IXOTH;
    root_inode.i_uid = 0;
    root_inode.i_size = 0;
    root_inode.i_time = 1234567890;
    root_inode.i_gid = 0;
    root_inode.i_nlinks = 2;
    
    result = minix_write_inode(MINIX_ROOT_INODE, &root_inode);
    if (result != 0) {
        print("MINIX: Failed to write root inode\n");
        return -1;
    }
    
    minix_fs_initialized = true;
    print("MINIX: Disk formatted successfully\n");
    
    return 0;
}

int minix_fs_mkdir(const char *path) {
    if (!minix_fs_initialized || !path) {
        return -1;
    }
    
    print("MINIX: Creating directory: ");
    print(path);
    print("\n");
    
    // Asignar nuevo inode
    uint32_t new_inode_num = minix_alloc_inode();
    if (new_inode_num == 0) {
        print("MINIX: No free inodes available\n");
        return -1;
    }
    
    // Crear inode del directorio
    minix_inode_t new_inode;
    memset(&new_inode, 0, sizeof(minix_inode_t));
    new_inode.i_mode = MINIX_IFDIR | MINIX_IRUSR | MINIX_IWUSR | MINIX_IXUSR | MINIX_IRGRP | MINIX_IXGRP | MINIX_IROTH | MINIX_IXOTH;
    new_inode.i_uid = 0;
    new_inode.i_size = 0;
    new_inode.i_time = 1234567890;
    new_inode.i_gid = 0;
    new_inode.i_nlinks = 2;
    
    // Escribir inode al disco
    print("MINIX: Writing new inode ");
    print_uint64(new_inode_num);
    print(" to disk...\n");
    int result = minix_write_inode(new_inode_num, &new_inode);
    if (result != 0) {
        minix_free_inode(new_inode_num);
        print("MINIX: Failed to write new inode\n");
        return -1;
    }
    print("MINIX: Successfully wrote inode ");
    print_uint64(new_inode_num);
    print(" to disk\n");
    
    // Leer inode del directorio padre (raíz por ahora)
    minix_inode_t parent_inode;
    result = minix_read_inode(MINIX_ROOT_INODE, &parent_inode);
    if (result != 0) {
        minix_free_inode(new_inode_num);
        print("MINIX: Failed to read parent inode\n");
        return -1;
    }
    
    // Crear entrada en el directorio padre
    minix_dir_entry_t new_entry;
    new_entry.inode = new_inode_num;
    strncpy(new_entry.name, path + 1, MINIX_NAME_LEN - 1); // Saltar el '/' inicial
    new_entry.name[MINIX_NAME_LEN - 1] = '\0';
    
    result = minix_write_dir_entry(&parent_inode, parent_inode.i_size, &new_entry);
    if (result != 0) {
        minix_free_inode(new_inode_num);
        print("MINIX: Failed to write directory entry\n");
        return -1;
    }
    
    // Actualizar tamaño del directorio padre
    parent_inode.i_size += sizeof(minix_dir_entry_t);
    result = minix_write_inode(MINIX_ROOT_INODE, &parent_inode);
    if (result != 0) {
        print("MINIX: Failed to update parent inode\n");
        return -1;
    }
    
    print("MINIX: Directory created successfully\n");
    return 0;
}

int minix_fs_ls(const char *path) {
    if (!minix_fs_initialized) {
        return -1;
    }
    
    print("MINIX: Listing directory: ");
    print(path);
    print("\n");
    
    // Leer inode del directorio (raíz por ahora)
    minix_inode_t dir_inode;
    int result = minix_read_inode(MINIX_ROOT_INODE, &dir_inode);
    if (result != 0) {
        print("MINIX: Failed to read directory inode\n");
        return -1;
    }
    
    print("=== Directory contents ===\n");
    
    // Leer entradas del directorio
    uint32_t offset = 0;
    while (offset < dir_inode.i_size) {
        minix_dir_entry_t entry;
        result = minix_read_dir_entry(&dir_inode, offset, &entry);
        if (result != 0) {
            break;
        }
        
        if (entry.inode != 0) { // Entrada válida
            // Leer inode para obtener información
            minix_inode_t file_inode;
            if (minix_read_inode(entry.inode, &file_inode) == 0) {
                const char *type = minix_is_dir(&file_inode) ? "d" : "-";
                print(type);
                print("rwxr-xr-x  root  root  ");
                print(entry.name);
                print("\n");
            }
        }
        
        offset += sizeof(minix_dir_entry_t);
    }
    
    return 0;
}

// ===============================================================================
// CLEANUP FUNCTIONS
// ===============================================================================

void minix_fs_cleanup(void) {
    if (minix_fs.inode_bitmap) {
        kfree(minix_fs.inode_bitmap);
        minix_fs.inode_bitmap = NULL;
    }
    
    if (minix_fs.zone_bitmap) {
        kfree(minix_fs.zone_bitmap);
        minix_fs.zone_bitmap = NULL;
    }
    
    minix_fs_initialized = false;
    print("MINIX: Filesystem cleaned up\n");
}
