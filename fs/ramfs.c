// SPDX-License-Identifier: GPL-3.0-only
/**
 * IR0 Kernel - RAMFS (RAM Filesystem)
 * Copyright (C) 2025  Iván Rodriguez
 *
 * Simple in-memory filesystem for boot files and temporary data
 */

#include "vfs.h"
#include <ir0/kmem.h>
#include <string.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <ir0/stat.h>
#include <ir0/oops.h>

#define RAMFS_MAX_FILES 64
#define RAMFS_MAX_FILE_SIZE 4096
#define RAMFS_MAX_NAME_LEN 255

typedef struct ramfs_file
{
  char name[RAMFS_MAX_NAME_LEN + 1];
  uint8_t *data;
  size_t size;
  mode_t mode;
  bool in_use;
} ramfs_file_t;

typedef struct ramfs_data
{
  ramfs_file_t files[RAMFS_MAX_FILES];
  size_t total_size;
} ramfs_data_t;

static ramfs_data_t *ramfs_root = NULL;

static int ramfs_mount(const char *dev_name __attribute__((unused)),  const char *dir_name __attribute__((unused)))
{
  if (ramfs_root)
    return 0;

  ramfs_root = (ramfs_data_t *)kmalloc(sizeof(ramfs_data_t));
  if (!ramfs_root)
    return -1;

  kmemset(ramfs_root, 0, sizeof(ramfs_data_t));

  extern void print(const char *str);
  print("RAMFS: Mounted successfully\n");

  return 0;
}

static int ramfs_create_file(const char *name, mode_t mode)
{
  if (!ramfs_root || !name)
    return -1;

  for (int i = 0; i < RAMFS_MAX_FILES; i++)
  {
    if (!ramfs_root->files[i].in_use)
    {
      ramfs_file_t *file = &ramfs_root->files[i];
      kstrncpy(file->name, name, RAMFS_MAX_NAME_LEN);
      file->name[RAMFS_MAX_NAME_LEN] = '\0';
      file->data = NULL;
      file->size = 0;
      file->mode = mode;
      file->in_use = true;
      return 0;
    }
  }

  return -1;
}

static ramfs_file_t *ramfs_find_file(const char *name)
{
  if (!ramfs_root || !name)
    return NULL;

  for (int i = 0; i < RAMFS_MAX_FILES; i++)
  {
    if (ramfs_root->files[i].in_use && kstrcmp(ramfs_root->files[i].name, name) == 0)
    {
      return &ramfs_root->files[i];
    }
  }

  return NULL;
}

static int ramfs_write_file(const char *name, const void *data, size_t size)
{
  ramfs_file_t *file = ramfs_find_file(name);
  if (!file)
  {
    if (ramfs_create_file(name, 0644) != 0)
      return -1;
    file = ramfs_find_file(name);
    if (!file)
      return -1;
  }

  if (file->data)
    kfree(file->data);

  file->data = (uint8_t *)kmalloc(size);
  if (!file->data)
    return -1;

  kmemcpy(file->data, data, size);
  file->size = size;
  ramfs_root->total_size += size;

  return 0;
}

[[maybe_unused]] static int ramfs_read_file(const char *name, void *buf, size_t size, size_t *read)
{
  ramfs_file_t *file = ramfs_find_file(name);
  if (!file || !file->data)
    return -1;

  size_t to_read = (size < file->size) ? size : file->size;


  kmemcpy(buf, file->data, to_read);

  if (read)
  {
    *read = to_read;
  }

  return 0;
}

[[maybe_unused]] static int ramfs_list_files(char *names[], int max_names)
{
  if (!ramfs_root)
    return 0;

  int count = 0;
  for (int i = 0; i < RAMFS_MAX_FILES && count < max_names; i++)
  {
    if (ramfs_root->files[i].in_use)
    {
      names[count] = ramfs_root->files[i].name;
      count++;
    }
  }

  return count;
}

static struct filesystem_type ramfs_fs_type = 
{
    .name = "ramfs",
    .mount = ramfs_mount,
    .next = NULL
};

int ramfs_register(void)
{
  extern int register_filesystem(struct filesystem_type * fs_type);
  return register_filesystem(&ramfs_fs_type);
}

int ramfs_init_boot_files(void)
{
  if (!ramfs_root)
    return -1;

  const char *boot_info = "IR0 Kernel v0.0.1\nBoot filesystem\n";
  ramfs_write_file("boot.txt", boot_info, kstrlen(boot_info));

  const char *kernel_info = "Kernel: IR0 v0.0.1\nArchitecture: x86-64\n";
  ramfs_write_file("kernel.info", kernel_info, kstrlen(kernel_info));

  return 0;
}
