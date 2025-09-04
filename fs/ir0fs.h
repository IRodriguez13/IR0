// fs/ir0fs.h - IR0 File System Implementation
#pragma once

#include "vfs.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// ===============================================================================
// IR0FS CONSTANTS AND LIMITS
// ===============================================================================

#define IR0FS_MAGIC 0x49523046     // "IR0F"
#define IR0FS_VERSION 0x0100       // Version 1.0
#define IR0FS_BLOCK_SIZE 4096      // 4KB blocks
#define IR0FS_MAX_BLOCKS 0x1000000 // 64GB max filesystem size
#define IR0FS_MAX_FILES 0x100000   // 1M files max
#define IR0FS_MAX_FILENAME 255     // Max filename length
#define IR0FS_MAX_PATH 4096        // Max path length
#define IR0FS_INODES_PER_BLOCK (IR0FS_BLOCK_SIZE / sizeof(ir0fs_inode_t))
#define IR0FS_DIR_ENTRIES_PER_BLOCK (IR0FS_BLOCK_SIZE / sizeof(ir0fs_dirent_t))

// IR0FS block types
typedef enum
{
    IR0FS_BLOCK_FREE = 0,
    IR0FS_BLOCK_SUPERBLOCK,
    IR0FS_BLOCK_INODE_TABLE,
    IR0FS_BLOCK_DATA,
    IR0FS_BLOCK_BITMAP,
    IR0FS_BLOCK_JOURNAL,
    IR0FS_BLOCK_ROOT_DIR
} ir0fs_block_type_t;

// IR0FS inode types
typedef enum
{
    IR0FS_INODE_TYPE_FILE = 1,
    IR0FS_INODE_TYPE_DIRECTORY = 2,
    IR0FS_INODE_TYPE_SYMLINK = 3,
    IR0FS_INODE_TYPE_DEVICE = 4
} ir0fs_inode_type_t;

// IR0FS checksum types
typedef enum
{
    IR0FS_CHECKSUM_NONE = 0,
    IR0FS_CHECKSUM_CRC32 = 1,
    IR0FS_CHECKSUM_SHA256 = 2
} ir0fs_checksum_type_t;

// IR0FS flags
typedef enum
{
    IR0FS_FLAG_NONE = 0,
    IR0FS_FLAG_JOURNALING = 0x0001,
    IR0FS_FLAG_COMPRESSION = 0x0002,
    IR0FS_FLAG_ENCRYPTION = 0x0004
} ir0fs_flags_t;

// IR0FS inode flags
typedef enum
{
    IR0FS_INODE_DIRTY = 0x0001,
    IR0FS_INODE_COMPRESSED = 0x0002,
    IR0FS_INODE_ENCRYPTED = 0x0004,
    IR0FS_INODE_SYMLINK = 0x0008,
    IR0FS_INODE_HARDLINK = 0x0010,
    IR0FS_INODE_SPARSE = 0x0020,
    IR0FS_INODE_IMMUTABLE = 0x0040,
    IR0FS_INODE_APPEND = 0x0080
} ir0fs_inode_flags_t;

// IR0FS compression types
typedef enum
{
    IR0FS_COMPRESS_NONE = 0,
    IR0FS_COMPRESS_LZ4,
    IR0FS_COMPRESS_ZSTD,
    IR0FS_COMPRESS_LZMA
} ir0fs_compression_t;

// ===============================================================================
// IR0FS STRUCTURES
// ===============================================================================

// IR0FS superblock
typedef struct
{
    uint32_t magic;             // Magic number
    uint32_t version;           // Filesystem version
    uint64_t total_blocks;      // Total number of blocks
    uint64_t free_blocks;       // Number of free blocks
    uint64_t total_inodes;      // Total number of inodes
    uint64_t free_inodes;       // Number of free inodes
    uint32_t block_size;        // Block size in bytes
    uint32_t inode_size;        // Inode size in bytes
    uint64_t first_data_block;  // First data block
    uint64_t data_blocks_start; // Start of data blocks
    uint64_t inode_table_start; // Start of inode table
    uint64_t bitmap_start;      // Start of block bitmap
    uint64_t journal_start;     // Start of journal
    uint64_t journal_size;      // Journal size in blocks
    uint64_t last_mount_time;   // Last mount time
    uint64_t last_write_time;   // Last write time
    uint32_t mount_count;       // Number of mounts
    uint32_t max_mount_count;   // Max mounts before fsck
    uint32_t state;             // Filesystem state
    uint32_t error_behavior;    // Error handling behavior
    uint32_t compression;       // Compression type
    uint32_t checksum_type;     // Checksum type
    uint32_t flags;             // Filesystem flags
    uint8_t volume_name[64];    // Volume name
    uint8_t uuid[16];           // Filesystem UUID
    uint8_t reserved[256];      // Reserved for future use
} __attribute__((packed)) ir0fs_superblock_t;

// IR0FS inode
typedef struct
{
    uint32_t ino;                  // Inode number
    uint32_t mode;                 // File mode and permissions
    uint32_t uid;                  // Owner ID
    uint32_t gid;                  // Group ID
    uint64_t size;                 // File size in bytes
    uint64_t blocks;               // Number of blocks
    uint64_t atime;                // Access time
    uint64_t mtime;                // Modification time
    uint64_t ctime;                // Creation time
    uint32_t links;                // Hard link count
    uint32_t flags;                // Inode flags
    uint32_t compression;          // Compression type
    uint32_t encryption;           // Encryption type
    uint32_t checksum;             // Inode checksum
    uint32_t type;                 // Inode type (file, directory, etc.)
    uint32_t permissions;          // File permissions
    char name[IR0FS_MAX_FILENAME]; // File name

    // Direct block pointers (12 blocks = 48KB)
    uint64_t direct_blocks[12];

    // Indirect block pointer (1 block = 4KB of pointers = 1GB)
    uint64_t indirect_block;

    // Double indirect block pointer (1 block = 4KB of pointers = 1TB)
    uint64_t double_indirect_block;

    // Triple indirect block pointer (1 block = 4KB of pointers = 1PB)
    uint64_t triple_indirect_block;

    // Extended attributes
    uint32_t xattr_block; // Extended attributes block
    uint32_t reserved[4]; // Reserved for future use
} __attribute__((packed)) ir0fs_inode_t;

