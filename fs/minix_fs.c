// ===============================================================================
// IR0 KERNEL - MINIX FILESYSTEM IMPLEMENTATION
// ===============================================================================

#include "minix_fs.h"
#include <drivers/storage/ata.h>
#include <drivers/timer/clock_system.h>
#include <ir0/print.h>
#include <ir0/stat.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

// External memory functions
extern void *kmalloc(size_t size);
extern void kfree(void *ptr);

// Definir constantes faltantes
#define MINIX_SUPER_MAGIC 0x137F
#define EIO 5 // Input/output error
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

// ===============================================================================
// DISK I/O FUNCTIONS (using ATA driver)
// ===============================================================================

// Declaraciones externas del driver ATA
extern bool ata_read_sectors(uint8_t drive, uint32_t lba, uint8_t num_sectors,
                             void *buffer);
extern bool ata_write_sectors(uint8_t drive, uint32_t lba, uint8_t num_sectors,
                              const void *buffer);

int minix_read_block(uint32_t block_num, void *buffer) {
  uint32_t lba =
      block_num * 2; // 2 sectores de 512 bytes = 1 bloque de 1024 bytes
  uint8_t num_sectors = 2;

  bool success = ata_read_sectors(0, lba, num_sectors, buffer);

  if (success) {
    return 0;
  } else {
    return -1;
  }
}

int minix_write_block(uint32_t block_num, const void *buffer) {
  uint32_t lba =
      block_num * 2; // 2 sectores de 512 bytes = 1 bloque de 1024 bytes
  uint8_t num_sectors = 2;

  bool success = ata_write_sectors(0, lba, num_sectors, buffer);

  if (success) {
    return 0;
  } else {
    return -1;
  }
}

// ===============================================================================
// BITMAP FUNCTIONS
// ===============================================================================

static bool __attribute__((unused)) minix_is_inode_free(uint32_t inode_num) {
  if (inode_num >= MINIX_MAX_INODES)
    return false;

  uint32_t byte = inode_num / 8;
  uint32_t bit = inode_num % 8;

  return !(minix_fs.inode_bitmap[byte] & (1 << bit));
}

void minix_mark_inode_used(uint32_t inode_num) {
  if (inode_num >= MINIX_MAX_INODES)
    return;

  uint32_t byte = inode_num / 8;
  uint32_t bit = inode_num % 8;

  minix_fs.inode_bitmap[byte] |= (1 << bit);
}

void minix_mark_inode_free(uint32_t inode_num) {
  if (inode_num >= MINIX_MAX_INODES)
    return;

  uint32_t byte = inode_num / 8;
  uint32_t bit = inode_num % 8;

  minix_fs.inode_bitmap[byte] &= ~(1 << bit);
}

bool minix_is_zone_free(uint32_t zone_num) {
  if (zone_num < minix_fs.superblock.s_firstdatazone ||
      zone_num >= MINIX_MAX_ZONES) {
    return false;
  }

  // Calcular la posición en el bitmap
  uint32_t byte_index = (zone_num - minix_fs.superblock.s_firstdatazone) / 8;
  uint32_t bit_index = (zone_num - minix_fs.superblock.s_firstdatazone) % 8;

  if (byte_index >= minix_fs.superblock.s_zmap_blocks * MINIX_BLOCK_SIZE) {
    return false;
  }

  // Leer el bloque del bitmap
  uint32_t block_num =
      minix_fs.superblock.s_zmap_blocks + byte_index / MINIX_BLOCK_SIZE;
  uint32_t block_offset = byte_index % MINIX_BLOCK_SIZE;

  uint8_t bitmap_block[MINIX_BLOCK_SIZE];
  if (minix_read_block(block_num, bitmap_block) != 0) {
    return false;
  }

  // Verificar si el bit está libre (1 = libre, 0 = usado)
  return (bitmap_block[block_offset] & (1 << bit_index)) != 0;
}

void minix_mark_zone_used(uint32_t zone_num) {
  if (zone_num < minix_fs.superblock.s_firstdatazone ||
      zone_num >= MINIX_MAX_ZONES) {
    return;
  }

  // Calcular la posición en el bitmap
  uint32_t byte_index = (zone_num - minix_fs.superblock.s_firstdatazone) / 8;
  uint32_t bit_index = (zone_num - minix_fs.superblock.s_firstdatazone) % 8;

  if (byte_index >= minix_fs.superblock.s_zmap_blocks * MINIX_BLOCK_SIZE) {
    return;
  }

  // Leer el bloque del bitmap
  uint32_t block_num =
      minix_fs.superblock.s_zmap_blocks + byte_index / MINIX_BLOCK_SIZE;
  uint32_t block_offset = byte_index % MINIX_BLOCK_SIZE;

  uint8_t bitmap_block[MINIX_BLOCK_SIZE];
  if (minix_read_block(block_num, bitmap_block) != 0) {
    return;
  }

  // Marcar la zona como usada (bit = 0)
  bitmap_block[block_offset] &= ~(1 << bit_index);

  // Escribir el bloque actualizado
  minix_write_block(block_num, bitmap_block);
}

uint32_t minix_alloc_zone(void) {
  for (uint32_t i = minix_fs.superblock.s_firstdatazone; i < MINIX_MAX_ZONES;
       i++) {
    if (minix_is_zone_free(i)) {
      minix_mark_zone_used(i);
      return i;
    }
  }
  return 0; // No hay zonas libres
}

void minix_free_zone(uint32_t zone_num) {
  if (zone_num < minix_fs.superblock.s_firstdatazone ||
      zone_num >= MINIX_MAX_ZONES) {
    return;
  }

  // Calcular la posición en el bitmap
  uint32_t byte_index = (zone_num - minix_fs.superblock.s_firstdatazone) / 8;
  uint32_t bit_index = (zone_num - minix_fs.superblock.s_firstdatazone) % 8;

  if (byte_index >= minix_fs.superblock.s_zmap_blocks * MINIX_BLOCK_SIZE) {
    return;
  }

  // Leer el bloque del bitmap si no está en memoria
  uint32_t block_num =
      minix_fs.superblock.s_zmap_blocks + byte_index / MINIX_BLOCK_SIZE;
  uint32_t block_offset = byte_index % MINIX_BLOCK_SIZE;

  uint8_t bitmap_block[MINIX_BLOCK_SIZE];
  if (minix_read_block(block_num, bitmap_block) != 0) {
    return;
  }

  // Marcar la zona como libre (bit = 1)
  bitmap_block[block_offset] |= (1 << bit_index);

  // Escribir el bloque actualizado
  if (minix_write_block(block_num, bitmap_block) != 0) {
    return;
  }
}

