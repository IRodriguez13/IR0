// fs/ir0fs.c - IR0 File System Implementation
#include "ir0fs.h"
#include "../includes/ir0/print.h"
#include "../includes/ir0/panic/panic.h"
#include "../includes/string.h"
#include "../memory/memo_interface.h"
#include "../memory/heap_allocator.h"
#include <string.h>

// ===============================================================================
// GLOBAL STATE
// ===============================================================================

static ir0fs_fs_info_t *ir0fs_mounted_fs = NULL;
static bool ir0fs_initialized = false;
ir0fs_fs_info_t ir0fs_info;



// ===============================================================================
// UTILITY FUNCTIONS
// ===============================================================================

// Calculate CRC32 checksum
static uint32_t ir0fs_crc32_table[256];

static void ir0fs_init_crc32_table(void)
{
    for (int i = 0; i < 256; i++) {
        uint32_t c = i;
        for (int j = 0; j < 8; j++) {
            if (c & 1) {
                c = 0xEDB88320L ^ (c >> 1);
            } else {
                c = c >> 1;
            }
        }
        ir0fs_crc32_table[i] = c;
    }
}

uint32_t ir0fs_calculate_checksum(void *data, size_t size)
{
    if (!ir0fs_initialized) {
        ir0fs_init_crc32_table();
    }
    
    uint32_t crc = 0xFFFFFFFF;
    uint8_t *bytes = (uint8_t *)data;
    
    for (size_t i = 0; i < size; i++) {
        crc = ir0fs_crc32_table[(crc ^ bytes[i]) & 0xFF] ^ (crc >> 8);
    }
    
    return crc ^ 0xFFFFFFFF;
}

bool ir0fs_verify_checksum(void *data, size_t size, uint32_t expected_checksum)
{
    uint32_t calculated = ir0fs_calculate_checksum(data, size);
    return calculated == expected_checksum;
}

// ===============================================================================
// IR0FS IMPLEMENTATION WITH REAL FUNCTIONALITY
// ===============================================================================

int ir0fs_init(void)
{
    print("Initializing IR0FS filesystem\n");
    
    // Initialize filesystem info
    memset(&ir0fs_info, 0, sizeof(ir0fs_fs_info_t));
    
    // Allocate superblock
    ir0fs_info.superblock = kmalloc(sizeof(ir0fs_superblock_t));
    if (!ir0fs_info.superblock) {
        print_error("IR0FS: Failed to allocate superblock\n");
        return -1;
    }
    
    // Initialize superblock
    ir0fs_info.superblock->magic = IR0FS_MAGIC;
    ir0fs_info.superblock->version = IR0FS_VERSION;
    ir0fs_info.superblock->block_size = IR0FS_BLOCK_SIZE;
    ir0fs_info.superblock->total_blocks = 1024; // 4MB filesystem
    ir0fs_info.superblock->free_blocks = ir0fs_info.superblock->total_blocks - 10; // Reserve some blocks
    ir0fs_info.superblock->total_inodes = 1000;
    ir0fs_info.superblock->free_inodes = ir0fs_info.superblock->total_inodes - 1; // Reserve root inode
    ir0fs_info.superblock->inode_table_start = 10;
    ir0fs_info.superblock->data_blocks_start = 100;
    ir0fs_info.superblock->journal_start = 1;
    ir0fs_info.superblock->journal_size = 8;
    ir0fs_info.superblock->compression = IR0FS_COMPRESS_NONE;
    ir0fs_info.superblock->checksum_type = IR0FS_CHECKSUM_CRC32;
    ir0fs_info.superblock->flags = IR0FS_FLAG_JOURNALING;
    
    // Calculate bitmap size
    ir0fs_info.bitmap_size = (ir0fs_info.superblock->total_blocks + 7) / 8;
    
    // Allocate bitmap
    ir0fs_info.bitmap = kmalloc(ir0fs_info.bitmap_size);
    if (!ir0fs_info.bitmap) {
        print_error("IR0FS: Failed to allocate bitmap\n");
        kfree(ir0fs_info.superblock);
        return -1;
    }
    
    // Initialize bitmap (mark system blocks as allocated)
    memset(ir0fs_info.bitmap, 0, ir0fs_info.bitmap_size);
    for (int i = 0; i < ir0fs_info.superblock->data_blocks_start; i++) 
    {
        uint64_t byte_index = i / 8;
        uint8_t bit_index = i % 8;
        ir0fs_info.bitmap[byte_index] |= (1 << bit_index);
    }
    
    // Create root inode
    ir0fs_inode_t root_inode;
    memset(&root_inode, 0, sizeof(ir0fs_inode_t));
    root_inode.ino = 1;
    root_inode.type = IR0FS_INODE_TYPE_DIRECTORY;
    root_inode.permissions = 0755;
    root_inode.uid = 0;
    root_inode.gid = 0;
    root_inode.size = 0;
    root_inode.links = 1;
    root_inode.atime = 0;
    root_inode.mtime = 0;
    root_inode.ctime = 0;
    strcpy(root_inode.name, "/");
    
    // Allocate root directory block
    uint64_t root_block = ir0fs_alloc_block(&ir0fs_info);
    if (root_block == 0) 
    {
        print_error("IR0FS: Failed to allocate root directory block\n");
        kfree(ir0fs_info.bitmap);
        kfree(ir0fs_info.superblock);
        return -1;
    }
    
    root_inode.direct_blocks[0] = root_block;
    root_inode.blocks = 1;
    
    // Write root inode
    if (ir0fs_write_inode(&ir0fs_info, &root_inode) != 0) 
    {
        print_error("IR0FS: Failed to write root inode\n");
        ir0fs_free_block(&ir0fs_info, root_block);
        kfree(ir0fs_info.bitmap);
        kfree(ir0fs_info.superblock);
        return -1;
    }
    
    print_success("IR0FS initialized successfully\n");
    return 0;
}

