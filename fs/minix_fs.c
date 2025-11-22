// SPDX-License-Identifier: GPL-3.0-only
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: minix_fs.c
 * Description: MINIX filesystem implementation with disk I/O and directory
 * operations
 */

#include "minix_fs.h"
#include <drivers/storage/ata.h>
#include <drivers/timer/clock_system.h>
#include <ir0/vga.h>
#include <ir0/stat.h>
#include <drivers/video/typewriter.h>
#include <ir0/serial.h>
#include <ir0/memory/kmem.h>
#include <string.h>

extern int cursor_pos;

// Error code definitions
#define ENOENT 2   // No such file or directory
#define EIO 5      // I/O error
#define ENOTDIR 20 // Not a directory

#define MINIX_SUPER_MAGIC 0x137F
#define MINIX_MAGIC 0x137F

#define MINIX_ROOT_INODE 1
#define MINIX_MAX_INODES 1024
#define MINIX_MAX_ZONES 1024
#define MINIX_ZONE_SIZE 1024

typedef struct minix_fs_info
{
  minix_superblock_t superblock;
  uint8_t *inode_bitmap;
  uint8_t *zone_bitmap;
  minix_inode_t *inode_table;
  uint8_t *zone_table;
  bool initialized;
} minix_fs_info_t;

static minix_fs_info_t minix_fs;

int minix_read_block(uint32_t block_num, void *buffer)
{
  uint32_t lba =
      block_num * 2; // 2 sectores de 512 bytes = 1 bloque de 1024 bytes
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
  uint32_t lba =
      block_num * 2; // 2 sectores de 512 bytes = 1 bloque de 1024 bytes
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

[[maybe_unused]] static bool minix_is_inode_free(uint32_t inode_num)
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
  if (zone_num < minix_fs.superblock.s_firstdatazone ||
      zone_num >= MINIX_MAX_ZONES)
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
  // Zone bitmap starts at block 2 + s_imap_blocks
  uint32_t zmap_start_block = 2 + minix_fs.superblock.s_imap_blocks;
  uint32_t block_num = zmap_start_block + byte_index / MINIX_BLOCK_SIZE;
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
  if (zone_num < minix_fs.superblock.s_firstdatazone ||
      zone_num >= MINIX_MAX_ZONES)
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
  // Zone bitmap starts at block 2 + s_imap_blocks
  uint32_t zmap_start_block = 2 + minix_fs.superblock.s_imap_blocks;
  uint32_t block_num = zmap_start_block + byte_index / MINIX_BLOCK_SIZE;
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
  for (uint32_t i = minix_fs.superblock.s_firstdatazone; i < MINIX_MAX_ZONES;
       i++)
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
  if (zone_num < minix_fs.superblock.s_firstdatazone ||
      zone_num >= MINIX_MAX_ZONES)
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
  // Zone bitmap starts at block 2 + s_imap_blocks
  uint32_t zmap_start_block = 2 + minix_fs.superblock.s_imap_blocks;
  uint32_t block_num = zmap_start_block + byte_index / MINIX_BLOCK_SIZE;
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

static int minix_read_inode(uint32_t inode_num, minix_inode_t *inode)
{
  if (inode_num == 0 || inode_num >= MINIX_MAX_INODES || !inode)
  {
    return -1;
  }

  // Calcular posición del inode en el disco
  // Para MINIX: inode table starts after superblock + bitmaps
  // Block layout: 0=boot, 1=super, 2=imap, 3=zmap, 4=inodes, 5+=data
  uint32_t inode_table_start =
      2 + minix_fs.superblock.s_imap_blocks + minix_fs.superblock.s_zmap_blocks;
  uint32_t inode_block =
      inode_table_start +
      ((inode_num - 1) * sizeof(minix_inode_t)) / MINIX_BLOCK_SIZE;
  uint32_t inode_offset =
      ((inode_num - 1) * sizeof(minix_inode_t)) % MINIX_BLOCK_SIZE;

  uint8_t block_buffer[MINIX_BLOCK_SIZE];
  int result = minix_read_block(inode_block, block_buffer);
  if (result != 0)
  {
    return -1;
  }

  // Copiar inode del buffer
  kmemcpy(inode, block_buffer + inode_offset, sizeof(minix_inode_t));

  return 0;
}

static int __attribute__((unused))
minix_write_inode(uint32_t inode_num, const minix_inode_t *inode)
{
  if (inode_num == 0 || inode_num >= MINIX_MAX_INODES || !inode)
  {
    return -1;
  }

  // Calcular posición del inode en el disco
  // Block layout: 0=boot, 1=super, 2=imap, 3=zmap, 4=inodes, 5+=data
  uint32_t inode_table_start = 2 + minix_fs.superblock.s_imap_blocks + minix_fs.superblock.s_zmap_blocks;
  uint32_t inode_block = inode_table_start +
                         (inode_num * sizeof(minix_inode_t)) / MINIX_BLOCK_SIZE;
  uint32_t inode_offset =
      (inode_num * sizeof(minix_inode_t)) % MINIX_BLOCK_SIZE;

  uint8_t block_buffer[MINIX_BLOCK_SIZE];
  int result = minix_read_block(inode_block, block_buffer);
  if (result != 0)
  {
    return -1;
  }

  // Copiar inode al buffer
  kmemcpy(block_buffer + inode_offset, inode, sizeof(minix_inode_t));

  // Escribir bloque de vuelta al disco
  result = minix_write_block(inode_block, block_buffer);

  return result;
}

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

// Helper function to format file permissions in rwxr-xr-x format
[[maybe_unused]] static void format_permissions(uint16_t mode, char *buffer)
{
  // File type
  buffer[0] = (mode & MINIX_IFDIR) ? 'd' : '-';

  // User permissions
  buffer[1] = (mode & MINIX_IRUSR) ? 'r' : '-';
  buffer[2] = (mode & MINIX_IWUSR) ? 'w' : '-';
  buffer[3] = (mode & MINIX_IXUSR) ? 'x' : '-';

  // Group permissions
  buffer[4] = (mode & MINIX_IRGRP) ? 'r' : '-';
  buffer[5] = (mode & MINIX_IWGRP) ? 'w' : '-';
  buffer[6] = (mode & MINIX_IXGRP) ? 'x' : '-';

  // Other permissions
  buffer[7] = (mode & MINIX_IROTH) ? 'r' : '-';
  buffer[8] = (mode & MINIX_IWOTH) ? 'w' : '-';
  buffer[9] = (mode & MINIX_IXOTH) ? 'x' : '-';

  buffer[10] = ' ';  // Space after permissions
  buffer[11] = '\0'; // Null terminator
}

// Global static inodes to avoid memory issues
static minix_inode_t cached_root_inode;
static minix_inode_t cached_result_inode;
static bool root_inode_cached = false;

minix_inode_t *minix_fs_find_inode(const char *pathname)
{
  // Using typewriter_print for kernel output
  static minix_inode_t result_inode;

  if (!pathname || !minix_fs.initialized)
  {
    return NULL;
  }
  if (kstrcmp(pathname, "/") == 0)
  {
    // Always read from disk to ensure we have the latest version
    if (minix_read_inode(MINIX_ROOT_INODE, &cached_root_inode) == 0)
    {
      root_inode_cached = true;
      kmemcpy(&result_inode, &cached_root_inode, sizeof(minix_inode_t));
      return &result_inode;
    }
    return NULL;
  }

  // Parsear el path
  char path_copy[256];
  kstrncpy(path_copy, pathname, sizeof(path_copy) - 1);
  path_copy[sizeof(path_copy) - 1] = '\0';

  // Empezar desde el inode raíz
  minix_inode_t current_inode;
  if (minix_read_inode(MINIX_ROOT_INODE, &current_inode) != 0)
  {
    return NULL;
  }

  // Dividir el path en componentes
  // Simple tokenizer to replace strtok
  char *token = path_copy;
  if (*token == '/')
    token++; // Skip leading slash
  
  // Handle trailing slash or empty path after leading slash
  if (*token == '\0')
  {
     kmemcpy(&cached_result_inode, &current_inode, sizeof(minix_inode_t));
     return &cached_result_inode;
  }

  char *next = token;
  while (*next && *next != '/')
    next++;
  
  if (*next == '/')
    *next++ = '\0';
  else
    next = NULL; // End of string

  while (token != NULL && *token != '\0')
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

    // Get next token
    if (next)
    {
      token = next;
      while (*next && *next != '/')
        next++;
      
      if (*next == '/')
        *next++ = '\0';
      else
        next = NULL; // End of string
    }
    else
    {
      token = NULL;
    }
  }

  // Retornar una copia estática del inode encontrado
  kmemcpy(&cached_result_inode, &current_inode, sizeof(minix_inode_t));

  return &cached_result_inode;
}