// ===============================================================================
// INODE FUNCTIONS
// ===============================================================================

static int minix_read_inode(uint32_t inode_num, minix_inode_t *inode) {
  if (inode_num == 0 || inode_num >= MINIX_MAX_INODES || !inode) {
    return -1;
  }

  // Calcular posición del inode en el disco
  // Para MINIX: inode table starts after superblock + bitmaps
  // Block layout: 0=boot, 1=super, 2=imap, 3=zmap, 4=inodes, 5+=data
  uint32_t inode_table_start =
      1 + minix_fs.superblock.s_imap_blocks + minix_fs.superblock.s_zmap_blocks;
  uint32_t inode_block =
      inode_table_start +
      ((inode_num - 1) * sizeof(minix_inode_t)) / MINIX_BLOCK_SIZE;
  uint32_t inode_offset =
      ((inode_num - 1) * sizeof(minix_inode_t)) % MINIX_BLOCK_SIZE;

  uint8_t block_buffer[MINIX_BLOCK_SIZE];
  int result = minix_read_block(inode_block, block_buffer);
  if (result != 0) {
    return -1;
  }

  // Copiar inode del buffer
  memcpy(inode, block_buffer + inode_offset, sizeof(minix_inode_t));

  return 0;
}

static int __attribute__((unused))
minix_write_inode(uint32_t inode_num, const minix_inode_t *inode) {
  if (inode_num == 0 || inode_num >= MINIX_MAX_INODES || !inode) {
    return -1;
  }

  // Calcular posición del inode en el disco
  uint32_t inode_block = minix_fs.superblock.s_imap_blocks + 1 +
                         (inode_num * sizeof(minix_inode_t)) / MINIX_BLOCK_SIZE;
  uint32_t inode_offset =
      (inode_num * sizeof(minix_inode_t)) % MINIX_BLOCK_SIZE;

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

uint32_t minix_alloc_inode(void) {
  for (uint32_t i = 1; i < MINIX_MAX_INODES; i++) {
    uint32_t byte = i / 8;
    uint32_t bit = i % 8;

    if (!(minix_fs.inode_bitmap[byte] & (1 << bit))) {
      minix_fs.inode_bitmap[byte] |= (1 << bit);
      return i;
    }
  }

  return 0; // No hay inodes libres
}

// ===============================================================================
// MINIX FILESYSTEM FUNCTIONS IMPLEMENTATION
// ===============================================================================

// Global static inodes to avoid memory issues
static minix_inode_t cached_root_inode;
static minix_inode_t cached_result_inode;
static bool root_inode_cached = false;

minix_inode_t *minix_fs_find_inode(const char *pathname) {
  extern void print(const char *str);

  if (!pathname || !minix_fs.initialized) {
    return NULL;
  }

  // FORCE REAL DISK USAGE - Always try to read from disk
  // (Skip ATA check for QEMU compatibility)

  // Si es el directorio raíz
  if (strcmp(pathname, "/") == 0) {
    // Use cached root inode if available
    if (!root_inode_cached) {
      if (minix_read_inode(MINIX_ROOT_INODE, &cached_root_inode) == 0) {
        root_inode_cached = true;
      } else {
        return NULL;
      }
    }

    return &cached_root_inode;
  }

  // Parsear el path
  char path_copy[256];
  strncpy(path_copy, pathname, sizeof(path_copy) - 1);
  path_copy[sizeof(path_copy) - 1] = '\0';

  // Empezar desde el inode raíz
  minix_inode_t current_inode;
  if (minix_read_inode(MINIX_ROOT_INODE, &current_inode) != 0) {
    return NULL;
  }

  // Dividir el path en componentes
  char *token = strtok(path_copy, "/");
  while (token != NULL) {
    // Verificar que el inode actual es un directorio
    if (!(current_inode.i_mode & MINIX_IFDIR)) {
      return NULL;
    }

    // Buscar la entrada en el directorio actual
    uint16_t found_inode = minix_fs_find_dir_entry(&current_inode, token);
    if (found_inode == 0) {
      return NULL;
    }

    // Leer el inode encontrado
    if (minix_read_inode(found_inode, &current_inode) != 0) {
      return NULL;
    }

    token = strtok(NULL, "/");
  }

  // Retornar una copia estática del inode encontrado
  memcpy(&cached_result_inode, &current_inode, sizeof(minix_inode_t));

  return &cached_result_inode;
}

// Función auxiliar para obtener el número de inode de un path
static uint16_t minix_fs_get_inode_number(const char *pathname) {
  if (!pathname || !minix_fs.initialized) {
    return 0;
  }

  // Si es el directorio raíz
  if (strcmp(pathname, "/") == 0) {
    return MINIX_ROOT_INODE;
  }

  // Parsear el path
  char path_copy[256];
  strncpy(path_copy, pathname, sizeof(path_copy) - 1);
  path_copy[sizeof(path_copy) - 1] = '\0';

  // Empezar desde el inode raíz
  minix_inode_t current_inode;
  if (minix_read_inode(MINIX_ROOT_INODE, &current_inode) != 0) {
    return 0;
  }

  // Dividir el path en componentes
  char *token = strtok(path_copy, "/");
  uint16_t current_inode_num = MINIX_ROOT_INODE;

  while (token != NULL) {
    // Verificar que el inode actual es un directorio
    if (!(current_inode.i_mode & MINIX_IFDIR)) {
      return 0;
    }

    // Buscar la entrada en el directorio actual
    uint16_t found_inode = minix_fs_find_dir_entry(&current_inode, token);
    if (found_inode == 0) {
      return 0;
    }

    current_inode_num = found_inode;

    // Leer el inode encontrado
    if (minix_read_inode(found_inode, &current_inode) != 0) {
      return 0;
    }

    token = strtok(NULL, "/");
  }

  return current_inode_num;
}

// ===============================================================================
// DIRECTORY ENTRY FUNCTIONS
// ===============================================================================

uint16_t minix_fs_find_dir_entry(const minix_inode_t *dir_inode,
                                 const char *name) {
  if (!dir_inode || !name || !(dir_inode->i_mode & MINIX_IFDIR)) {
    return 0;
  }

  // Leer todas las zonas del directorio
  for (int i = 0; i < 7; i++) {
    if (dir_inode->i_zone[i] == 0) {
      continue; // Zona vacía
    }

    uint8_t block_buffer[MINIX_BLOCK_SIZE];
    if (minix_read_block(dir_inode->i_zone[i], block_buffer) != 0) {
      continue;
    }

    // Buscar en las entradas del directorio
    minix_dir_entry_t *entries = (minix_dir_entry_t *)block_buffer;
    int num_entries = MINIX_BLOCK_SIZE / sizeof(minix_dir_entry_t);

    for (int j = 0; j < num_entries; j++) {
      if (entries[j].inode == 0) {
        continue; // Entrada vacía
      }

      if (strcmp(entries[j].name, name) == 0) {
        return entries[j].inode;
      }
    }
  }

  return 0;
}

int minix_fs_write_inode(uint16_t inode_num, const minix_inode_t *inode) {
  if (!inode || inode_num == 0) {
    return -1;
  }

  // Calcular la posición del inode en el disco
  uint32_t inode_block =
      minix_fs.superblock.s_imap_blocks + 1 +
      (inode_num - 1) / (MINIX_BLOCK_SIZE / MINIX_INODE_SIZE);
  uint32_t inode_offset =
      ((inode_num - 1) % (MINIX_BLOCK_SIZE / MINIX_INODE_SIZE)) *
      MINIX_INODE_SIZE;

  // Leer el bloque que contiene el inode
  uint8_t block_buffer[MINIX_BLOCK_SIZE];
  if (minix_read_block(inode_block, block_buffer) != 0) {
    return -1;
  }

  // Copiar el inode al buffer
  memcpy(block_buffer + inode_offset, inode, MINIX_INODE_SIZE);

  // Escribir el bloque actualizado
  if (minix_write_block(inode_block, block_buffer) != 0) {
    return -1;
  }

  return 0;
}

int minix_fs_free_inode(uint16_t inode_num) {
  if (inode_num == 0 || inode_num > minix_fs.superblock.s_ninodes) {
    return -1;
  }

  // Calcular la posición en el bitmap de inodes
  uint32_t byte_index = (inode_num - 1) / 8;
  uint32_t bit_index = (inode_num - 1) % 8;

  if (byte_index >= minix_fs.superblock.s_imap_blocks * MINIX_BLOCK_SIZE) {
    return -1;
  }

  // Leer el bloque del bitmap
  uint32_t block_num = 1 + byte_index / MINIX_BLOCK_SIZE;
  uint32_t block_offset = byte_index % MINIX_BLOCK_SIZE;

  uint8_t bitmap_block[MINIX_BLOCK_SIZE];
  if (minix_read_block(block_num, bitmap_block) != 0) {
    return -1;
  }

  // Marcar el inode como libre (bit = 1)
  bitmap_block[block_offset] |= (1 << bit_index);

  // Escribir el bloque actualizado
  if (minix_write_block(block_num, bitmap_block) != 0) {
    return -1;
  }

  return 0;
}

int minix_fs_split_path(const char *pathname, char *parent_path,
                        char *filename) {
  if (!pathname || !parent_path || !filename) {
    return -1;
  }

  // Encontrar la última barra
  const char *last_slash = strrchr(pathname, '/');
  if (!last_slash) {
    // No hay barra, el archivo está en el directorio actual
    strcpy(parent_path, ".");
    strcpy(filename, pathname);
    return 0;
  }

  if (last_slash == pathname) {
    // Es el directorio raíz
    strcpy(parent_path, "/");
  } else {
    // Copiar la parte del directorio padre
    size_t parent_len = last_slash - pathname;
    strncpy(parent_path, pathname, parent_len);
    parent_path[parent_len] = '\0';
  }

  // Copiar el nombre del archivo
  strcpy(filename, last_slash + 1);

  return 0;
}

int minix_fs_add_dir_entry(minix_inode_t *parent_inode, const char *filename,
                           uint16_t inode_num) {
  if (!parent_inode || !filename || inode_num == 0) {
    return -1;
  }

  uint32_t target_zone = 0;
  uint32_t target_block = 0;
  int target_entry = -1;

  for (int i = 0; i < 7; i++) {
    if (parent_inode->i_zone[i] == 0) {
      continue;
    }

    uint8_t block_buffer[MINIX_BLOCK_SIZE];
    if (minix_read_block(parent_inode->i_zone[i], block_buffer) != 0) {
      continue;
    }

    minix_dir_entry_t *entries = (minix_dir_entry_t *)block_buffer;
    int num_entries = MINIX_BLOCK_SIZE / sizeof(minix_dir_entry_t);

    for (int j = 0; j < num_entries; j++) {
      if (entries[j].inode == 0) {
        // Encontramos una entrada libre
        target_zone = parent_inode->i_zone[i];
        target_block = parent_inode->i_zone[i];
        target_entry = j;
        break;
      }
    }

    if (target_entry != -1) {
      break;
    }
  }

  // Si no encontramos espacio, asignar una nueva zona
  if (target_entry == -1) {
    for (int i = 0; i < 7; i++) {
      if (parent_inode->i_zone[i] == 0) {
        target_zone = minix_alloc_zone();
        if (target_zone != 0) {
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

  if (target_entry == -1) {
    return -1;
  }

  // Leer el bloque donde agregaremos la entrada
  uint8_t block_buffer[MINIX_BLOCK_SIZE];
  if (minix_read_block(target_block, block_buffer) != 0) {
    return -1;
  }

  // Agregar la nueva entrada
  minix_dir_entry_t *entries = (minix_dir_entry_t *)block_buffer;
  entries[target_entry].inode = inode_num;
  strncpy(entries[target_entry].name, filename, MINIX_NAME_LEN);
  entries[target_entry].name[MINIX_NAME_LEN - 1] = '\0';

  // Escribir el bloque actualizado
  if (minix_write_block(target_block, block_buffer) != 0) {
    return -1;
  }

  // Actualizar el tamaño del directorio si es necesario
  if ((size_t)target_entry >=
      parent_inode->i_size / sizeof(minix_dir_entry_t)) {
    parent_inode->i_size = (target_entry + 1) * sizeof(minix_dir_entry_t);
  }

  return 0;
}

int minix_fs_remove_dir_entry(minix_inode_t *parent_inode,
                              const char *filename) {
  if (!parent_inode || !filename) {
    return -1;
  }

  // Buscar la entrada en todas las zonas del directorio
  for (int i = 0; i < 7; i++) {
    if (parent_inode->i_zone[i] == 0) {
      continue;
    }

    uint8_t block_buffer[MINIX_BLOCK_SIZE];
    if (minix_read_block(parent_inode->i_zone[i], block_buffer) != 0) {
      continue;
    }

    minix_dir_entry_t *entries = (minix_dir_entry_t *)block_buffer;
    int num_entries = MINIX_BLOCK_SIZE / sizeof(minix_dir_entry_t);

    for (int j = 0; j < num_entries; j++) {
      if (entries[j].inode == 0) {
        continue; // Entrada vacía
      }

      if (strcmp(entries[j].name, filename) == 0) {
        // Encontramos la entrada, marcarla como libre
        entries[j].inode = 0;
        memset(entries[j].name, 0, MINIX_NAME_LEN);

        // Escribir el bloque actualizado
        if (minix_write_block(parent_inode->i_zone[i], block_buffer) != 0) {
          return -1;
        }

        // Actualizar el tamaño del directorio si es necesario
        if ((size_t)j < parent_inode->i_size / sizeof(minix_dir_entry_t)) {
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

bool minix_fs_is_available(void) {
  // Verificar si el driver ATA está disponible
  return ata_is_available();
}

bool minix_fs_is_working(void) { return minix_fs.initialized; }

int minix_fs_init(void) {
  // FORCE REAL DISK USAGE - In QEMU, disk is always available

  // Read superblock from disk
  if (minix_read_block(1, &minix_fs.superblock) != 0) {
    // Can't read - format disk
    return minix_fs_format();
  }

  // Check magic number
  if (minix_fs.superblock.s_magic != MINIX_SUPER_MAGIC) {
    // Invalid - format disk
    return minix_fs_format();
  }

  // Read bitmaps
  if (minix_read_block(minix_fs.superblock.s_imap_blocks,
                       minix_fs.inode_bitmap) != 0) {
    return minix_fs_format();
  }

  if (minix_read_block(minix_fs.superblock.s_zmap_blocks,
                       minix_fs.zone_bitmap) != 0) {
    return minix_fs_format();
  }

  // Valid filesystem found
  minix_fs.initialized = true;
  root_inode_cached = false; // Reset cache
  return 0;
}

int minix_fs_format(void) {
  // REAL MINIX FILESYSTEM CREATION

  // Initialize superblock
  memset(&minix_fs.superblock, 0, sizeof(minix_superblock_t));
  minix_fs.superblock.s_magic = MINIX_SUPER_MAGIC;
  minix_fs.superblock.s_ninodes = 64;    // Small but real
  minix_fs.superblock.s_nzones = 1024;   // 1MB of data
  minix_fs.superblock.s_imap_blocks = 1; // 1 block for inode bitmap
  minix_fs.superblock.s_zmap_blocks = 1; // 1 block for zone bitmap
  minix_fs.superblock.s_firstdatazone =
      5; // Block layout: 0=boot, 1=super, 2=imap, 3=zmap, 4=inodes, 5+=data
  minix_fs.superblock.s_log_zone_size = 0;
  minix_fs.superblock.s_max_size = 1048576; // 1MB max file size

  // Escribir superblock
  if (minix_write_block(1, &minix_fs.superblock) != 0) {
    return -1;
  }

  // Inicializar bitmaps
  memset(minix_fs.inode_bitmap, 0, MINIX_BLOCK_SIZE);
  memset(minix_fs.zone_bitmap, 0, MINIX_BLOCK_SIZE);

  // Marcar inode 1 como usado (root directory)
  minix_fs.inode_bitmap[0] = 0x01;

  // Escribir bitmaps
  if (minix_write_block(minix_fs.superblock.s_imap_blocks,
                        minix_fs.inode_bitmap) != 0) {
    return -1;
  }

  if (minix_write_block(minix_fs.superblock.s_zmap_blocks,
                        minix_fs.zone_bitmap) != 0) {
    return -1;
  }

  // Crear inode raíz
  minix_inode_t root_inode;
  memset(&root_inode, 0, sizeof(minix_inode_t));
  root_inode.i_mode = MINIX_IFDIR | MINIX_IRWXU | MINIX_IRGRP | MINIX_IROTH;
  root_inode.i_uid = 0;
  root_inode.i_size = 2 * sizeof(minix_dir_entry_t); // . and .. entries
  root_inode.i_time = 0;
  root_inode.i_gid = 0;
  root_inode.i_nlinks = 2;                                    // . and .. links
  root_inode.i_zone[0] = minix_fs.superblock.s_firstdatazone; // First data zone

  // Escribir inode raíz (inode 1)
  uint8_t inode_block[MINIX_BLOCK_SIZE];
  memset(inode_block, 0, MINIX_BLOCK_SIZE);
  memcpy(inode_block, &root_inode, sizeof(minix_inode_t));

  if (minix_write_block(3, inode_block) != 0) // Block 3 = inode table
  {
    return -1;
  }

  // Crear directorio root con entradas . y ..
  uint8_t root_dir_block[MINIX_BLOCK_SIZE];
  memset(root_dir_block, 0, MINIX_BLOCK_SIZE);

  minix_dir_entry_t *entries = (minix_dir_entry_t *)root_dir_block;

  // Entrada "."
  entries[0].inode = 1; // Root inode
  strcpy(entries[0].name, ".");

  // Entrada ".."
  entries[1].inode = 1; // Root inode (parent of root is root)
  strcpy(entries[1].name, "..");

  // Escribir directorio root
  if (minix_write_block(minix_fs.superblock.s_firstdatazone, root_dir_block) !=
      0) {
    return -1;
  }

  // Solo escribir el inode del directorio root (inode 1)
  if (minix_write_block(3, inode_block) != 0) {
    return -1;
  }

  // Marcar solo el inode 1 como usado en bitmap
  minix_fs.inode_bitmap[0] |= 0x01; // Bit 0 = inode 1

  // Escribir bitmap de inodes
  if (minix_write_block(2, minix_fs.inode_bitmap) != 0) {
    return -1;
  }

  minix_fs.initialized = true;
  root_inode_cached = false; // Reset cache
  return 0;
}

int minix_fs_mkdir(const char *path, mode_t mode) {
  if (!minix_fs.initialized) {
    return -1;
  }

  if (!path || strlen(path) == 0) {
    return -1;
  }

  // Verificar que el disco esté disponible
  if (!ata_is_available()) {
    return -EIO; // Error real - no hay disco disponible
  }

  // Parsear el path para obtener directorio padre y nombre
  char parent_path[256];
  char dirname[64];

  if (minix_fs_split_path(path, parent_path, dirname) != 0) {
    return -1;
  }

  // Obtener el inode del directorio padre
  minix_inode_t *parent_inode = minix_fs_find_inode(parent_path);
  if (!parent_inode) {
    return -1;
  }

  if (!(parent_inode->i_mode & MINIX_IFDIR)) {
    return -1;
  }

  // Verificar si el directorio ya existe
  uint16_t existing_inode = minix_fs_find_dir_entry(parent_inode, dirname);
  if (existing_inode != 0) {
    return -1;
  }

  // Asignar un nuevo inode
  uint16_t new_inode_num = minix_alloc_inode();
  if (new_inode_num == 0) {
    return -1;
  }

  // Crear el nuevo inode de directorio
  minix_inode_t new_inode;
  memset(&new_inode, 0, sizeof(minix_inode_t));
  new_inode.i_mode = MINIX_IFDIR | (mode & 0777);
  new_inode.i_uid = 0; // root
  new_inode.i_gid = 0; // root
  new_inode.i_size = 0;
  new_inode.i_time = get_system_time(); // Usar tiempo real del sistema
  new_inode.i_nlinks = 2;               // . y ..
  memset(new_inode.i_zone, 0, sizeof(new_inode.i_zone));

  // Escribir el nuevo inode
  if (minix_fs_write_inode(new_inode_num, &new_inode) != 0) {
    minix_fs_free_inode(new_inode_num);
    return -1;
  }

  // Agregar entrada al directorio padre
  if (minix_fs_add_dir_entry(parent_inode, dirname, new_inode_num) != 0) {
    minix_fs_free_inode(new_inode_num);
    return -1;
  }

  // Obtener el número de inode del padre
  uint16_t parent_inode_num = minix_fs_get_inode_number(parent_path);

  // Actualizar el inode del padre
  if (parent_inode_num != 0) {
    if (minix_fs_write_inode(parent_inode_num, parent_inode) != 0) {
      return -1;
    }
  } else {
    return -1;
  }

  return 0;
}

int minix_fs_ls(const char *path) {
  if (!minix_fs.initialized) {
    return -1;
  }

  const char *target_path = path ? path : "/";

  // Verificar que el disco esté disponible
  if (!ata_is_available()) {
    extern int64_t sys_write(int fd, const void *buf, size_t count);
    sys_write(2, "ls: disk not available\n", 23);
    return -EIO; // Error real - no hay disco disponible
  }

  // Obtener el inode del directorio (solo si hay disco real)
  minix_inode_t *dir_inode = minix_fs_find_inode(target_path);
  if (!dir_inode) {
    extern int64_t sys_write(int fd, const void *buf, size_t count);
    sys_write(2, "Directory not found: ", 21);
    sys_write(2, target_path, strlen(target_path));
    sys_write(2, "\n", 1);
    return -1;
  }

  if (!minix_is_dir(dir_inode)) {
    extern int64_t sys_write(int fd, const void *buf, size_t count);
    sys_write(2, "Not a directory: ", 17);
    sys_write(2, target_path, strlen(target_path));
    sys_write(2, "\n", 1);
    return -1;
  }

  // Listar todas las entradas del directorio
  bool found_entries = false;
  bool has_zones = false;

  for (int i = 0; i < 7; i++) {
    if (dir_inode->i_zone[i] == 0) {
      continue;
    }
    has_zones = true;

    uint8_t block_buffer[MINIX_BLOCK_SIZE];
    if (minix_read_block(dir_inode->i_zone[i], block_buffer) != 0) {
      continue;
    }

    minix_dir_entry_t *entries = (minix_dir_entry_t *)block_buffer;
    int num_entries = MINIX_BLOCK_SIZE / sizeof(minix_dir_entry_t);

    for (int j = 0; j < num_entries; j++) {
      if (entries[j].inode == 0) {
        continue; // Entrada vacía
      }

      found_entries = true;

      // Leer el inode para obtener información
      minix_inode_t entry_inode;
      if (minix_read_inode(entries[j].inode, &entry_inode) == 0) {
        // Mostrar tipo de archivo
        if (entry_inode.i_mode & MINIX_IFDIR) {
          print("d");
        } else {
          print("-");
        }

        // Mostrar permisos básicos (removido hardcode)

        // Mostrar tamaño
        print_uint32(entry_inode.i_size);
        print(" ");

        // Mostrar nombre
        print(entries[j].name);
        print("\n");
      } else {
        print("? ??? ");
        print(entries[j].name);
        print(" (inode error)\n");
      }
    }
  }

  if (!has_zones) {
    print("Directory has no data blocks\n");
  } else if (!found_entries) {
    print("Directory is empty\n");
  }

  return 0;
}

void minix_fs_cleanup(void) {
  if (minix_fs.initialized) {
    minix_fs.initialized = false;
  }
}

int minix_fs_cat(const char *path) {
  if (!minix_fs.initialized) {
    return -1;
  }

  if (!path) {
    return -1;
  }

  // Verificar que el disco esté disponible
  if (!ata_is_available()) {
    print("cat: disk not available\n");
    return -EIO; // Error real - no hay disco disponible
  }

  // Obtener el inode del archivo
  minix_inode_t *file_inode = minix_fs_find_inode(path);
  if (!file_inode) {
    print("Error: File '");
    print(path);
    print("' not found\n");
    return -1;
  }

  // Verificar que sea un archivo regular
  if (file_inode->i_mode & MINIX_IFDIR) {
    print("Error: '");
    print(path);
    print("' is a directory\n");
    return -1;
  }

  print("\n=== File: ");
  print(path);
  print(" ===\n");

  // Leer y mostrar el contenido del archivo
  uint32_t file_size = file_inode->i_size;
  uint32_t bytes_read = 0;

  for (int i = 0; i < 7 && bytes_read < file_size; i++) {
    if (file_inode->i_zone[i] == 0) {
      continue;
    }

    uint8_t block_buffer[MINIX_BLOCK_SIZE];
    if (minix_read_block(file_inode->i_zone[i], block_buffer) != 0) {
      print("Error reading block ");
      print_uint32(i);
      print("\n");
      continue;
    }

    // Mostrar el contenido del bloque
    uint32_t bytes_to_show = MINIX_BLOCK_SIZE;
    if (bytes_read + bytes_to_show > file_size) {
      bytes_to_show = file_size - bytes_read;
    }

    for (uint32_t j = 0; j < bytes_to_show; j++) {
      char c = block_buffer[j];
      if (c == '\0')
        break; // End of string

      if (c >= 32 && c < 127) {
        // Printable character
        char str[2] = {c, '\0'};
        print(str);
      } else if (c == '\n') {
        print("\n");
      } else if (c == '\t') {
        print("    "); // Tab as 4 spaces
      } else {
        // Non-printable character, show as hex
        print("\\x");
        print_hex_compact(c);
      }
    }

    bytes_read += bytes_to_show;
  }

  print("\n--- End of file (");
  print_uint32(bytes_read);
  print(" bytes) ---\n");

  return 0;
}

// ===============================================================================
// FUNCIÓN PARA ESCRIBIR CONTENIDO A ARCHIVOS
// ===============================================================================

int minix_fs_write_file(const char *path, const char *content) {
  extern void serial_print(const char *str);
  extern void serial_print_hex32(uint32_t num);

  serial_print("SERIAL: minix_fs_write_file called\n");

  if (!minix_fs.initialized) {
    serial_print("SERIAL: minix_fs_write_file: filesystem not initialized\n");
    return -1;
  }

  if (!path) {
    serial_print("SERIAL: minix_fs_write_file: path is NULL\n");
    return -1;
  }

  if (!content) {
    serial_print("SERIAL: minix_fs_write_file: content is NULL\n");
    return -1;
  }

  serial_print("SERIAL: minix_fs_write_file: path=");
  serial_print(path);
  serial_print(" content=");
  serial_print(content);
  serial_print("\n");

  // Verificar que el disco esté disponible
  if (!ata_is_available()) {
    extern void serial_print(const char *str);
    serial_print("SERIAL: write: disk not available\n");
    return -EIO;
  }

  // Buscar si el archivo ya existe
  minix_inode_t *file_inode = minix_fs_find_inode(path);
  uint16_t inode_num = 0;

  if (file_inode) {
    // El archivo existe, obtener su número de inode
    inode_num = minix_fs_get_inode_number(path);
    if (inode_num == 0) {
      return -1;
    }
  } else {
    // El archivo no existe, crearlo primero
    if (minix_fs_touch(path, 0644) != 0) {
      extern void serial_print(const char *str);
      serial_print("SERIAL: Error: Could not create file ");
      serial_print(path);
      serial_print("\n");
      return -1;
    }

    // Obtener el inode del archivo recién creado
    file_inode = minix_fs_find_inode(path);
    if (!file_inode) {
      return -1;
    }

    inode_num = minix_fs_get_inode_number(path);
    if (inode_num == 0) {
      return -1;
    }
  }

  // Calcular el tamaño del contenido
  uint32_t content_size = strlen(content);

  // Verificar que no exceda el tamaño máximo de archivo
  if (content_size > MINIX_BLOCK_SIZE * 7) {
    extern void serial_print(const char *str);
    extern void serial_print_hex32(uint32_t num);
    serial_print("SERIAL: Error: Content too large (max ");
    serial_print_hex32(MINIX_BLOCK_SIZE * 7);
    serial_print(" bytes)\n");
    return -1;
  }

  // Asignar zona si el archivo no tiene ninguna
  if (file_inode->i_zone[0] == 0) {
    uint32_t new_zone = minix_alloc_zone();
    if (new_zone == 0) {
      extern void serial_print(const char *str);
      serial_print("SERIAL: Error: No free zones available\n");
      return -1;
    }
    file_inode->i_zone[0] = new_zone;
  }

  // Escribir el contenido al primer bloque
  uint8_t block_buffer[MINIX_BLOCK_SIZE];
  memset(block_buffer, 0, MINIX_BLOCK_SIZE);

  // Copiar el contenido al buffer
  uint32_t bytes_to_write = content_size;
  if (bytes_to_write > MINIX_BLOCK_SIZE) {
    bytes_to_write = MINIX_BLOCK_SIZE;
  }

  memcpy(block_buffer, content, bytes_to_write);

  // Escribir el bloque al disco
  if (minix_write_block(file_inode->i_zone[0], block_buffer) != 0) {
    extern void serial_print(const char *str);
    serial_print("SERIAL: Error: Could not write block to disk\n");
    return -1;
  }

  // Actualizar el tamaño del archivo en el inode
  file_inode->i_size = content_size;

  // Escribir el inode actualizado al disco
  if (minix_fs_write_inode(inode_num, file_inode) != 0) {
    extern void serial_print(const char *str);
    serial_print("SERIAL: Error: Could not update inode\n");
    return -1;
  }

  extern void serial_print(const char *str);
  extern void serial_print_hex32(uint32_t num);
  serial_print("SERIAL: File '");
  serial_print(path);
  serial_print("' written successfully (");
  serial_print_hex32(content_size);
  serial_print(" bytes)\n");

  return 0;
}

// ===============================================================================
// FUNCIÓN PARA CREAR ARCHIVOS (TOUCH)
// ===============================================================================

int minix_fs_touch(const char *path, mode_t mode) {
  if (!minix_fs.initialized) {
    return -1;
  }

  if (!path || strlen(path) == 0) {
    return -1;
  }

  // Verificar que el disco esté disponible
  if (!ata_is_available()) {
    return -EIO; // Error real - no hay disco disponible
  }

  // Verificar si el archivo ya existe
  minix_inode_t *existing_inode = minix_fs_find_inode(path);
  if (existing_inode) {
    print("File '");
    print(path);
    print("' already exists\n");
    return 0; // No error, just update timestamp (simplified)
  }

  // Parsear el path para obtener directorio padre y nombre
  char parent_path[256];
  char filename[64];

  if (minix_fs_split_path(path, parent_path, filename) != 0) {
    print("Error: Invalid path\n");
    return -1;
  }

  // Obtener el inode del directorio padre
  minix_inode_t *parent_inode = minix_fs_find_inode(parent_path);
  if (!parent_inode) {
    print("Error: Parent directory not found\n");
    return -1;
  }

  if (!(parent_inode->i_mode & MINIX_IFDIR)) {
    print("Error: Parent is not a directory\n");
    return -1;
  }

  // Obtener un inode libre
  uint16_t new_inode_num = minix_alloc_inode();
  if (new_inode_num == 0) {
    print("Error: No free inodes available\n");
    return -1;
  }

  // Crear el nuevo inode para el archivo
  minix_inode_t new_inode = {0};
  new_inode.i_mode = MINIX_IFREG | (mode & 0777);
  new_inode.i_uid = 0;
  new_inode.i_size = 0; // Empty file
  new_inode.i_time = 0; // Simplified timestamp
  new_inode.i_gid = 0;
  new_inode.i_nlinks = 1;

  // Escribir el nuevo inode
  if (minix_fs_write_inode(new_inode_num, &new_inode) != 0) {
    minix_fs_free_inode(new_inode_num);
    print("Error: Could not write inode\n");
    return -1;
  }

  // Agregar entrada al directorio padre
  if (minix_fs_add_dir_entry(parent_inode, filename, new_inode_num) != 0) {
    minix_fs_free_inode(new_inode_num);
    print("Error: Could not add directory entry\n");
    return -1;
  }

  // Actualizar el inode del padre
  uint16_t parent_inode_num = minix_fs_get_inode_number(parent_path);
  if (parent_inode_num != 0) {
    if (minix_fs_write_inode(parent_inode_num, parent_inode) != 0) {
      print("Warning: Could not update parent directory\n");
    }
  }

  print("File '");
  print(path);
  print("' created successfully\n");

  return 0;
}

// ===============================================================================
// FUNCIÓN PARA ELIMINAR ARCHIVOS (RM)
// ===============================================================================

int minix_fs_rm(const char *path) {
  if (!minix_fs.initialized) {
    print("Error: MINIX filesystem not initialized\n");
    return -1;
  }

  if (!path || strlen(path) == 0) {
    print("Error: No file path specified\n");
    return -1;
  }

  // Obtener el inode del archivo
  minix_inode_t *file_inode = minix_fs_find_inode(path);
  if (!file_inode) {
    print("Error: File '");
    print(path);
    print("' not found\n");
    return -1;
  }

  // Verificar que no sea un directorio
  if (file_inode->i_mode & MINIX_IFDIR) {
    print("Error: '");
    print(path);
    print("' is a directory (use rmdir)\n");
    return -1;
  }

  // Parsear el path para obtener directorio padre y nombre
  char parent_path[256];
  char filename[64];

  if (minix_fs_split_path(path, parent_path, filename) != 0) {
    print("Error: Invalid path\n");
    return -1;
  }

  // Obtener el inode del directorio padre
  minix_inode_t *parent_inode = minix_fs_find_inode(parent_path);
  if (!parent_inode) {
    print("Error: Parent directory not found\n");
    return -1;
  }

  // Obtener el número de inode del archivo
  uint16_t file_inode_num = minix_fs_get_inode_number(path);
  if (file_inode_num == 0) {
    print("Error: Could not get inode number\n");
    return -1;
  }

  // Eliminar entrada del directorio padre
  if (minix_fs_remove_dir_entry(parent_inode, filename) != 0) {
    print("Error: Could not remove directory entry\n");
    return -1;
  }

  // Liberar el inode
  if (minix_fs_free_inode(file_inode_num) != 0) {
    print("Error: Could not free inode\n");
    return -1;
  }

  // Actualizar el inode del padre
  uint16_t parent_inode_num = minix_fs_get_inode_number(parent_path);
  if (parent_inode_num != 0) {
    if (minix_fs_write_inode(parent_inode_num, parent_inode) != 0) {
      print("Warning: Could not update parent directory\n");
    }
  }

  print("File '");
  print(path);
  print("' removed successfully\n");

  return 0;
}

// Removed unused function format_permissions

// Removed unused function uint32_to_str

// This function ensures the disk has a valid MINIX filesystem
// If not, it creates one with a basic root directory
int minix_fs_ensure_valid(void) {
  // Try to read superblock
  if (minix_read_block(1, &minix_fs.superblock) != 0) {
    // Can't read - format disk
    return minix_fs_format();
  }

  // Check magic number
  if (minix_fs.superblock.s_magic != MINIX_SUPER_MAGIC) {
    // Invalid - format disk
    return minix_fs_format();
  }

  // Valid filesystem found
  return 0;
}

int minix_fs_rmdir(const char *path) {
  if (!minix_fs.initialized) {
    print("Error: MINIX filesystem not initialized\n");
    return -1;
  }

  if (!path || strlen(path) == 0) {
    print("Error: No directory path specified\n");
    return -1;
  }

  // No permitir eliminar el directorio raíz
  if (strcmp(path, "/") == 0) {
    print("Error: Cannot remove root directory\n");
    return -1;
  }

  // Obtener el inode del directorio
  minix_inode_t *dir_inode = minix_fs_find_inode(path);
  if (!dir_inode) {
    print("Error: Directory '");
    print(path);
    print("' not found\n");
    return -1;
  }

  // Verificar que sea un directorio
  if (!minix_is_dir(dir_inode)) {
    print("Error: '");
    print(path);
    print("' is not a directory\n");
    return -1;
  }

  // Verificar que el directorio esté vacío (solo . y ..)
  bool is_empty = true;

  for (int i = 0; i < 7; i++) {
    if (dir_inode->i_zone[i] == 0) {
      continue;
    }

    uint8_t block_buffer[MINIX_BLOCK_SIZE];
    if (minix_read_block(dir_inode->i_zone[i], block_buffer) != 0) {
      continue;
    }

    minix_dir_entry_t *entries = (minix_dir_entry_t *)block_buffer;
    int num_entries = MINIX_BLOCK_SIZE / sizeof(minix_dir_entry_t);

    for (int j = 0; j < num_entries; j++) {
      if (entries[j].inode == 0) {
        continue; // Entrada vacía
      }

      // Solo permitir . y ..
      if (strcmp(entries[j].name, ".") != 0 &&
          strcmp(entries[j].name, "..") != 0) {
        is_empty = false;
        break;
      }
    }

    if (!is_empty) {
      break;
    }
  }

  if (!is_empty) {
    print("Error: Directory '");
    print(path);
    print("' is not empty\n");
    return -1;
  }

  // Parsear el path para obtener directorio padre y nombre
  char parent_path[256];
  char dirname[64];

  if (minix_fs_split_path(path, parent_path, dirname) != 0) {
    print("Error: Invalid path\n");
    return -1;
  }

  // Obtener el inode del directorio padre
  minix_inode_t *parent_inode = minix_fs_find_inode(parent_path);
  if (!parent_inode) {
    print("Error: Parent directory not found\n");
    return -1;
  }

  // Obtener el número de inode del directorio
  uint16_t dir_inode_num = minix_fs_get_inode_number(path);
  if (dir_inode_num == 0) {
    print("Error: Could not get inode number\n");
    return -1;
  }

  // Eliminar entrada del directorio padre
  if (minix_fs_remove_dir_entry(parent_inode, dirname) != 0) {
    print("Error: Could not remove directory entry\n");
    return -1;
  }

  // Liberar el inode
  if (minix_fs_free_inode(dir_inode_num) != 0) {
    print("Error: Could not free inode\n");
    return -1;
  }

  // Actualizar el inode del padre
  uint16_t parent_inode_num = minix_fs_get_inode_number(parent_path);
  if (parent_inode_num != 0) {
    if (minix_fs_write_inode(parent_inode_num, parent_inode) != 0) {
      print("Warning: Could not update parent directory\n");
    }
  }

  print("Directory '");
  print(path);
  print("' removed successfully\n");

  return 0;
}

/**
 * Read entire file into memory - for ELF loader support
 * This function reads a complete file and allocates memory for it
 */
int minix_fs_read_file(const char *path, void **data, size_t *size) {
  if (!path || !data || !size) {
    return -1;
  }

  // Find the file inode
  uint16_t inode_num = minix_fs_get_inode_number(path);
  if (inode_num == 0) {
    return -1; // File not found
  }

  // Read the inode
  minix_inode_t inode;
  if (minix_read_inode(inode_num, &inode) != 0) {
    return -1;
  }

  // Check if it's a regular file
  if (!(inode.i_mode & MINIX_IFREG)) {
    return -1; // Not a regular file
  }

  // Allocate memory for file content
  *size = inode.i_size;
  *data = kmalloc(*size);
  if (!*data) {
    return -1; // Memory allocation failed
  }

  // Read file content
  uint8_t *buffer = (uint8_t *)*data;
  size_t bytes_read = 0;

  // Read direct zones (first 7 zones)
  for (int i = 0; i < 7 && bytes_read < *size; i++) {
    if (inode.i_zone[i] == 0) {
      continue;
    }

    uint8_t block_buffer[MINIX_BLOCK_SIZE];
    if (minix_read_block(inode.i_zone[i], block_buffer) != 0) {
      kfree(*data);
      return -1;
    }

    size_t bytes_to_copy = (*size - bytes_read > MINIX_BLOCK_SIZE)
                               ? MINIX_BLOCK_SIZE
                               : (*size - bytes_read);
    memcpy(buffer + bytes_read, block_buffer, bytes_to_copy);
    bytes_read += bytes_to_copy;
  }

  // Handle indirect zones if needed (zone[7] and zone[8])
  if (bytes_read < *size && inode.i_zone[7] != 0) {
    // Single indirect zone
    uint8_t indirect_buffer[MINIX_BLOCK_SIZE];
    if (minix_read_block(inode.i_zone[7], indirect_buffer) != 0) {
      kfree(*data);
      return -1;
    }

    uint16_t *zone_list = (uint16_t *)indirect_buffer;
    int num_zones = MINIX_BLOCK_SIZE / sizeof(uint16_t);

    for (int i = 0; i < num_zones && bytes_read < *size; i++) {
      if (zone_list[i] == 0) {
        continue;
      }

      uint8_t block_buffer[MINIX_BLOCK_SIZE];
      if (minix_read_block(zone_list[i], block_buffer) != 0) {
        kfree(*data);
        return -1;
      }

      size_t bytes_to_copy = (*size - bytes_read > MINIX_BLOCK_SIZE)
                                 ? MINIX_BLOCK_SIZE
                                 : (*size - bytes_read);
      memcpy(buffer + bytes_read, block_buffer, bytes_to_copy);
      bytes_read += bytes_to_copy;
    }
  }

  return 0;
}

int minix_fs_stat(const char *pathname, stat_t *buf) {
  if (!minix_fs.initialized || !pathname || !buf) {
    return -1;
  }

  // Find the inode for this path
  minix_inode_t *inode = minix_fs_find_inode(pathname);
  if (!inode) {
    return -1; // File not found
  }

  // Get inode number
  uint16_t inode_num = minix_fs_get_inode_number(pathname);

  // Fill stat structure with UNIX-compatible information
  buf->st_dev = 0;                 // Device ID (0 for our simple FS)
  buf->st_ino = inode_num;         // Inode number
  buf->st_nlink = inode->i_nlinks; // Number of hard links
  buf->st_uid = inode->i_uid;      // User ID
  buf->st_gid = inode->i_gid;      // Group ID
  buf->st_size = inode->i_size;    // File size in bytes
  buf->st_atime = inode->i_time;   // Access time
  buf->st_mtime = inode->i_time;   // Modification time
  buf->st_ctime = inode->i_time;   // Creation time

  // Convert MINIX mode to UNIX mode - simplified approach
  // Since MINIX and UNIX use the same permission bit layout, we can copy
  // directly
  buf->st_mode = inode->i_mode;

  return 0;
}