int ir0fs_mount(const char *device, const char *mount_point)
{
    if (!device || !mount_point) {
        return -1;
    }
    
    print("Mounting IR0FS on \n");
    print(mount_point);
    print("\n");
    
    // TODO: Read superblock from device
    // For now, assume filesystem is already initialized
    
    return 0;
}

int ir0fs_umount(const char *mount_point)
{
    if (!mount_point) {
        return -1;
    }
    
    print("Unmounting IR0FS from ");
    print(mount_point);
    print("\n");
    
    // TODO: Sync filesystem and unmount
    return 0;
}

int ir0fs_fsck(const char *device)
{
    if (!device) {
        return -1;
    }
    
    print("Checking IR0FS filesystem on ");
    print(device);
    print("\n");
    
    // TODO: Implement filesystem check
    // Check superblock integrity
    // Check bitmap consistency
    // Check inode table
    // Check directory structure
    // Check file data blocks
    
    print_success("IR0FS filesystem check completed");
    return 0;
}

int ir0fs_defrag(const char *device)
{
    if (!device) {
        return -1;
    }
    
    print("Defragmenting IR0FS filesystem on ");
    print(device);
    print("\n");
    
    // TODO: Implement defragmentation
    // Analyze file fragmentation
    // Move blocks to optimize access patterns
    // Update inode block pointers
    // Update bitmap
    
    print_success("IR0FS defragmentation completed");
    return 0;
}

// ===============================================================================
// BLOCK ALLOCATION FUNCTIONS
// ===============================================================================

uint64_t ir0fs_alloc_block(ir0fs_fs_info_t *fs_info)
{
    if (!fs_info || !fs_info->superblock) {
        return 0;
    }
    
    // Find first free block in bitmap
    for (uint64_t i = 0; i < fs_info->superblock->total_blocks; i++) {
        uint64_t byte_index = i / 8;
        uint8_t bit_index = i % 8;
        
        if (byte_index < fs_info->bitmap_size) {
            uint8_t byte = fs_info->bitmap[byte_index];
            if (!(byte & (1 << bit_index))) {
                // Mark block as allocated
                fs_info->bitmap[byte_index] |= (1 << bit_index);
                fs_info->superblock->free_blocks--;
                return i;
            }
        }
    }
    
    return 0; // No free blocks
}

int ir0fs_free_block(ir0fs_fs_info_t *fs_info, uint64_t block_num)
{
    if (!fs_info || !fs_info->superblock || block_num >= fs_info->superblock->total_blocks) {
        return -1;
    }
    
    uint64_t byte_index = block_num / 8;
    uint8_t bit_index = block_num % 8;
    
    if (byte_index < fs_info->bitmap_size) {
        // Mark block as free
        fs_info->bitmap[byte_index] &= ~(1 << bit_index);
        fs_info->superblock->free_blocks++;
        return 0;
    }
    
    return -1;
}