// IR0FS directory entry
typedef struct
{
    uint32_t ino;                  // Inode number
    uint8_t type;                  // File type
    uint8_t name_len;              // Name length
    char name[IR0FS_MAX_FILENAME]; // File name
} __attribute__((packed)) ir0fs_dirent_t;

// IR0FS journal header
typedef struct
{
    uint32_t magic;       // Journal magic
    uint32_t sequence;    // Sequence number
    uint32_t block_count; // Number of blocks in transaction
    uint64_t timestamp;   // Transaction timestamp
    uint32_t checksum;    // Journal header checksum
} __attribute__((packed)) ir0fs_journal_header_t;

// IR0FS journal entry
typedef struct
{
    uint64_t block_number; // Block number
    uint32_t operation;    // Operation type
    uint32_t checksum;     // Block checksum
} __attribute__((packed)) ir0fs_journal_entry_t;

// IR0FS filesystem info
typedef struct
{
    ir0fs_superblock_t *superblock;
    uint8_t *bitmap;
    uint64_t bitmap_size;
    uint64_t bitmap_blocks;
    uint64_t inode_table_blocks;
    uint64_t data_blocks_start;
    uint64_t journal_blocks;
    bool journal_enabled;
    uint32_t journal_sequence;
    void *device_data;
} ir0fs_fs_info_t;

// ===============================================================================
// IR0FS OPERATIONS
// ===============================================================================

// Initialize IR0FS filesystem
int ir0fs_init(void);

// Format a device with IR0FS
int ir0fs_format(const char *device_path, const char *volume_name);

// Mount IR0FS filesystem
int ir0fs_mount(const char *device_path, const char *mount_point);

// Unmount IR0FS filesystem
int ir0fs_umount(const char *mount_point);

// Directory operations
int ir0fs_readdir(ir0fs_fs_info_t *fs_info, ir0fs_inode_t *dir_inode, ir0fs_dirent_t *dirent, uint32_t *offset);
int ir0fs_add_dirent(ir0fs_fs_info_t *fs_info, ir0fs_inode_t *dir_inode, const char *name, uint32_t ino, uint32_t type);
int ir0fs_remove_dirent(ir0fs_fs_info_t *fs_info, ir0fs_inode_t *dir_inode, const char *name);

// Block I/O operations
int ir0fs_read_block(ir0fs_fs_info_t *fs_info, uint64_t block_num, void *buffer);
int ir0fs_write_block(ir0fs_fs_info_t *fs_info, uint64_t block_num, const void *buffer);

// Global filesystem info
extern ir0fs_fs_info_t ir0fs_info;

// ===============================================================================
// IR0FS INTERNAL FUNCTIONS
// ===============================================================================

// Block management
uint64_t ir0fs_alloc_block(ir0fs_fs_info_t *fs_info);
int ir0fs_free_block(ir0fs_fs_info_t *fs_info, uint64_t block);
int ir0fs_is_block_free(ir0fs_fs_info_t *fs_info, uint64_t block);

// Inode management
uint32_t ir0fs_alloc_inode(ir0fs_fs_info_t *fs_info);
int ir0fs_free_inode(ir0fs_fs_info_t *fs_info, uint32_t ino);
int ir0fs_get_inode(ir0fs_fs_info_t *fs_info, uint32_t ino, ir0fs_inode_t *inode);
int ir0fs_write_inode(ir0fs_fs_info_t *fs_info, ir0fs_inode_t *inode);

// Block mapping
int ir0fs_get_block(ir0fs_fs_info_t *fs_info, ir0fs_inode_t *inode, uint64_t block_num, uint64_t *physical_block);
int ir0fs_set_block(ir0fs_fs_info_t *fs_info, ir0fs_inode_t *inode, uint64_t block_num, uint64_t physical_block);

// Journal operations
int ir0fs_journal_start(ir0fs_fs_info_t *fs_info);
int ir0fs_journal_commit(ir0fs_fs_info_t *fs_info);
int ir0fs_journal_rollback(ir0fs_fs_info_t *fs_info);
int ir0fs_journal_write_block(ir0fs_fs_info_t *fs_info, uint64_t block_num, void *data);

// Compression
int ir0fs_compress_block(void *input, size_t input_size, void *output, size_t *output_size, ir0fs_compression_t type);
int ir0fs_decompress_block(void *input, size_t input_size, void *output, size_t *output_size, ir0fs_compression_t type);

// Checksum calculation
uint32_t ir0fs_calculate_checksum(void *data, size_t size);
bool ir0fs_verify_checksum(void *data, size_t size, uint32_t expected_checksum);

// Directory operations
int ir0fs_find_dirent(ir0fs_fs_info_t *fs_info, ir0fs_inode_t *dir_inode, const char *name, uint32_t *ino, uint8_t *type);

// Utility functions
uint64_t ir0fs_get_block_count(ir0fs_fs_info_t *fs_info);
uint64_t ir0fs_get_free_blocks(ir0fs_fs_info_t *fs_info);
uint64_t ir0fs_get_free_inodes(ir0fs_fs_info_t *fs_info);
int ir0fs_fsck(const char *device_path);
int ir0fs_defrag(const char *device_path);