// Función auxiliar para obtener el número de inode de un path
static uint16_t minix_fs_get_inode_number(const char *pathname)
{
  if (!pathname || !minix_fs.initialized)
  {
    return 0;
  }

  // Si es el directorio raíz
  if (kstrcmp(pathname, "/") == 0)
  {
    return MINIX_ROOT_INODE;
  }

  // Parsear el path
  char path_copy[256];
  kstrncpy(path_copy, pathname, sizeof(path_copy) - 1);
  path_copy[sizeof(path_copy) - 1] = '\0';

  // Empezar desde el inode raíz
  minix_inode_t current_inode;
  if (minix_read_inode(MINIX_ROOT_INODE, &current_inode) != 0)
  {
    return 0;
  }

  // Dividir el path en componentes
  // Simple tokenizer to replace strtok
  char *token = path_copy;
  if (*token == '/')
    token++; // Skip leading slash
  
  // Handle trailing slash or empty path after leading slash
  if (*token == '\0')
  {
     return MINIX_ROOT_INODE;
  }

  char *next = token;
  while (*next && *next != '/')
    next++;
  
  if (*next == '/')
    *next++ = '\0';
  else
    next = NULL; // End of string

  uint16_t current_inode_num = MINIX_ROOT_INODE;

  while (token != NULL && *token != '\0')
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

    // Get next token
    if (next)
    {
      token = next;
      while (*next && *next != '/')
        next++;
      
      if (*next == '/')
        *next++ = '\0';
      else
        next = NULL; // End of string
    }
    else
    {
      token = NULL;
    }
  }

  return current_inode_num;
}

uint16_t minix_fs_find_dir_entry(const minix_inode_t *dir_inode,
                                 const char *name)
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

      if (kstrcmp(entries[j].name, name) == 0)
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

  // Calcular la posición del inode en el disco (DEBE SER IGUAL A minix_read_inode)
  // Block layout: 0=boot, 1=super, 2=imap, 3=zmap, 4=inodes, 5+=data
  uint32_t inode_table_start =
      1 + minix_fs.superblock.s_imap_blocks + minix_fs.superblock.s_zmap_blocks;
  uint32_t inode_block =
      inode_table_start +
      ((inode_num - 1) * sizeof(minix_inode_t)) / MINIX_BLOCK_SIZE;
  uint32_t inode_offset =
      ((inode_num - 1) * sizeof(minix_inode_t)) % MINIX_BLOCK_SIZE;


  serial_print("SERIAL: write_inode: inode_num=");
  serial_print_hex32(inode_num);
  serial_print(" block=");
  serial_print_hex32(inode_block);
  serial_print(" offset=");
  serial_print_hex32(inode_offset);
  serial_print("\n");

  // Leer el bloque que contiene el inode
  uint8_t block_buffer[MINIX_BLOCK_SIZE];
  if (minix_read_block(inode_block, block_buffer) != 0)
  {
    serial_print("SERIAL: write_inode: failed to read block\n");
    return -1;
  }

  // Copiar el inode al buffer
  kmemcpy(block_buffer + inode_offset, inode, sizeof(minix_inode_t));

  // Escribir el bloque actualizado
  if (minix_write_block(inode_block, block_buffer) != 0)
  {
    serial_print("SERIAL: write_inode: failed to write block\n");
    return -1;
  }

  // Invalidate root cache if we wrote the root inode
  if (inode_num == MINIX_ROOT_INODE)
  {
    root_inode_cached = false;
  }

  // Invalidate root cache if we wrote the root inode
  if (inode_num == MINIX_ROOT_INODE)
  {
    root_inode_cached = false;
  }

  serial_print("SERIAL: write_inode: success\n");
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

int minix_fs_split_path(const char *pathname, char *parent_path,
                        char *filename)
{
  if (!pathname || !parent_path || !filename)
  {
    return -1;
  }

  // Encontrar la última barra
  // Find last slash
  const char *last_slash = NULL;
  const char *p = pathname;
  while (*p)
  {
    if (*p == '/')
      last_slash = p;
    p++;
  }
  if (!last_slash)
  {
    // No hay barra, el archivo está en el directorio actual
    kstrncpy(parent_path, ".", 2);
    kstrncpy(filename, pathname, MINIX_NAME_LEN);
    return 0;
  }

  if (last_slash == pathname)
  {
    // Es el directorio raíz
    kstrncpy(parent_path, "/", 2);
  }
  else
  {
    // Copiar la parte del directorio padre
    size_t parent_len = last_slash - pathname;
    kstrncpy(parent_path, pathname, parent_len);
    parent_path[parent_len] = '\0';
  }

  // Copiar el nombre del archivo
  kstrncpy(filename, last_slash + 1, MINIX_NAME_LEN);

  return 0;
}

/**
 * Add a directory entry to the parent directory
 * @param parent_inode Parent directory inode (will be modified)
 * @param filename Name of the new entry
 * @param inode_num Inode number of the new entry
 * @return 0 on success, -1 on failure
 */