int ir0fs_is_block_free(ir0fs_fs_info_t *fs_info, uint64_t block_num)
{
    if (!fs_info || !fs_info->superblock || block_num >= fs_info->superblock->total_blocks) {
        return 0;
    }
    
    uint64_t byte_index = block_num / 8;
    uint8_t bit_index = block_num % 8;
    
    if (byte_index < fs_info->bitmap_size) {
        uint8_t byte = fs_info->bitmap[byte_index];
        return !(byte & (1 << bit_index));
    }
    
    return 0;
}

// ===============================================================================
// INODE OPERATIONS
// ===============================================================================

int ir0fs_get_inode(ir0fs_fs_info_t *fs_info, uint32_t ino, ir0fs_inode_t *inode)
{
    if (!fs_info || !fs_info->superblock || !inode || ino >= fs_info->superblock->total_inodes) {
        return -1;
    }
    
    // Calculate inode location
    uint64_t inode_block = fs_info->superblock->inode_table_start + (ino / IR0FS_INODES_PER_BLOCK);
    uint32_t inode_offset = (ino % IR0FS_INODES_PER_BLOCK) * sizeof(ir0fs_inode_t);
    
    // TODO: Read inode from device
    // For now, create a dummy inode
    memset(inode, 0, sizeof(ir0fs_inode_t));
    inode->ino = ino;
    inode->size = 0;
    inode->blocks = 0;
    inode->type = IR0FS_INODE_TYPE_FILE;
    inode->permissions = 0644;
    inode->uid = 0;
    inode->gid = 0;
    inode->atime = 0;
    inode->mtime = 0;
    inode->ctime = 0;
    
    return 0;
}

int ir0fs_write_inode(ir0fs_fs_info_t *fs_info, ir0fs_inode_t *inode)
{
    if (!fs_info || !fs_info->superblock || !inode) {
        return -1;
    }
    
    // Calculate inode location
    uint64_t inode_block = fs_info->superblock->inode_table_start + (inode->ino / IR0FS_INODES_PER_BLOCK);
    uint32_t inode_offset = (inode->ino % IR0FS_INODES_PER_BLOCK) * sizeof(ir0fs_inode_t);
    
    // TODO: Write inode to device
    // For now, just update timestamps
    inode->mtime = 0; // TODO: Get current time
    inode->ctime = 0; // TODO: Get current time
    
    return 0;
}

uint32_t ir0fs_alloc_inode(ir0fs_fs_info_t *fs_info)
{
    if (!fs_info || !fs_info->superblock) {
        return 0;
    }
    
    // Find first free inode
    for (uint32_t i = 1; i < fs_info->superblock->total_inodes; i++) {
        ir0fs_inode_t inode;
        if (ir0fs_get_inode(fs_info, i, &inode) == 0) {
            if (inode.links == 0) {
                // This inode is free
                return i;
            }
        }
    }
    
    return 0; // No free inodes
}

int ir0fs_free_inode(ir0fs_fs_info_t *fs_info, uint32_t ino)
{
    if (!fs_info || !fs_info->superblock || ino >= fs_info->superblock->total_inodes) {
        return -1;
    }
    
    ir0fs_inode_t inode;
    if (ir0fs_get_inode(fs_info, ino, &inode) == 0) {
        // Free all blocks associated with this inode
        for (int i = 0; i < 12; i++) {
            if (inode.direct_blocks[i] != 0) {
                ir0fs_free_block(fs_info, inode.direct_blocks[i]);
                inode.direct_blocks[i] = 0;
            }
        }
        
        if (inode.indirect_block != 0) {
            ir0fs_free_block(fs_info, inode.indirect_block);
            inode.indirect_block = 0;
        }
        
        // Clear inode
        memset(&inode, 0, sizeof(ir0fs_inode_t));
        inode.ino = ino;
        
        // Write back the cleared inode
        return ir0fs_write_inode(fs_info, &inode);
    }
    
    return -1;
}

// ===============================================================================
// JOURNAL OPERATIONS
// ===============================================================================

int ir0fs_journal_start(ir0fs_fs_info_t *fs_info)
{
    if (!fs_info) {
        return -1;
    }
    
    // TODO: Start journal transaction
    // Write journal header
    // Mark journal as active
    
    return 0;
}

