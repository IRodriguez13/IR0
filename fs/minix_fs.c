// ===============================================================================
// IR0 KERNEL - MINIX FILESYSTEM IMPLEMENTATION
// ===============================================================================

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <ir0/print.h>
#include <drivers/storage/ata.h>
#include <drivers/timer/clock_system.h>
#include "minix_fs.h"

// Definir constantes faltantes
#define MINIX_SUPER_MAGIC 0x137F
#define MINIX_MAGIC 0x137F

// ===============================================================================
// MINIX FILESYSTEM CONSTANTS
// ===============================================================================

#define MINIX_ROOT_INODE 1
#define MINIX_MAX_INODES 1024
#define MINIX_MAX_ZONES 1024
#define MINIX_ZONE_SIZE 1024

// ===============================================================================
// MINIX FILESYSTEM STRUCTURES
// ===============================================================================

typedef struct minix_fs_info
{
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
extern bool ata_read_sectors(uint8_t drive, uint32_t lba, uint8_t num_sectors, void *buffer);
extern bool ata_write_sectors(uint8_t drive, uint32_t lba, uint8_t num_sectors, const void *buffer);

int minix_read_block(uint32_t block_num, void *buffer)
{
    uint32_t lba = block_num * 2; // 2 sectores de 512 bytes = 1 bloque de 1024 bytes
    uint8_t num_sectors = 2;


    bool success = ata_read_sectors(0, lba, num_sectors, buffer);

    if (success)
    {
        return 0;
    }
    else
    {
        return -1;
    }
}

int minix_write_block(uint32_t block_num, const void *buffer)
{
    uint32_t lba = block_num * 2; // 2 sectores de 512 bytes = 1 bloque de 1024 bytes
    uint8_t num_sectors = 2;


    bool success = ata_write_sectors(0, lba, num_sectors, buffer);

    if (success)
    {
        return 0;
    }
    else
    {
        return -1;
    }
}

// ===============================================================================
// BITMAP FUNCTIONS
// ===============================================================================

static bool __attribute__((unused)) minix_is_inode_free(uint32_t inode_num)
{
    if (inode_num >= MINIX_MAX_INODES)
        return false;

    uint32_t byte = inode_num / 8;
    uint32_t bit = inode_num % 8;

    return !(minix_fs.inode_bitmap[byte] & (1 << bit));
}

void minix_mark_inode_used(uint32_t inode_num)
{
    if (inode_num >= MINIX_MAX_INODES)
        return;

    uint32_t byte = inode_num / 8;
    uint32_t bit = inode_num % 8;

    minix_fs.inode_bitmap[byte] |= (1 << bit);
}

void minix_mark_inode_free(uint32_t inode_num)
{
    if (inode_num >= MINIX_MAX_INODES)
        return;

    uint32_t byte = inode_num / 8;
    uint32_t bit = inode_num % 8;

    minix_fs.inode_bitmap[byte] &= ~(1 << bit);
}

bool minix_is_zone_free(uint32_t zone_num)
{
    if (zone_num < minix_fs.superblock.s_firstdatazone || zone_num >= MINIX_MAX_ZONES)
    {
        return false;
    }

    // Calcular la posición en el bitmap
    uint32_t byte_index = (zone_num - minix_fs.superblock.s_firstdatazone) / 8;
    uint32_t bit_index = (zone_num - minix_fs.superblock.s_firstdatazone) % 8;

    if (byte_index >= minix_fs.superblock.s_zmap_blocks * MINIX_BLOCK_SIZE)
    {
        return false;
    }

    // Leer el bloque del bitmap
    uint32_t block_num = minix_fs.superblock.s_zmap_blocks + byte_index / MINIX_BLOCK_SIZE;
    uint32_t block_offset = byte_index % MINIX_BLOCK_SIZE;

    uint8_t bitmap_block[MINIX_BLOCK_SIZE];
    if (minix_read_block(block_num, bitmap_block) != 0)
    {
        return false;
    }

    // Verificar si el bit está libre (1 = libre, 0 = usado)
    return (bitmap_block[block_offset] & (1 << bit_index)) != 0;
}

void minix_mark_zone_used(uint32_t zone_num)
{
    if (zone_num < minix_fs.superblock.s_firstdatazone || zone_num >= MINIX_MAX_ZONES)
    {
        return;
    }

    // Calcular la posición en el bitmap
    uint32_t byte_index = (zone_num - minix_fs.superblock.s_firstdatazone) / 8;
    uint32_t bit_index = (zone_num - minix_fs.superblock.s_firstdatazone) % 8;

    if (byte_index >= minix_fs.superblock.s_zmap_blocks * MINIX_BLOCK_SIZE)
    {
        return;
    }

    // Leer el bloque del bitmap
    uint32_t block_num = minix_fs.superblock.s_zmap_blocks + byte_index / MINIX_BLOCK_SIZE;
    uint32_t block_offset = byte_index % MINIX_BLOCK_SIZE;

    uint8_t bitmap_block[MINIX_BLOCK_SIZE];
    if (minix_read_block(block_num, bitmap_block) != 0)
    {
        return;
    }

    // Marcar la zona como usada (bit = 0)
    bitmap_block[block_offset] &= ~(1 << bit_index);

    // Escribir el bloque actualizado
    minix_write_block(block_num, bitmap_block);
}

uint32_t minix_alloc_zone(void)
{
    for (uint32_t i = minix_fs.superblock.s_firstdatazone; i < MINIX_MAX_ZONES; i++)
    {
        if (minix_is_zone_free(i))
        {
            minix_mark_zone_used(i);
            return i;
        }
    }
    return 0; // No hay zonas libres
}

void minix_free_zone(uint32_t zone_num)
{
    if (zone_num < minix_fs.superblock.s_firstdatazone || zone_num >= MINIX_MAX_ZONES)
    {
        return;
    }


    // Calcular la posición en el bitmap
    uint32_t byte_index = (zone_num - minix_fs.superblock.s_firstdatazone) / 8;
    uint32_t bit_index = (zone_num - minix_fs.superblock.s_firstdatazone) % 8;

    if (byte_index >= minix_fs.superblock.s_zmap_blocks * MINIX_BLOCK_SIZE)
    {
        return;
    }

    // Leer el bloque del bitmap si no está en memoria
    uint32_t block_num = minix_fs.superblock.s_zmap_blocks + byte_index / MINIX_BLOCK_SIZE;
    uint32_t block_offset = byte_index % MINIX_BLOCK_SIZE;

    uint8_t bitmap_block[MINIX_BLOCK_SIZE];
    if (minix_read_block(block_num, bitmap_block) != 0)
    {
        return;
    }

    // Marcar la zona como libre (bit = 1)
    bitmap_block[block_offset] |= (1 << bit_index);

    // Escribir el bloque actualizado
    if (minix_write_block(block_num, bitmap_block) != 0)
    {
        return;
    }

}

// ===============================================================================
// INODE FUNCTIONS
// ===============================================================================

static int minix_read_inode(uint32_t inode_num, minix_inode_t *inode) __attribute__((unused));
static int minix_read_inode(uint32_t inode_num, minix_inode_t *inode)
{
    if (inode_num == 0 || inode_num >= MINIX_MAX_INODES || !inode)
    {
        return -1;
    }

    // Calcular posición del inode en el disco
    uint32_t inode_block = minix_fs.superblock.s_imap_blocks + 1 + (inode_num * sizeof(minix_inode_t)) / MINIX_BLOCK_SIZE;
    uint32_t inode_offset = (inode_num * sizeof(minix_inode_t)) % MINIX_BLOCK_SIZE;

    uint8_t block_buffer[MINIX_BLOCK_SIZE];
    int result = minix_read_block(inode_block, block_buffer);
    if (result != 0)
    {
        return -1;
    }

    // Copiar inode del buffer
    memcpy(inode, block_buffer + inode_offset, sizeof(minix_inode_t));

    return 0;
}

static int __attribute__((unused)) minix_write_inode(uint32_t inode_num, const minix_inode_t *inode)
{
    if (inode_num == 0 || inode_num >= MINIX_MAX_INODES || !inode)
    {
        return -1;
    }

    // Calcular posición del inode en el disco
    uint32_t inode_block = minix_fs.superblock.s_imap_blocks + 1 + (inode_num * sizeof(minix_inode_t)) / MINIX_BLOCK_SIZE;
    uint32_t inode_offset = (inode_num * sizeof(minix_inode_t)) % MINIX_BLOCK_SIZE;

    uint8_t block_buffer[MINIX_BLOCK_SIZE];
    int result = minix_read_block(inode_block, block_buffer);
    if (result != 0)
    {
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

uint32_t minix_alloc_inode(void)
{
    for (uint32_t i = 1; i < MINIX_MAX_INODES; i++)
    {
        uint32_t byte = i / 8;
        uint32_t bit = i % 8;

        if (!(minix_fs.inode_bitmap[byte] & (1 << bit)))
        {
            minix_fs.inode_bitmap[byte] |= (1 << bit);
            return i;
        }
    }

    return 0; // No hay inodes libres
}

// ===============================================================================
// MINIX FILESYSTEM FUNCTIONS IMPLEMENTATION
// ===============================================================================

minix_inode_t *minix_fs_find_inode(const char *pathname)
{
    if (!pathname || !minix_fs_initialized)
    {
        return NULL;
    }


    // Si es el directorio raíz
    if (strcmp(pathname, "/") == 0)
    {
        static minix_inode_t root_inode;
        if (minix_read_inode(MINIX_ROOT_INODE, &root_inode) == 0)
        {
            return &root_inode;
        }
        else
        {
            return NULL;
        }
    }

    // Parsear el path
    char path_copy[256];
    strncpy(path_copy, pathname, sizeof(path_copy) - 1);
    path_copy[sizeof(path_copy) - 1] = '\0';

    // Empezar desde el inode raíz
    minix_inode_t current_inode;
    if (minix_read_inode(MINIX_ROOT_INODE, &current_inode) != 0)
    {
        return NULL;
    }

    // Dividir el path en componentes
    char *token = strtok(path_copy, "/");
    while (token != NULL)
    {
        // Verificar que el inode actual es un directorio
        if (!(current_inode.i_mode & MINIX_IFDIR))
        {
            return NULL;
        }

        // Buscar la entrada en el directorio actual
        uint16_t found_inode = minix_fs_find_dir_entry(&current_inode, token);
        if (found_inode == 0)
        {
            return NULL;
        }

        // Leer el inode encontrado
        if (minix_read_inode(found_inode, &current_inode) != 0)
        {
            return NULL;
        }

        token = strtok(NULL, "/");
    }

    // Retornar una copia estática del inode encontrado
    static minix_inode_t result_inode;
    memcpy(&result_inode, &current_inode, sizeof(minix_inode_t));

    return &result_inode;
}

// Función auxiliar para obtener el número de inode de un path
static uint16_t minix_fs_get_inode_number(const char *pathname)
{
    if (!pathname || !minix_fs_initialized)
    {
        return 0;
    }

    // Si es el directorio raíz
    if (strcmp(pathname, "/") == 0)
    {
        return MINIX_ROOT_INODE;
    }

    // Parsear el path
    char path_copy[256];
    strncpy(path_copy, pathname, sizeof(path_copy) - 1);
    path_copy[sizeof(path_copy) - 1] = '\0';

    // Empezar desde el inode raíz
    minix_inode_t current_inode;
    if (minix_read_inode(MINIX_ROOT_INODE, &current_inode) != 0)
    {
        return 0;
    }

    // Dividir el path en componentes
    char *token = strtok(path_copy, "/");
    uint16_t current_inode_num = MINIX_ROOT_INODE;

    while (token != NULL)
    {
        // Verificar que el inode actual es un directorio
        if (!(current_inode.i_mode & MINIX_IFDIR))
        {
            return 0;
        }

        // Buscar la entrada en el directorio actual
        uint16_t found_inode = minix_fs_find_dir_entry(&current_inode, token);
        if (found_inode == 0)
        {
            return 0;
        }

        current_inode_num = found_inode;

        // Leer el inode encontrado
        if (minix_read_inode(found_inode, &current_inode) != 0)
        {
            return 0;
        }

        token = strtok(NULL, "/");
    }

    return current_inode_num;
}

// ===============================================================================
// DIRECTORY ENTRY FUNCTIONS
// ===============================================================================

uint16_t minix_fs_find_dir_entry(const minix_inode_t *dir_inode, const char *name)
{
    if (!dir_inode || !name || !(dir_inode->i_mode & MINIX_IFDIR))
    {
        return 0;
    }


    // Leer todas las zonas del directorio
    for (int i = 0; i < 7; i++)
    {
        if (dir_inode->i_zone[i] == 0)
        {
            continue; // Zona vacía
        }

        uint8_t block_buffer[MINIX_BLOCK_SIZE];
        if (minix_read_block(dir_inode->i_zone[i], block_buffer) != 0)
        {
            continue;
        }

        // Buscar en las entradas del directorio
        minix_dir_entry_t *entries = (minix_dir_entry_t *)block_buffer;
        int num_entries = MINIX_BLOCK_SIZE / sizeof(minix_dir_entry_t);

        for (int j = 0; j < num_entries; j++)
        {
            if (entries[j].inode == 0)
            {
                continue; // Entrada vacía
            }

            if (strcmp(entries[j].name, name) == 0)
            {
                return entries[j].inode;
            }
        }
    }

    return 0;
}

int minix_fs_write_inode(uint16_t inode_num, const minix_inode_t *inode)
{
    if (!inode || inode_num == 0)
    {
        return -1;
    }


    // Calcular la posición del inode en el disco
    uint32_t inode_block = minix_fs.superblock.s_imap_blocks + 1 + (inode_num - 1) / (MINIX_BLOCK_SIZE / MINIX_INODE_SIZE);
    uint32_t inode_offset = ((inode_num - 1) % (MINIX_BLOCK_SIZE / MINIX_INODE_SIZE)) * MINIX_INODE_SIZE;

    // Leer el bloque que contiene el inode
    uint8_t block_buffer[MINIX_BLOCK_SIZE];
    if (minix_read_block(inode_block, block_buffer) != 0)
    {
        return -1;
    }

    // Copiar el inode al buffer
    memcpy(block_buffer + inode_offset, inode, MINIX_INODE_SIZE);

    // Escribir el bloque actualizado
    if (minix_write_block(inode_block, block_buffer) != 0)
    {
        return -1;
    }

    return 0;
}

int minix_fs_free_inode(uint16_t inode_num)
{
    if (inode_num == 0 || inode_num > minix_fs.superblock.s_ninodes)
    {
        return -1;
    }


    // Calcular la posición en el bitmap de inodes
    uint32_t byte_index = (inode_num - 1) / 8;
    uint32_t bit_index = (inode_num - 1) % 8;

    if (byte_index >= minix_fs.superblock.s_imap_blocks * MINIX_BLOCK_SIZE)
    {
        return -1;
    }

    // Leer el bloque del bitmap
    uint32_t block_num = 1 + byte_index / MINIX_BLOCK_SIZE;
    uint32_t block_offset = byte_index % MINIX_BLOCK_SIZE;

    uint8_t bitmap_block[MINIX_BLOCK_SIZE];
    if (minix_read_block(block_num, bitmap_block) != 0)
    {
        return -1;
    }

    // Marcar el inode como libre (bit = 1)
    bitmap_block[block_offset] |= (1 << bit_index);

    // Escribir el bloque actualizado
    if (minix_write_block(block_num, bitmap_block) != 0)
    {
        return -1;
    }

    return 0;
}

int minix_fs_split_path(const char *pathname, char *parent_path, char *filename)
{
    if (!pathname || !parent_path || !filename)
    {
        return -1;
    }

    // Encontrar la última barra
    const char *last_slash = strrchr(pathname, '/');
    if (!last_slash)
    {
        // No hay barra, el archivo está en el directorio actual
        strcpy(parent_path, ".");
        strcpy(filename, pathname);
        return 0;
    }

    if (last_slash == pathname)
    {
        // Es el directorio raíz
        strcpy(parent_path, "/");
    }
    else
    {
        // Copiar la parte del directorio padre
        size_t parent_len = last_slash - pathname;
        strncpy(parent_path, pathname, parent_len);
        parent_path[parent_len] = '\0';
    }

    // Copiar el nombre del archivo
    strcpy(filename, last_slash + 1);


    return 0;
}

int minix_fs_add_dir_entry(minix_inode_t *parent_inode, const char *filename, uint16_t inode_num)
{
    if (!parent_inode || !filename || inode_num == 0)
    {
        return -1;
    }


    // Buscar una zona con espacio libre o asignar una nueva
    uint32_t target_zone = 0;
    uint32_t target_block = 0;
    int target_entry = -1;

    // Primero buscar en zonas existentes
    for (int i = 0; i < 7; i++)
    {
        if (parent_inode->i_zone[i] == 0)
        {
            continue;
        }

        uint8_t block_buffer[MINIX_BLOCK_SIZE];
        if (minix_read_block(parent_inode->i_zone[i], block_buffer) != 0)
        {
            continue;
        }

        minix_dir_entry_t *entries = (minix_dir_entry_t *)block_buffer;
        int num_entries = MINIX_BLOCK_SIZE / sizeof(minix_dir_entry_t);

        for (int j = 0; j < num_entries; j++)
        {
            if (entries[j].inode == 0)
            {
                // Encontramos una entrada libre
                target_zone = parent_inode->i_zone[i];
                target_block = parent_inode->i_zone[i];
                target_entry = j;
                break;
            }
        }

        if (target_entry != -1)
        {
            break;
        }
    }

    // Si no encontramos espacio, asignar una nueva zona
    if (target_entry == -1)
    {
        for (int i = 0; i < 7; i++)
        {
            if (parent_inode->i_zone[i] == 0)
            {
                target_zone = minix_alloc_zone();
                if (target_zone != 0)
                {
                    parent_inode->i_zone[i] = target_zone;
                    target_block = target_zone;
                    target_entry = 0;

                    // Inicializar el nuevo bloque con ceros
                    uint8_t block_buffer[MINIX_BLOCK_SIZE];
                    memset(block_buffer, 0, MINIX_BLOCK_SIZE);
                    minix_write_block(target_zone, block_buffer);
                    break;
                }
            }
        }
    }

    if (target_entry == -1)
    {
        return -1;
    }

    // Leer el bloque donde agregaremos la entrada
    uint8_t block_buffer[MINIX_BLOCK_SIZE];
    if (minix_read_block(target_block, block_buffer) != 0)
    {
        return -1;
    }

    // Agregar la nueva entrada
    minix_dir_entry_t *entries = (minix_dir_entry_t *)block_buffer;
    entries[target_entry].inode = inode_num;
    strncpy(entries[target_entry].name, filename, MINIX_NAME_LEN);
    entries[target_entry].name[MINIX_NAME_LEN - 1] = '\0';

    // Escribir el bloque actualizado
    if (minix_write_block(target_block, block_buffer) != 0)
    {
        return -1;
    }

    // Actualizar el tamaño del directorio si es necesario
    if ((size_t)target_entry >= parent_inode->i_size / sizeof(minix_dir_entry_t))
    {
        parent_inode->i_size = (target_entry + 1) * sizeof(minix_dir_entry_t);
    }

    return 0;
}

int minix_fs_remove_dir_entry(minix_inode_t *parent_inode, const char *filename)
{
    if (!parent_inode || !filename)
    {
        return -1;
    }


    // Buscar la entrada en todas las zonas del directorio
    for (int i = 0; i < 7; i++)
    {
        if (parent_inode->i_zone[i] == 0)
        {
            continue;
        }

        uint8_t block_buffer[MINIX_BLOCK_SIZE];
        if (minix_read_block(parent_inode->i_zone[i], block_buffer) != 0)
        {
            continue;
        }

        minix_dir_entry_t *entries = (minix_dir_entry_t *)block_buffer;
        int num_entries = MINIX_BLOCK_SIZE / sizeof(minix_dir_entry_t);

        for (int j = 0; j < num_entries; j++)
        {
            if (entries[j].inode == 0)
            {
                continue; // Entrada vacía
            }

            if (strcmp(entries[j].name, filename) == 0)
            {
                // Encontramos la entrada, marcarla como libre
                entries[j].inode = 0;
                memset(entries[j].name, 0, MINIX_NAME_LEN);

                // Escribir el bloque actualizado
                if (minix_write_block(parent_inode->i_zone[i], block_buffer) != 0)
                {
                    return -1;
                }

                // Actualizar el tamaño del directorio si es necesario
                if ((size_t)j < parent_inode->i_size / sizeof(minix_dir_entry_t))
                {
                    parent_inode->i_size = j * sizeof(minix_dir_entry_t);
                }

                return 0;
            }
        }
    }

    return -1;
}

// ===============================================================================
// PUBLIC FUNCTIONS IMPLEMENTATION
// ===============================================================================

bool minix_fs_is_available(void)
{
    // Verificar si el driver ATA está disponible
    return ata_is_available();
}

bool minix_fs_is_working(void)
{
    return minix_fs.initialized;
}

int minix_fs_init(void)
{

    // Verificar si ATA está disponible
    if (!ata_is_available())
    {
        return -1;
    }

    // Leer el superblock
    if (minix_read_block(1, &minix_fs.superblock) != 0)
    {
        return -1;
    }

    // Verificar magic number
    if (minix_fs.superblock.s_magic != MINIX_SUPER_MAGIC)
    {
        return minix_fs_format();
    }


    // Leer bitmaps
    if (minix_read_block(minix_fs.superblock.s_imap_blocks, minix_fs.inode_bitmap) != 0)
    {
        return -1;
    }

    if (minix_read_block(minix_fs.superblock.s_zmap_blocks, minix_fs.zone_bitmap) != 0)
    {
        return -1;
    }

    minix_fs.initialized = true;
    return 0;
}

int minix_fs_format(void)
{

    // Inicializar superblock
    memset(&minix_fs.superblock, 0, sizeof(minix_superblock_t));
    minix_fs.superblock.s_magic = MINIX_SUPER_MAGIC;
    minix_fs.superblock.s_ninodes = MINIX_MAX_INODES;
    minix_fs.superblock.s_nzones = MINIX_MAX_ZONES;
    minix_fs.superblock.s_imap_blocks = 1;
    minix_fs.superblock.s_zmap_blocks = 1;
    minix_fs.superblock.s_firstdatazone = 2;
    minix_fs.superblock.s_log_zone_size = 0;
    minix_fs.superblock.s_max_size = 268966912;

    // Escribir superblock
    if (minix_write_block(1, &minix_fs.superblock) != 0)
    {
        return -1;
    }

    // Inicializar bitmaps
    memset(minix_fs.inode_bitmap, 0, MINIX_BLOCK_SIZE);
    memset(minix_fs.zone_bitmap, 0, MINIX_BLOCK_SIZE);

    // Marcar inode 1 como usado (root directory)
    minix_fs.inode_bitmap[0] = 0x01;

    // Escribir bitmaps
    if (minix_write_block(minix_fs.superblock.s_imap_blocks, minix_fs.inode_bitmap) != 0)
    {
        return -1;
    }

    if (minix_write_block(minix_fs.superblock.s_zmap_blocks, minix_fs.zone_bitmap) != 0)
    {
        return -1;
    }

    // Crear inode raíz
    minix_inode_t root_inode;
    memset(&root_inode, 0, sizeof(minix_inode_t));
    root_inode.i_mode = MINIX_IFDIR | MINIX_IRWXU | MINIX_IRGRP | MINIX_IROTH;
    root_inode.i_uid = 0;
    root_inode.i_size = 0;
    root_inode.i_time = 0;
    root_inode.i_gid = 0;
    root_inode.i_nlinks = 1;
    root_inode.i_zone[0] = 0; // No zones for empty directory

    // Escribir inode raíz
    uint8_t inode_block[MINIX_BLOCK_SIZE];
    memset(inode_block, 0, MINIX_BLOCK_SIZE);
    memcpy(inode_block, &root_inode, sizeof(minix_inode_t));

    if (minix_write_block(minix_fs.superblock.s_imap_blocks + 1, inode_block) != 0)
    {
        return -1;
    }

    minix_fs.initialized = true;
    return 0;
}

int minix_fs_mkdir(const char *path)
{
    if (!minix_fs.initialized)
    {
        return -1;
    }

    if (!path || strlen(path) == 0)
    {
        return -1;
    }


    // Parsear el path para obtener directorio padre y nombre
    char parent_path[256];
    char dirname[64];

    if (minix_fs_split_path(path, parent_path, dirname) != 0)
    {
        return -1;
    }

    // Obtener el inode del directorio padre
    minix_inode_t *parent_inode = minix_fs_find_inode(parent_path);
    if (!parent_inode)
    {
        return -1;
    }

    if (!(parent_inode->i_mode & MINIX_IFDIR))
    {
        return -1;
    }

    // Verificar si el directorio ya existe
    uint16_t existing_inode = minix_fs_find_dir_entry(parent_inode, dirname);
    if (existing_inode != 0)
    {
        return -1;
    }

    // Asignar un nuevo inode
    uint16_t new_inode_num = minix_alloc_inode();
    if (new_inode_num == 0)
    {
        return -1;
    }

    // Crear el nuevo inode de directorio
    minix_inode_t new_inode;
    memset(&new_inode, 0, sizeof(minix_inode_t));
    new_inode.i_mode = MINIX_IFDIR | MINIX_IRWXU | MINIX_IRGRP | MINIX_IROTH;
    new_inode.i_uid = 0; // root
    new_inode.i_gid = 0; // root
    new_inode.i_size = 0;
    new_inode.i_time = get_system_time(); // Usar tiempo real del sistema
    new_inode.i_nlinks = 2;               // . y ..
    memset(new_inode.i_zone, 0, sizeof(new_inode.i_zone));

    // Escribir el nuevo inode
    if (minix_fs_write_inode(new_inode_num, &new_inode) != 0)
    {
        minix_fs_free_inode(new_inode_num);
        return -1;
    }

    // Agregar entrada al directorio padre
    if (minix_fs_add_dir_entry(parent_inode, dirname, new_inode_num) != 0)
    {
        minix_fs_free_inode(new_inode_num);
        return -1;
    }

    // Obtener el número de inode del padre
    uint16_t parent_inode_num = minix_fs_get_inode_number(parent_path);

    // Actualizar el inode del padre
    if (parent_inode_num != 0)
    {
        if (minix_fs_write_inode(parent_inode_num, parent_inode) != 0)
        {
            return -1;
        }
    }
    else
    {
        return -1;
    }

    return 0;
}

int minix_fs_ls(const char *path)
{
    if (!minix_fs.initialized)
    {
        return -1;
    }

    const char *target_path = path ? path : "/";

    // Obtener el inode del directorio
    minix_inode_t *dir_inode = minix_fs_find_inode(target_path);
    if (!dir_inode)
    {
        return -1;
    }

    if (!(dir_inode->i_mode & MINIX_IFDIR))
    {
        return -1;
    }

    // Listar todas las entradas del directorio
    bool found_entries = false;
    bool has_zones = false;

    for (int i = 0; i < 7; i++)
    {
        if (dir_inode->i_zone[i] == 0)
        {
            continue;
        }
        has_zones = true;

        uint8_t block_buffer[MINIX_BLOCK_SIZE];
        if (minix_read_block(dir_inode->i_zone[i], block_buffer) != 0)
        {
            continue;
        }

        minix_dir_entry_t *entries = (minix_dir_entry_t *)block_buffer;
        int num_entries = MINIX_BLOCK_SIZE / sizeof(minix_dir_entry_t);

        for (int j = 0; j < num_entries; j++)
        {
            if (entries[j].inode == 0)
            {
                continue; // Entrada vacía
            }

            found_entries = true;

            // Leer el inode para obtener información
            minix_inode_t entry_inode;
            if (minix_read_inode(entries[j].inode, &entry_inode) == 0)
            {
                // Mostrar tipo de archivo
                if (entry_inode.i_mode & MINIX_IFDIR)
                {
                }
                else
                {
                }

                // Mostrar permisos

            }
            else
            {
            }
        }
    }

    if (!has_zones)
    {
    }
    else if (!found_entries)
    {
    }

    return 0;
}

void minix_fs_cleanup(void)
{
    if (minix_fs.initialized)
    {
        minix_fs.initialized = false;
    }
}