int minix_fs_add_dir_entry(minix_inode_t *parent_inode, const char *filename,
                           uint16_t inode_num)
{
  if (!parent_inode || !filename || inode_num == 0 || inode_num >= 65535)
  {
    return -1; // Invalid parameters
  }

  // Check if filename is too long or empty
  size_t name_len = kstrlen(filename);
  if (name_len == 0 || name_len >= MINIX_NAME_LEN)
  {
    return -1; // Invalid filename length
  }

  // Try to find a free entry in existing zones
  for (int zone_index = 0; zone_index < 7; zone_index++)
  {
    if (parent_inode->i_zone[zone_index] == 0)
    {
      break; // No more allocated zones
    }

    uint32_t zone = parent_inode->i_zone[zone_index];
    uint8_t block_buffer[MINIX_BLOCK_SIZE] = {0};

    // Read the zone
    if (minix_read_block(zone, block_buffer) != 0)
    {
      continue; // Skip bad blocks
    }

    minix_dir_entry_t *entries = (minix_dir_entry_t *)block_buffer;
    int num_entries = MINIX_BLOCK_SIZE / sizeof(minix_dir_entry_t);

    // Look for a free entry or check for duplicate filename
    for (int i = 0; i < num_entries; i++)
    {
      // Check for duplicate filename
      if (entries[i].inode != 0 && kstrncmp(entries[i].name, filename, MINIX_NAME_LEN) == 0)
      {
        return -1; // Entry with this name already exists
      }

      // Found a free entry
      if (entries[i].inode == 0)
      {
        // Initialize the entry
        entries[i].inode = inode_num;
        size_t copy_len = name_len < (MINIX_NAME_LEN - 1) ? name_len : (MINIX_NAME_LEN - 1);
        kmemcpy(entries[i].name, filename, copy_len);
        entries[i].name[copy_len] = '\0';
        if (copy_len < MINIX_NAME_LEN - 1)
          kmemset(entries[i].name + copy_len + 1, 0, MINIX_NAME_LEN - copy_len - 1);

        // Write the updated block back
        if (minix_write_block(zone, block_buffer) != 0)
        {
          return -1; // Failed to write block
        }

        // Update directory size if needed
        size_t entry_offset = (zone_index * MINIX_BLOCK_SIZE) + (i * sizeof(minix_dir_entry_t));
        if (entry_offset + sizeof(minix_dir_entry_t) > parent_inode->i_size)
        {
          parent_inode->i_size = entry_offset + sizeof(minix_dir_entry_t);
        }

        return 0; // Success
      }
    }
  }

  // No space in existing zones, allocate a new one
  for (int zone_index = 0; zone_index < 7; zone_index++)
  {
    if (parent_inode->i_zone[zone_index] == 0)
    {
      // Allocate a new zone
      uint32_t new_zone = minix_alloc_zone();
      if (new_zone == 0)
      {
        return -1; // No free zones
      }

      // Initialize the new zone with zeros
      uint8_t block_buffer[MINIX_BLOCK_SIZE] = {0};
      minix_dir_entry_t *entries = (minix_dir_entry_t *)block_buffer;

      // Add the new entry as the first one
      entries[0].inode = inode_num;
      size_t copy_len = name_len < (MINIX_NAME_LEN - 1) ? name_len : (MINIX_NAME_LEN - 1);
      kmemcpy(entries[0].name, filename, copy_len);
      entries[0].name[copy_len] = '\0';
      if (copy_len < MINIX_NAME_LEN - 1)
        kmemset(entries[0].name + copy_len + 1, 0, MINIX_NAME_LEN - copy_len - 1);

      // Write the new zone
      if (minix_write_block(new_zone, block_buffer) != 0)
      {
        minix_free_zone(new_zone); // Clean up
        return -1;
      }

      // Update parent inode
      parent_inode->i_zone[zone_index] = new_zone;
      parent_inode->i_size = (zone_index + 1) * MINIX_BLOCK_SIZE;

      return 0; // Success
    }
  }

  return 0;
}

/**
 * Remove a directory entry from a directory
 * @param parent_inode Parent directory inode (will be modified)
 * @param filename Name of the entry to remove
 * @return 0 on success, -1 on failure
 */