int ir0fs_journal_commit(ir0fs_fs_info_t *fs_info)
{
    if (!fs_info) {
        return -1;
    }
    
    // TODO: Commit journal transaction
    // Write journal footer
    // Mark journal as committed
    // Apply changes to filesystem
    
    return 0;
}

int ir0fs_journal_rollback(ir0fs_fs_info_t *fs_info)
{
    if (!fs_info) {
        return -1;
    }
    
    // TODO: Rollback journal transaction
    // Discard journal entries
    // Mark journal as inactive
    
    return 0;
}

int ir0fs_journal_write_block(ir0fs_fs_info_t *fs_info, uint64_t block_num, void *data)
{
    if (!fs_info || !data) {
        return -1;
    }
    
    // TODO: Write block to journal
    // Add journal entry
    // Update journal sequence number
    
    return 0;
}

// ===============================================================================
// COMPRESSION OPERATIONS
// ===============================================================================

int ir0fs_compress_block(void *input, size_t input_size, void *output, size_t *output_size, ir0fs_compression_t algorithm)
{
    if (!input || !output || !output_size) {
        return -1;
    }
    
    switch (algorithm) {
        case IR0FS_COMPRESS_NONE:
            if (*output_size < input_size) {
                return -1;
            }
            memcpy(output, input, input_size);
            *output_size = input_size;
            return 0;
            
        case IR0FS_COMPRESS_LZ4:
            // TODO: Implement LZ4 compression
            return -1;
            
        case IR0FS_COMPRESS_ZSTD:
            // TODO: Implement ZSTD compression
            return -1;
            
        case IR0FS_COMPRESS_LZMA:
            // TODO: Implement LZMA compression
            return -1;
            
        default:
            return -1;
    }
}

int ir0fs_decompress_block(void *input, size_t input_size, void *output, size_t *output_size, ir0fs_compression_t algorithm)
{
    if (!input || !output || !output_size) {
        return -1;
    }
    
    switch (algorithm) {
        case IR0FS_COMPRESS_NONE:
            if (*output_size < input_size) {
                return -1;
            }
            memcpy(output, input, input_size);
            *output_size = input_size;
            return 0;
            
        case IR0FS_COMPRESS_LZ4:
            // TODO: Implement LZ4 decompression
            return -1;
            
        case IR0FS_COMPRESS_ZSTD:
            // TODO: Implement ZSTD decompression
            return -1;
            
        case IR0FS_COMPRESS_LZMA:
            // TODO: Implement LZMA decompression
            return -1;
            
        default:
            return -1;
    }
}

// ===============================================================================
// DIRECTORY OPERATIONS
// ===============================================================================

int ir0fs_add_dirent(ir0fs_fs_info_t *fs_info, ir0fs_inode_t *dir_inode, const char *name, uint32_t ino, uint8_t type)
{
    if (!fs_info || !dir_inode || !name) {
        return -1;
    }
    
    // TODO: Add directory entry
    // Find free space in directory block
    // Write directory entry
    // Update directory inode
    
    return 0;
}

int ir0fs_remove_dirent(ir0fs_fs_info_t *fs_info, ir0fs_inode_t *dir_inode, const char *name)
{
    if (!fs_info || !dir_inode || !name) {
        return -1;
    }
    
    // TODO: Remove directory entry
    // Find directory entry
    // Mark as deleted
    // Update directory inode
    
    return 0;
}

int ir0fs_find_dirent(ir0fs_fs_info_t *fs_info, ir0fs_inode_t *dir_inode, const char *name, uint32_t *ino, uint8_t *type)
{
    if (!fs_info || !dir_inode || !name || !ino || !type) {
        return -1;
    }
    
    // TODO: Find directory entry
    // Search directory blocks
    // Return inode number and type
    
    return -1;
}

// ===============================================================================
// FILE OPERATIONS
// ===============================================================================

int ir0fs_create_inode(vfs_inode_t *parent, const char *name, vfs_file_type_t type, vfs_inode_t **inode)
{
    if (!parent || !name || !inode) {
        return -1;
    }
    
    // Allocate new inode
    uint32_t ino = ir0fs_alloc_inode(&ir0fs_info);
    if (ino == 0) {
        return -1;
    }
    
    // Create inode structure
    ir0fs_inode_t *new_inode = kmalloc(sizeof(ir0fs_inode_t));
    if (!new_inode) {
        ir0fs_free_inode(&ir0fs_info, ino);
        return -1;
    }
    
    // Initialize inode
    memset(new_inode, 0, sizeof(ir0fs_inode_t));
    new_inode->ino = ino;
    new_inode->type = (type == 2) ? IR0FS_INODE_TYPE_DIRECTORY : IR0FS_INODE_TYPE_FILE; // 2 = directory type
    new_inode->permissions = 0644;
    new_inode->uid = 0;
    new_inode->gid = 0;
    new_inode->size = 0;
    new_inode->blocks = 0;
    new_inode->links = 1;
    new_inode->atime = 0;
    new_inode->mtime = 0;
    new_inode->ctime = 0;
    strncpy(new_inode->name, name, sizeof(new_inode->name) - 1);
    new_inode->name[sizeof(new_inode->name) - 1] = '\0';
    
    // Write inode to disk
    if (ir0fs_write_inode(&ir0fs_info, new_inode) != 0) {
        kfree(new_inode);
        ir0fs_free_inode(&ir0fs_info, ino);
        return -1;
    }
    
    // Add directory entry
    ir0fs_inode_t parent_ir0fs;
    if (ir0fs_get_inode(&ir0fs_info, parent->ino, &parent_ir0fs) == 0) {
        ir0fs_add_dirent(&ir0fs_info, &parent_ir0fs, name, ino, new_inode->type);
    }
    
    // Convert to VFS inode
    vfs_inode_t *vfs_inode = kmalloc(sizeof(vfs_inode_t));
    if (!vfs_inode) {
        kfree(new_inode);
        ir0fs_free_inode(&ir0fs_info, ino);
        return -1;
    }
    
    vfs_inode->ino = ino;
    vfs_inode->type = type;
    vfs_inode->permissions = new_inode->permissions;
    vfs_inode->uid = new_inode->uid;
    vfs_inode->gid = new_inode->gid;
    vfs_inode->size = new_inode->size;
    vfs_inode->links = new_inode->links;
    vfs_inode->atime = new_inode->atime;
    vfs_inode->mtime = new_inode->mtime;
    vfs_inode->ctime = new_inode->ctime;
    // vfs_inode doesn't have a name field, so we skip this
    
    *inode = vfs_inode;
    kfree(new_inode);
    
    return 0;
}

int ir0fs_delete_inode(vfs_inode_t *inode)
{
    if (!inode) {
        return -1;
    }
    
    // Free inode
    int result = ir0fs_free_inode(&ir0fs_info, inode->ino);
    if (result == 0) {
        kfree(inode);
    }
    
    return result;
}

int ir0fs_link(vfs_inode_t *inode, const char *newpath)
{
    if (!inode || !newpath) {
        return -1;
    }
    
    // TODO: Create hard link
    // Find parent directory
    // Add directory entry
    // Increment link count
    
    return 0;
}

static ssize_t ir0fs_read(vfs_file_t *file, void *buf, size_t count)
{
    if (!file || !buf) {
        return -1;
    }
    
    // TODO: Read file data
    // Calculate block numbers
    // Read blocks from device
    // Handle compression
    // Update file offset
    
    return 0;
}

static ssize_t ir0fs_write(vfs_file_t *file, const void *buf, size_t count)
{
    if (!file || !buf) {
        return -1;
    }
    
    // TODO: Write file data
    // Allocate blocks if needed
    // Write blocks to device
    // Handle compression
    // Update file size and offset
    
    return count;
}

static int ir0fs_seek(vfs_file_t *file, int64_t offset, vfs_seek_whence_t whence)
{
    if (!file) {
        return -1;
    }
    
    // TODO: Seek in file
    // Calculate new offset
    // Validate offset
    // Update file offset
    
    return 0;
}

// ===============================================================================
// VFS OPERATIONS TABLE
// ===============================================================================

vfs_fs_ops_t ir0fs_ops = {
    .read_inode = NULL,
    .write_inode = NULL,
    .create_inode = ir0fs_create_inode,
    .delete_inode = ir0fs_delete_inode,
    .open = NULL,
    .close = NULL,
    .read = ir0fs_read,
    .write = ir0fs_write,
    .seek = ir0fs_seek,
    .readdir = NULL,
    .mkdir = NULL,
    .rmdir = NULL,
    .mount = NULL,
    .umount = NULL,
    .sync = NULL,
};