int minix_fs_remove_dir_entry(minix_inode_t *parent_inode, const char *filename)
{
  if (!parent_inode || !filename || !*filename)
  {
    return -1; // Invalid parameters
  }

  // Search for the entry in all directory zones
  for (int zone_idx = 0; zone_idx < 7; zone_idx++)
  {
    uint32_t zone = parent_inode->i_zone[zone_idx];
    if (zone == 0)
    {
      continue; // No more allocated zones
    }

    uint8_t block_buffer[MINIX_BLOCK_SIZE] = {0};
    if (minix_read_block(zone, block_buffer) != 0)
    {
      continue; // Skip bad blocks
    }

    minix_dir_entry_t *entries = (minix_dir_entry_t *)block_buffer;
    int num_entries = MINIX_BLOCK_SIZE / sizeof(minix_dir_entry_t);
    bool entry_found = false;

    // First pass: find the entry
    for (int i = 0; i < num_entries; i++)
    {
      if (entries[i].inode == 0)
      {
        continue; // Skip empty entries
      }

      if (kstrncmp(entries[i].name, filename, MINIX_NAME_LEN) == 0)
      {
        // Found the entry, mark it as free
        entries[i].inode = 0;
        kmemset(entries[i].name, 0, MINIX_NAME_LEN);
        entry_found = true;
        break;
      }
    }

    if (entry_found)
    {
      // Write the updated block back
      if (minix_write_block(zone, block_buffer) != 0)
      {
        return -1; // Failed to write block
      }

      // Update directory size if needed
      size_t entry_size = sizeof(minix_dir_entry_t);
      size_t current_size = parent_inode->i_size;
      if (current_size > 0)
      {
        // Only shrink the directory if this was the last entry
        bool is_last_entry = true;
        for (int i = 0; i < num_entries; i++)
        {
          if (entries[i].inode != 0)
          {
            is_last_entry = false;
            break;
          }
        }

        if (is_last_entry)
        {
          // This was the last entry in this block
          if (parent_inode->i_size >= entry_size)
          {
            parent_inode->i_size -= entry_size;
          }
          else
          {
            parent_inode->i_size = 0;
          }
        }
      }

      return 0; // Success
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

bool minix_fs_is_working(void) { return minix_fs.initialized; }

int minix_fs_init(void)
{
  // FORCE REAL DISK USAGE - In QEMU, disk is always available

  // Read superblock from disk
  if (minix_read_block(1, &minix_fs.superblock) != 0)
  {
    // Can't read - format disk
    return minix_fs_format();
  }

  // Check magic number
  if (minix_fs.superblock.s_magic != MINIX_SUPER_MAGIC)
  {
    // Invalid - format disk
    return minix_fs_format();
  }

  // Read bitmaps
  // Block 2 is always the first inode bitmap block
  uint32_t imap_block = 2;
  uint32_t zmap_block = 2 + minix_fs.superblock.s_imap_blocks;

  if (minix_read_block(imap_block, minix_fs.inode_bitmap) != 0)
  {
    return minix_fs_format();
  }

  if (minix_read_block(zmap_block, minix_fs.zone_bitmap) != 0)
  {
    return minix_fs_format();
  }

  // Valid filesystem found
  minix_fs.initialized = true;
  root_inode_cached = false; // Reset cache
  return 0;
}

int minix_fs_format(void)
{
  // REAL MINIX FILESYSTEM CREATION

  // Initialize superblock
  kmemset(&minix_fs.superblock, 0, sizeof(minix_superblock_t));
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
  if (minix_write_block(1, &minix_fs.superblock) != 0)
  {
    return -1;
  }

  // Inicializar bitmaps
  kmemset(minix_fs.inode_bitmap, 0, MINIX_BLOCK_SIZE);
  kmemset(minix_fs.zone_bitmap, 0, MINIX_BLOCK_SIZE);

  // Marcar inode 1 como usado (root directory)
  minix_fs.inode_bitmap[0] = 0x01;

  // Block offsets
  uint32_t imap_block = 2;
  uint32_t zmap_block = 2 + minix_fs.superblock.s_imap_blocks;
  uint32_t inode_table_block = zmap_block + minix_fs.superblock.s_zmap_blocks;

  // Escribir bitmaps
  if (minix_write_block(imap_block, minix_fs.inode_bitmap) != 0)
  {
    return -1;
  }

  if (minix_write_block(zmap_block, minix_fs.zone_bitmap) != 0)
  {
    return -1;
  }

  // Crear inode raíz
  minix_inode_t root_inode;
  kmemset(&root_inode, 0, sizeof(minix_inode_t));
  root_inode.i_mode = MINIX_IFDIR | MINIX_IRWXU | MINIX_IRGRP | MINIX_IROTH;
  root_inode.i_uid = 0;
  root_inode.i_size = 2 * sizeof(minix_dir_entry_t); // . and .. entries
  root_inode.i_time = 0;
  root_inode.i_gid = 0;
  root_inode.i_nlinks = 2;                                    // . and .. links
  root_inode.i_zone[0] = minix_fs.superblock.s_firstdatazone; // First data zone

  // Escribir inode raíz (inode 1)
  uint8_t inode_block[MINIX_BLOCK_SIZE];
  kmemset(inode_block, 0, MINIX_BLOCK_SIZE);
  kmemcpy(inode_block, &root_inode, sizeof(minix_inode_t));

  // Write to the first block of inode table
  if (minix_write_block(inode_table_block, inode_block) != 0) 
  {
    return -1;
  }

  // Crear directorio root con entradas . y ..
  uint8_t root_dir_block[MINIX_BLOCK_SIZE];
  kmemset(root_dir_block, 0, MINIX_BLOCK_SIZE);

  minix_dir_entry_t *entries = (minix_dir_entry_t *)root_dir_block;

  // Entrada "."
  entries[0].inode = 1; // Root inode
  kstrncpy(entries[0].name, ".", MINIX_NAME_LEN);

  // Entrada ".."
  entries[1].inode = 1; // Root inode (parent of root is root)
  kstrncpy(entries[1].name, "..", MINIX_NAME_LEN);

  // Escribir directorio root
  if (minix_write_block(minix_fs.superblock.s_firstdatazone, root_dir_block) !=
      0)
  {
    return -1;
  }

  // Solo escribir el inode del directorio root (inode 1)
  if (minix_write_block(inode_table_block, inode_block) != 0)
  {
    return -1;
  }

  // Marcar solo el inode 1 como usado en bitmap
  minix_fs.inode_bitmap[0] |= 0x01; // Bit 0 = inode 1

  // Escribir bitmap de inodes
  if (minix_write_block(imap_block, minix_fs.inode_bitmap) != 0)
  {
    return -1;
  }

  minix_fs.initialized = true;
  root_inode_cached = false; // Reset cache
  return 0;
}

int minix_fs_mkdir(const char *path, mode_t mode)
{
  if (!minix_fs.initialized)
  {
    return -1;
  }

  if (!path || kstrlen(path) == 0)
  {
    return -1;
  }

  if (kstrcmp(path, "/") == 0)
  {
    typewriter_vga_print("Error: Cannot create directory with root directory name\n", 0x0C);
    return -1;
  }

  // Verificar que el disco esté disponible
  if (!ata_is_available())
  {
    return -EIO; // Error real - no hay disco disponible
  }

  // Parsear el path para obtener directorio padre y nombre
  char parent_path[256];
  char dirname[64];

  serial_print("SERIAL: minix_fs_mkdir: splitting path: ");
  serial_print(path);
  serial_print("\n");

  if (minix_fs_split_path(path, parent_path, dirname) != 0)
  {
    serial_print("SERIAL: minix_fs_mkdir: split_path failed\n");
    return -1;
  }

  serial_print("SERIAL: minix_fs_mkdir: parent=");
  serial_print(parent_path);
  serial_print(" dirname=");
  serial_print(dirname);
  serial_print("\n");

  // Obtener el inode del directorio padre (hacemos una copia local)
  minix_inode_t parent_inode;
  minix_inode_t *parent_inode_ptr = minix_fs_find_inode(parent_path);
  if (!parent_inode_ptr)
  {
    serial_print("SERIAL: minix_fs_mkdir: parent inode not found\n");
    return -1;
  }
  kmemcpy(&parent_inode, parent_inode_ptr, sizeof(minix_inode_t));

  if (!(parent_inode.i_mode & MINIX_IFDIR))
  {
    serial_print("SERIAL: minix_fs_mkdir: parent is not a directory\n");
    return -1;
  }

  // Verificar si el directorio ya existe
  uint16_t existing_inode = minix_fs_find_dir_entry(&parent_inode, dirname);
  if (existing_inode != 0)
  {
    serial_print("SERIAL: minix_fs_mkdir: directory already exists\n");
    return -1;
  }

  // Asignar un nuevo inode
  uint16_t new_inode_num = minix_alloc_inode();
  if (new_inode_num == 0)
  {
    serial_print("SERIAL: minix_fs_mkdir: failed to allocate inode\n");
    return -1;
  }
  serial_print("SERIAL: minix_fs_mkdir: allocated inode ");
  serial_print_hex32(new_inode_num);
  serial_print("\n");

  // Get parent inode number before making changes
  uint16_t parent_inode_num = minix_fs_get_inode_number(parent_path);
  if (parent_inode_num == 0)
  {
    serial_print("SERIAL: minix_fs_mkdir: failed to get parent inode number\n");
    return -1;
  }
  serial_print("SERIAL: minix_fs_mkdir: parent inode num ");
  serial_print_hex32(parent_inode_num);
  serial_print("\n");

  // Create new directory inode
  minix_inode_t new_inode;
  kmemset(&new_inode, 0, sizeof(minix_inode_t));
  new_inode.i_mode = MINIX_IFDIR | (mode & 0777);
  new_inode.i_uid = 0;                 // root
  new_inode.i_gid = 0;                 // root
  new_inode.i_size = MINIX_BLOCK_SIZE; // One block for . and ..
  new_inode.i_time = get_system_time();
  new_inode.i_nlinks = 2; // . and ..

  // Allocate and initialize directory block
  uint32_t zone = minix_alloc_zone();
  if (zone == 0)
  {
    serial_print("SERIAL: minix_fs_mkdir: failed to allocate zone\n");
    return -1;
  }
  new_inode.i_zone[0] = zone;

  // Initialize directory block with . and ..
  uint8_t block_buffer[MINIX_BLOCK_SIZE] = {0};
  minix_dir_entry_t *entries = (minix_dir_entry_t *)block_buffer;

  // Add . entry (self)
  entries[0].inode = new_inode_num;
  kstrncpy(entries[0].name, ".", MINIX_NAME_LEN);

  // Add .. entry (parent)
  entries[1].inode = parent_inode_num;
  kstrncpy(entries[1].name, "..", MINIX_NAME_LEN);

  // Clear remaining entries
  size_t max_entries = MINIX_BLOCK_SIZE / sizeof(minix_dir_entry_t);
  for (size_t i = 2; i < max_entries; i++)
  {
    entries[i].inode = 0;
    kmemset(entries[i].name, 0, MINIX_NAME_LEN);
  }

  // Write the directory block
  if (minix_write_block(zone, block_buffer) != 0)
  {
    serial_print("SERIAL: minix_fs_mkdir: failed to write block\n");
    minix_free_zone(zone);
    return -1;
  }

  // Write the directory block first
  if (minix_write_block(zone, block_buffer) != 0)
  {
    minix_free_zone(zone);
    return -1;
  }

  // Write the new inode
  if (minix_fs_write_inode(new_inode_num, &new_inode) != 0)
  {
    serial_print("SERIAL: minix_fs_mkdir: failed to write new inode\n");
    minix_free_zone(zone);
    minix_fs_free_inode(new_inode_num);
    return -1;
  }

  // Add entry to parent directory
  if (minix_fs_add_dir_entry(&parent_inode, dirname, new_inode_num) != 0)
  {
    serial_print("SERIAL: minix_fs_mkdir: failed to add dir entry\n");
    minix_free_zone(zone);
    minix_fs_free_inode(new_inode_num);
    return -1;
  }

  // Update parent directory's link count and mtime
  parent_inode.i_nlinks++; // Increment link count for the new directory
  parent_inode.i_time = get_system_time();

  // Write back the updated parent inode
  if (minix_fs_write_inode(parent_inode_num, &parent_inode) != 0)
  {
    // If we can't update the parent, we're in an inconsistent state
    // This is bad - we've already created the directory
    return -1;
  }

  return 0;
}

static void uint32_to_str(uint32_t num, char *buf, size_t buf_size)
{
  if (buf_size == 0)
    return;

  if (num == 0)
  {
    buf[0] = '0';
    buf[1] = '\0';
    return;
  }

  char tmp[16];
  int pos = 0;
  while (num > 0 && pos < 15)
  {
    tmp[pos++] = '0' + (num % 10);
    num /= 10;
  }

  int i = 0;
  for (int j = pos - 1; j >= 0 && i < (int)buf_size - 1; j--, i++)
  {
    buf[i] = tmp[j];
  }
  buf[i] = '\0';
}

int minix_fs_ls(const char *path, bool detailed)
{
  extern void serial_print(const char *str);
  extern void serial_print_hex32(uint32_t num);

  serial_print("SERIAL: minix_fs_ls called for path: ");
  serial_print(path ? path : "NULL");
  serial_print("\n");

  if (!minix_fs.initialized)
  {
    typewriter_vga_print("ls: filesystem not initialized\n", 0x0C);
    return -1;
  }

  if (!path)
  {
    typewriter_vga_print("ls: invalid path\n", 0x0C);
    typewriter_vga_print("ls: filesystem not initialized\n", 0x0C);
    return -1;
  }

  if (!path)
  {
    typewriter_vga_print("ls: invalid path\n", 0x0C);
    return -1;
  }

  minix_inode_t *dir_inode = minix_fs_find_inode(path);
  if (!dir_inode)
  {
    char error_msg[256];
    snprintf(error_msg, sizeof(error_msg), "ls: cannot access '%s': No such file or directory\n", path);
    typewriter_vga_print(error_msg, 0x0C);
    return -ENOENT;
  }

  if (!(dir_inode->i_mode & MINIX_IFDIR))
  {
    char error_msg[256];
    snprintf(error_msg, sizeof(error_msg), "ls: cannot access '%s': Not a directory\n", path);
    typewriter_vga_print(error_msg, 0x0C);
    return -1;
  }

  minix_inode_t dir_inode_copy;
  kmemcpy(&dir_inode_copy, dir_inode, sizeof(minix_inode_t));

  if (detailed)
  {
    typewriter_vga_print("Permissions Links Size Owner Group Name\n", 0x0F);
  }
  if (detailed)
  {
    typewriter_vga_print("Permissions Links Size Owner Group Name\n", 0x0F);
  }

  for (int i = 0; i < 7; i++)
  for (int i = 0; i < 7; i++)
  {
    uint32_t zone = dir_inode_copy.i_zone[i];
    if (zone == 0)
      continue;

    uint8_t block_buffer[MINIX_BLOCK_SIZE];
    if (minix_read_block(zone, block_buffer) != 0)
    {
      typewriter_vga_print("ls: error reading directory block\n", 0x0C);
      typewriter_vga_print("ls: error reading directory block\n", 0x0C);
      continue;
    }

    minix_dir_entry_t *entries = (minix_dir_entry_t *)block_buffer;
    int num_entries = MINIX_BLOCK_SIZE / sizeof(minix_dir_entry_t);

    for (int j = 0; j < num_entries; j++)
    {
      if (entries[j].inode == 0)
        continue;

      // Safe name copy to ensure null termination
      char safe_name[MINIX_NAME_LEN + 1];
      // Copy manually to handle non-null terminated source
      int k;
      for (k = 0; k < MINIX_NAME_LEN; k++) {
          safe_name[k] = entries[j].name[k];
          if (safe_name[k] == '\0') break;
      }
      safe_name[k] = '\0';

      if (kstrcmp(safe_name, ".") == 0)
        continue;
      if (kstrcmp(safe_name, "..") == 0)
        continue;

      if (safe_name[0] == '\0')
        continue;

      if (detailed)
      {
        minix_inode_t entry_inode;
        if (minix_read_inode(entries[j].inode, &entry_inode) != 0)
        {
          typewriter_vga_print("ls: error reading inode\n", 0x0C);
          continue;
        }

        char perm_str[12];
        perm_str[0] = (entry_inode.i_mode & MINIX_IFDIR) ? 'd' : '-';
        perm_str[1] = (entry_inode.i_mode & MINIX_IRUSR) ? 'r' : '-';
        perm_str[2] = (entry_inode.i_mode & MINIX_IWUSR) ? 'w' : '-';
        perm_str[3] = (entry_inode.i_mode & MINIX_IXUSR) ? 'x' : '-';
        perm_str[4] = (entry_inode.i_mode & MINIX_IRGRP) ? 'r' : '-';
        perm_str[5] = (entry_inode.i_mode & MINIX_IWGRP) ? 'w' : '-';
        perm_str[6] = (entry_inode.i_mode & MINIX_IXGRP) ? 'x' : '-';
        perm_str[7] = (entry_inode.i_mode & MINIX_IROTH) ? 'r' : '-';
        perm_str[8] = (entry_inode.i_mode & MINIX_IWOTH) ? 'w' : '-';
        perm_str[9] = (entry_inode.i_mode & MINIX_IXOTH) ? 'x' : '-';
        perm_str[10] = ' ';
        perm_str[11] = '\0';

        char nlinks_str[16];
        uint32_to_str(entry_inode.i_nlinks, nlinks_str, sizeof(nlinks_str));

        char size_str[16];
        uint32_to_str(entry_inode.i_size, size_str, sizeof(size_str));

        char line[512];
        snprintf(line, sizeof(line), "%s %s %s root root %s\n",
                perm_str, nlinks_str, size_str, safe_name);
        typewriter_vga_print(line, 0x0F);
      }
      else
      {
        char line[256];
        int len = snprintf(line, sizeof(line), "%s\n", safe_name);
        
        serial_print("SERIAL: minix_fs_ls: printing line: '");
        serial_print(line);
        serial_print("' len=");
        serial_print_hex32(len);
        serial_print("\n");
        
        typewriter_vga_print(line, 0x0F); 
      }
    }
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

int minix_fs_cat(const char *path)
{
  if (!minix_fs.initialized)
  {
    typewriter_vga_print("cat: filesystem not initialized\n", 0x0C);
    typewriter_vga_print("cat: filesystem not initialized\n", 0x0C);
    return -1;
  }

  if (!path)
  {
    typewriter_vga_print("cat: invalid path\n", 0x0C);
    typewriter_vga_print("cat: invalid path\n", 0x0C);
    return -1;
  }

  if (!ata_is_available())
  {
    typewriter_vga_print("cat: disk not available\n", 0x0C);
    return -EIO;
    typewriter_vga_print("cat: disk not available\n", 0x0C);
    return -EIO;
  }

  uint16_t inode_num = minix_fs_get_inode_number(path);
  if (inode_num == 0)
  {
    char error_msg[256];
    snprintf(error_msg, sizeof(error_msg), "cat: '%s': No such file\n", path);
    typewriter_vga_print(error_msg, 0x0C);
    return -1;
  }

  minix_inode_t file_inode_data;
  if (minix_read_inode(inode_num, &file_inode_data) != 0)
  {
    typewriter_vga_print("cat: Error reading inode\n", 0x0C);
    return -1;
  }

  if (file_inode_data.i_mode & MINIX_IFDIR)
  {
    char error_msg[256];
    snprintf(error_msg, sizeof(error_msg), "cat: '%s': Is a directory\n", path);
    typewriter_vga_print(error_msg, 0x0C);
    return -1;
  }

  uint32_t file_size = file_inode_data.i_size;
  uint32_t bytes_read = 0;
  char output_buffer[MINIX_BLOCK_SIZE + 1];

  for (int i = 0; i < 7 && bytes_read < file_size; i++)
  {
    if (file_inode_data.i_zone[i] == 0)
    {
      continue;
    }

    uint8_t block_buffer[MINIX_BLOCK_SIZE];
    if (minix_read_block(file_inode_data.i_zone[i], block_buffer) != 0)
    {
      typewriter_vga_print("cat: Error reading block\n", 0x0C);
      continue;
    }

    uint32_t bytes_to_show = MINIX_BLOCK_SIZE;
    if (bytes_read + bytes_to_show > file_size)
    {
      bytes_to_show = file_size - bytes_read;
    }

    uint32_t output_pos = 0;
    for (uint32_t j = 0; j < bytes_to_show && output_pos < MINIX_BLOCK_SIZE; j++)
    {
      char c = block_buffer[j];
      if (c == '\0')
        break;

      if (c >= 32 && c < 127)
      {
        output_buffer[output_pos++] = c;
      }
      else if (c == '\n')
      {
        output_buffer[output_pos++] = '\n';
      }
      else if (c == '\t')
      {
        if (output_pos + 4 < MINIX_BLOCK_SIZE)
        {
          output_buffer[output_pos++] = ' ';
          output_buffer[output_pos++] = ' ';
          output_buffer[output_pos++] = ' ';
          output_buffer[output_pos++] = ' ';
        }
      }
    }

    if (output_pos > 0)
    {
      output_buffer[output_pos] = '\0';
      typewriter_vga_print(output_buffer, 0x0F);
    }

    bytes_read += bytes_to_show;
  }

  return 0;
}

// ===============================================================================
// FUNCIÓN PARA ESCRIBIR CONTENIDO A ARCHIVOS
// ===============================================================================

int minix_fs_write_file(const char *path, const char *content)
{
  extern void serial_print(const char *str);
  extern void serial_print_hex32(uint32_t num);

  serial_print("SERIAL: minix_fs_write_file called\n");

  if (!minix_fs.initialized)
  {
    serial_print("SERIAL: minix_fs_write_file: filesystem not initialized\n");
    return -1;
  }

  if (!path)
  {
    serial_print("SERIAL: minix_fs_write_file: path is NULL\n");
    return -1;
  }

  if (!content)
  {
    serial_print("SERIAL: minix_fs_write_file: content is NULL\n");
    return -1;
  }

  serial_print("SERIAL: minix_fs_write_file: path=");
  serial_print(path);
  serial_print(" content=");
  serial_print(content);
  serial_print("\n");

  // Verificar que el disco esté disponible
  if (!ata_is_available())
  {
    extern void serial_print(const char *str);
    serial_print("SERIAL: write: disk not available\n");
    return -EIO;
  }

  // Buscar si el archivo ya existe
  minix_inode_t *file_inode = minix_fs_find_inode(path);
  uint16_t inode_num = 0;

  if (file_inode)
  {
    // El archivo existe, obtener su número de inode
    inode_num = minix_fs_get_inode_number(path);
    if (inode_num == 0)
    {
      return -1;
    }
  }
  else
  {
    // El archivo no existe, crearlo primero
    if (minix_fs_touch(path, 0644) != 0)
    {
      extern void serial_print(const char *str);
      serial_print("SERIAL: Error: Could not create file ");
      serial_print(path);
      serial_print("\n");
      return -1;
    }

    // Obtener el inode del archivo recién creado
    file_inode = minix_fs_find_inode(path);
    if (!file_inode)
    {
      return -1;
    }

    inode_num = minix_fs_get_inode_number(path);
    if (inode_num == 0)
    {
      return -1;
    }
  }

  // Calcular el tamaño del contenido
  uint32_t content_size = kstrlen(content);

  // Verificar que no exceda el tamaño máximo de archivo
  if (content_size > MINIX_BLOCK_SIZE * 7)
  {
    extern void serial_print(const char *str);
    extern void serial_print_hex32(uint32_t num);
    serial_print("SERIAL: Error: Content too large (max ");
    serial_print_hex32(MINIX_BLOCK_SIZE * 7);
    serial_print(" bytes)\n");
    return -1;
  }

  // Asignar zona si el archivo no tiene ninguna
  if (file_inode->i_zone[0] == 0)
  {
    uint32_t new_zone = minix_alloc_zone();
    if (new_zone == 0)
    {
      extern void serial_print(const char *str);
      serial_print("SERIAL: Error: No free zones available\n");
      return -1;
    }
    file_inode->i_zone[0] = new_zone;
  }

  // Escribir el contenido al primer bloque
  uint8_t block_buffer[MINIX_BLOCK_SIZE];
  kmemset(block_buffer, 0, MINIX_BLOCK_SIZE);

  // Copiar el contenido al buffer
  uint32_t bytes_to_write = content_size;
  if (bytes_to_write > MINIX_BLOCK_SIZE)
  {
    bytes_to_write = MINIX_BLOCK_SIZE;
  }

  kmemcpy(block_buffer, content, bytes_to_write);

  // Escribir el bloque al disco
  if (minix_write_block(file_inode->i_zone[0], block_buffer) != 0)
  {
    extern void serial_print(const char *str);
    serial_print("SERIAL: Error: Could not write block to disk\n");
    return -1;
  }

  // Actualizar el tamaño del archivo en el inode
  file_inode->i_size = content_size;

  serial_print("SERIAL: write_file: before write_inode, inode_num=");
  serial_print_hex32(inode_num);
  serial_print(" size=");
  serial_print_hex32(file_inode->i_size);
  serial_print("\n");

  // Escribir el inode actualizado al disco
  if (minix_fs_write_inode(inode_num, file_inode) != 0)
  {
    extern void serial_print(const char *str);
    serial_print("SERIAL: Error: Could not update inode\n");
    return -1;
  }

  serial_print("SERIAL: write_file: after write_inode\n");

  extern void serial_print(const char *str);
  extern void serial_print_hex32(uint32_t num);
  serial_print("SERIAL: File '");
  serial_print(path);
  serial_print("' written successfully (");
  serial_print_hex32(content_size);
  serial_print(" bytes)\n");

  return 0;
}

/**
 * Creates a new empty file with the specified path and mode.
 * If the file already exists, updates its modification time.
 *
 * @param path Path of the file to create
 * @param mode File mode (permissions)
 * @return 0 on success, -1 on error
 */
int minix_fs_touch(const char *path, mode_t mode)
{
  if (!minix_fs.initialized)
  {
    typewriter_vga_print("Error: Filesystem not initialized\n", 0x0C);
    return -1;
  }

  if (!path || *path == '\0')
  {
    typewriter_vga_print("Error: Invalid path\n", 0x0C);
    return -1;
  }

  if (kstrcmp(path, "/") == 0)
  {
    typewriter_vga_print("Error: Cannot create file with root directory name\n", 0x0C);
    return -1;
  }

  if (!ata_is_available())
  {
    typewriter_vga_print("Error: Disk not available\n", 0x0C);
    return -EIO;
  }

  // Check if file already exists
  minix_inode_t *existing_inode = minix_fs_find_inode(path);
  if (existing_inode)
  {
    // File exists, just update its timestamp
    existing_inode->i_time = get_system_time();
    uint16_t inode_num = minix_fs_get_inode_number(path);
    if (inode_num != 0)
    {
      // Make a copy to avoid modifying the static buffer
      minix_inode_t inode_copy;
      kmemcpy(&inode_copy, existing_inode, sizeof(minix_inode_t));
      minix_fs_write_inode(inode_num, &inode_copy);
    }
    return 0;
  }

  // Parse path to get parent directory and filename
  char parent_path[256] = {0};
  char filename[64] = {0};

  if (minix_fs_split_path(path, parent_path, filename) != 0)
  {
    typewriter_vga_print("Error: Invalid path format\n", 0x0C);
    return -1;
  }

  minix_inode_t parent_inode;
  minix_inode_t *parent_inode_ptr = minix_fs_find_inode(parent_path);
  if (!parent_inode_ptr)
  {
    typewriter_vga_print("Error: Parent directory not found\n", 0x0C);
    return -1;
  }
  kmemcpy(&parent_inode, parent_inode_ptr, sizeof(minix_inode_t));

  if (!(parent_inode.i_mode & MINIX_IFDIR))
  {
    typewriter_vga_print("Error: Parent is not a directory\n", 0x0C);
    return -1;
  }

  if (kstrlen(filename) >= MINIX_NAME_LEN)
  {
    typewriter_vga_print("Error: Filename too long\n", 0x0C);
    return -1;
  }

  if (minix_fs_find_dir_entry(&parent_inode, filename) != 0)
  {
    typewriter_vga_print("Error: File already exists\n", 0x0C);
    typewriter_vga_print("Error: File already exists\n", 0x0C);
    return -1;
  }

  uint16_t new_inode_num = minix_alloc_inode();
  if (new_inode_num == 0)
  {
    typewriter_vga_print("Error: No free inodes available\n", 0x0C);
    return -1;
  }

  // Create new file inode
  minix_inode_t new_inode = {0};
  new_inode.i_mode = MINIX_IFREG | (mode & 0777);         // Regular file with given permissions
  new_inode.i_uid = 0;                                    // root
  new_inode.i_gid = 0;                                    // root
  new_inode.i_size = 0;                                   // Empty file
  new_inode.i_time = get_system_time();                   // Current time
  new_inode.i_nlinks = 1;                                 // Single hard link
  kmemset(new_inode.i_zone, 0, sizeof(new_inode.i_zone)); // No data blocks allocated yet

  if (minix_fs_write_inode(new_inode_num, &new_inode) != 0)
  {
    minix_fs_free_inode(new_inode_num);
    typewriter_vga_print("Error: Failed to write inode to disk\n", 0x0C);
    return -1;
  }

  if (minix_fs_add_dir_entry(&parent_inode, filename, new_inode_num) != 0)
  {
    minix_fs_free_inode(new_inode_num);
    typewriter_vga_print("Error: Failed to add directory entry\n", 0x0C);
    return -1;
  }

  parent_inode.i_time = get_system_time();
  uint16_t parent_inode_num = minix_fs_get_inode_number(parent_path);
  if (parent_inode_num != 0)
  {
    if (minix_fs_write_inode(parent_inode_num, &parent_inode) != 0)
    {
      typewriter_vga_print("Warning: Failed to update parent directory\n", 0x0C);
    }
  }

  return 0;
}

// ===============================================================================
// FUNCIÓN PARA ELIMINAR ARCHIVOS (RM)
// ===============================================================================

int minix_fs_rm(const char *path)
{
  if (!minix_fs.initialized)
  {
    typewriter_vga_print("Error: MINIX filesystem not initialized\n", 0x0C);
    return -1;
  }

  if (!path || kstrlen(path) == 0)
  {
    typewriter_vga_print("Error: No file path specified\n", 0x0C);
    return -1;
  }

  if (kstrcmp(path, "/") == 0)
  {
    typewriter_vga_print("Error: Cannot remove root directory\n", 0x0C);
    return -1;
  }

  minix_inode_t *file_inode = minix_fs_find_inode(path);
  if (!file_inode)
  {
    char error_msg[256];
    snprintf(error_msg, sizeof(error_msg), "rm: '%s': No such file\n", path);
    typewriter_vga_print(error_msg, 0x0C);
    return -1;
  }

  if (file_inode->i_mode & MINIX_IFDIR)
  {
    char error_msg[256];
    snprintf(error_msg, sizeof(error_msg), "rm: '%s': Is a directory\n", path);
    typewriter_vga_print(error_msg, 0x0C);
    return -1;
  }

  char parent_path[256];
  char filename[64];

  if (minix_fs_split_path(path, parent_path, filename) != 0)
  {
    typewriter_vga_print("Error: Invalid path\n", 0x0C);
    typewriter_vga_print("Error: Invalid path\n", 0x0C);
    return -1;
  }

  minix_inode_t *parent_inode = minix_fs_find_inode(parent_path);
  if (!parent_inode)
  {
    typewriter_vga_print("Error: Parent directory not found\n", 0x0C);
    typewriter_vga_print("Error: Parent directory not found\n", 0x0C);
    return -1;
  }

  uint16_t file_inode_num = minix_fs_get_inode_number(path);
  if (file_inode_num == 0)
  {
    typewriter_vga_print("Error: Could not get inode number\n", 0x0C);
    typewriter_vga_print("Error: Could not get inode number\n", 0x0C);
    return -1;
  }

  if (minix_fs_remove_dir_entry(parent_inode, filename) != 0)
  {
    typewriter_vga_print("Error: Could not remove directory entry\n", 0x0C);
    typewriter_vga_print("Error: Could not remove directory entry\n", 0x0C);
    return -1;
  }

  if (minix_fs_free_inode(file_inode_num) != 0)
  {
    typewriter_vga_print("Error: Could not free inode\n", 0x0C);
    typewriter_vga_print("Error: Could not free inode\n", 0x0C);
    return -1;
  }

  uint16_t parent_inode_num = minix_fs_get_inode_number(parent_path);
  if (parent_inode_num != 0)
  {
    if (minix_fs_write_inode(parent_inode_num, parent_inode) != 0)
    {
      typewriter_vga_print("Warning: Could not update parent directory\n", 0x0C);
      typewriter_vga_print("Warning: Could not update parent directory\n", 0x0C);
    }
  }

  return 0;
}

// Removed unused function format_permissions

// Removed unused function uint32_to_str

// This function ensures the disk has a valid MINIX filesystem
// If not, it creates one with a basic root directory
int minix_fs_ensure_valid(void)
{
  // Try to read superblock
  if (minix_read_block(1, &minix_fs.superblock) != 0)
  {
    // Can't read - format disk
    return minix_fs_format();
  }

  // Check magic number
  if (minix_fs.superblock.s_magic != MINIX_SUPER_MAGIC)
  {
    // Invalid - format disk
    return minix_fs_format();
  }

  // Valid filesystem found
  return 0;
}

/**
 * Remove a directory
 * @param path Path to the directory to remove
 * @return 0 on success, -1 on error
 */
int minix_fs_rmdir(const char *path)
{
  if (!minix_fs.initialized)
  {
    typewriter_vga_print("Error: MINIX filesystem not initialized\n", 0x0C);
    return -1;
  }

  if (!path || *path == '\0')
  {
    typewriter_vga_print("Error: No directory path specified\n", 0x0C);
    return -1;
  }

  if (kstrcmp(path, "/") == 0)
  {
    typewriter_vga_print("Error: Cannot remove root directory\n", 0x0C);
    return -1;
  }

  minix_inode_t *dir_inode_ptr = minix_fs_find_inode(path);
  if (!dir_inode_ptr)
  {
    char error_msg[256];
    snprintf(error_msg, sizeof(error_msg), "rmdir: '%s': No such file or directory\n", path);
    typewriter_vga_print(error_msg, 0x0C);
    return -1;
  }

  minix_inode_t dir_inode;
  kmemcpy(&dir_inode, dir_inode_ptr, sizeof(minix_inode_t));

  if (!(dir_inode.i_mode & MINIX_IFDIR))
  {
    char error_msg[256];
    snprintf(error_msg, sizeof(error_msg), "rmdir: '%s': Not a directory\n", path);
    typewriter_vga_print(error_msg, 0x0C);
    return -1;
  }

  // Check if directory is empty (only . and .. allowed)
  bool is_empty = true;

  for (int i = 0; i < 7 && is_empty; i++)
  {
    uint32_t zone = dir_inode.i_zone[i];
    if (zone == 0)
    {
      continue; // No more zones
    }

    uint8_t block_buffer[MINIX_BLOCK_SIZE] = {0};
    if (minix_read_block(zone, block_buffer) != 0)
    {
      typewriter_vga_print("Error: Could not read directory block\n", 0x0C);
      return -1;
    }

    minix_dir_entry_t *entries = (minix_dir_entry_t *)block_buffer;
    int num_entries = MINIX_BLOCK_SIZE / sizeof(minix_dir_entry_t);

    for (int j = 0; j < num_entries; j++)
    {
      // Skip empty entries
      if (entries[j].inode == 0)
      {
        continue;
      }

      // Only allow . and ..
      if (kstrcmp(entries[j].name, ".") != 0 &&
          kstrcmp(entries[j].name, "..") != 0)
      {
        is_empty = false;
        break;
      }
    }
  }

  if (!is_empty)
  {
    char error_msg[256];
    snprintf(error_msg, sizeof(error_msg), "rmdir: '%s': Directory not empty\n", path);
    typewriter_vga_print(error_msg, 0x0C);
    return -1;
  }

  char parent_path[256] = {0};
  char dirname[64] = {0};

  if (minix_fs_split_path(path, parent_path, dirname) != 0)
  {
    typewriter_vga_print("Error: Invalid path\n", 0x0C);
    return -1;
  }

  minix_inode_t *parent_inode_ptr = minix_fs_find_inode(parent_path);
  if (!parent_inode_ptr)
  {
    typewriter_vga_print("Error: Parent directory not found\n", 0x0C);
    return -1;
  }

  minix_inode_t parent_inode;
  kmemcpy(&parent_inode, parent_inode_ptr, sizeof(minix_inode_t));

  uint16_t dir_inode_num = minix_fs_get_inode_number(path);
  if (dir_inode_num == 0)
  {
    typewriter_vga_print("Error: Could not get inode number\n", 0x0C);
    return -1;
  }

  if (minix_fs_remove_dir_entry(&parent_inode, dirname) != 0)
  {
    typewriter_vga_print("Error: Could not remove directory entry\n", 0x0C);
    return -1;
  }

  // Free all zones used by the directory
  for (int i = 0; i < 7; i++)
  {
    if (dir_inode.i_zone[i] != 0)
    {
      minix_free_zone(dir_inode.i_zone[i]);
      dir_inode.i_zone[i] = 0;
    }
  }

  if (minix_fs_free_inode(dir_inode_num) != 0)
  {
    typewriter_vga_print("Error: Could not free inode\n", 0x0C);
    return -1;
  }

  uint16_t parent_inode_num = minix_fs_get_inode_number(parent_path);
  if (parent_inode_num != 0)
  {
    if (parent_inode.i_nlinks > 2)
    {
      parent_inode.i_nlinks--;
    }
    parent_inode.i_time = get_system_time();

    if (minix_fs_write_inode(parent_inode_num, &parent_inode) != 0)
    {
      typewriter_vga_print("Warning: Could not update parent directory inode\n", 0x0C);
    }
  }

  return 0;
}

/**
 * Read entire file into memory - for ELF loader support
 * This function reads a complete file and allocates memory for it
 */
int minix_fs_read_file(const char *path, void **data, size_t *size)
{
  if (!path || !data || !size)
  {
    return -1;
  }

  // Find the file inode
  uint16_t inode_num = minix_fs_get_inode_number(path);
  if (inode_num == 0)
  {
    return -1; // File not found
  }

  // Read the inode
  minix_inode_t inode;
  if (minix_read_inode(inode_num, &inode) != 0)
  {
    return -1;
  }

  // Check if it's a regular file
  if (!(inode.i_mode & MINIX_IFREG))
  {
    return -1; // Not a regular file
  }

  // Allocate memory for file content
  *size = inode.i_size;
  *data = kmalloc(*size);
  if (!*data)
  {
    return -1; // Memory allocation failed
  }

  // Read file content
  uint8_t *buffer = (uint8_t *)*data;
  size_t bytes_read = 0;

  // Read direct zones (first 7 zones)
  for (int i = 0; i < 7 && bytes_read < *size; i++)
  {
    if (inode.i_zone[i] == 0)
    {
      continue;
    }

    uint8_t block_buffer[MINIX_BLOCK_SIZE];
    if (minix_read_block(inode.i_zone[i], block_buffer) != 0)
    {
      kfree(*data);
      return -1;
    }

    size_t bytes_to_copy = (*size - bytes_read > MINIX_BLOCK_SIZE)
                               ? MINIX_BLOCK_SIZE
                               : (*size - bytes_read);
    kmemcpy(buffer + bytes_read, block_buffer, bytes_to_copy);
    bytes_read += bytes_to_copy;
  }

  // Handle indirect zones if needed (zone[7] and zone[8])
  if (bytes_read < *size && inode.i_zone[7] != 0)
  {
    // Single indirect zone
    uint8_t indirect_buffer[MINIX_BLOCK_SIZE];
    if (minix_read_block(inode.i_zone[7], indirect_buffer) != 0)
    {
      kfree(*data);
      return -1;
    }

    uint16_t *zone_list = (uint16_t *)indirect_buffer;
    int num_zones = MINIX_BLOCK_SIZE / sizeof(uint16_t);

    for (int i = 0; i < num_zones && bytes_read < *size; i++)
    {
      if (zone_list[i] == 0)
      {
        continue;
      }

      uint8_t block_buffer[MINIX_BLOCK_SIZE];
      if (minix_read_block(zone_list[i], block_buffer) != 0)
      {
        kfree(*data);
        return -1;
      }

      size_t bytes_to_copy = (*size - bytes_read > MINIX_BLOCK_SIZE)
                                 ? MINIX_BLOCK_SIZE
                                 : (*size - bytes_read);
      kmemcpy(buffer + bytes_read, block_buffer, bytes_to_copy);
      bytes_read += bytes_to_copy;
    }
  }

  return 0;
}

int minix_fs_stat(const char *pathname, stat_t *buf)
{
  if (!minix_fs.initialized || !pathname || !buf)
  {
    return -1;
  }

  // Find the inode for this path
  minix_inode_t *inode = minix_fs_find_inode(pathname);
  if (!inode)
  {
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
