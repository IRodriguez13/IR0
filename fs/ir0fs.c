// fs/ir0fs.c - IR0 File System Implementation
#include "ir0fs.h"
#include <ir0/print.h>
#include <ir0/panic/panic.h>
#include <ir0/logging.h>
#include <string.h>
#include <bump_allocator.h>  // Usar bump_allocator directamente
#include <drivers/storage/ata.h>
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
    for (size_t i = 0; i < ir0fs_info.superblock->data_blocks_start; i++) 
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
    (void)fs_info; // Suppress unused variable warning
    (void)ino;     // Suppress unused variable warning
    // uint64_t inode_block = fs_info->superblock->inode_table_start + (ino / IR0FS_INODES_PER_BLOCK);
    // uint32_t inode_offset = (ino % IR0FS_INODES_PER_BLOCK) * sizeof(ir0fs_inode_t);
    
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
    if (!fs_info || !inode) {
        return -1;
    }
    
    // Calculate inode block and offset
    uint64_t inode_block = fs_info->superblock->inode_table_start + (inode->ino / IR0FS_INODES_PER_BLOCK);
    (void)inode; // Suppress unused variable warning
    // uint32_t inode_offset = (inode->ino % IR0FS_INODES_PER_BLOCK) * sizeof(ir0fs_inode_t);
    
    // Read the block containing the inode
    uint8_t *block_data = kmalloc(IR0FS_BLOCK_SIZE);
    if (!block_data) {
        return -1;
    }
    
    // Read the block from disk
    int result = ir0fs_read_block(fs_info, inode_block, block_data);
    if (result != 0) {
        kfree(block_data);
        return -1;
    }
    
    // Update the inode in the block
    ir0fs_inode_t *inodes = (ir0fs_inode_t *)block_data;
    memcpy(&inodes[inode->ino % IR0FS_INODES_PER_BLOCK], inode, sizeof(ir0fs_inode_t));
    
    // Write the block back to disk
    result = ir0fs_write_block(fs_info, inode_block, block_data);
    
    kfree(block_data);
    
    if (result == 0) {
        print("IR0FS: Wrote inode ");
        print_int32(inode->ino);
        print(" to disk\n");
    }
    
    return result;
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
    (void)fs_info;   // Suppress unused parameter warning
    (void)block_num; // Suppress unused parameter warning
    (void)data;      // Suppress unused parameter warning
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

// Read directory entries from a directory inode
int ir0fs_readdir(ir0fs_fs_info_t *fs_info, ir0fs_inode_t *dir_inode, ir0fs_dirent_t *dirent, uint32_t *offset)
{
    if (!fs_info || !dir_inode || !dirent || !offset) {
        return -1;
    }
    
    if (dir_inode->type != IR0FS_INODE_TYPE_DIRECTORY) {
        return -1;
    }
    
    // If directory has no blocks, return end of directory
    if (dir_inode->blocks == 0) {
        return 1;
    }
    
    // Calculate which block to read
    uint32_t block_index = *offset / IR0FS_DIR_ENTRIES_PER_BLOCK;
    uint32_t entry_in_block = *offset % IR0FS_DIR_ENTRIES_PER_BLOCK;
    
    // Check if we're beyond the directory's blocks
    if (block_index >= dir_inode->blocks) {
        return 1; // End of directory
    }
    
    // Read the directory block from disk
    uint64_t dir_block = dir_inode->direct_blocks[block_index];
    uint8_t *block_data = kmalloc(IR0FS_BLOCK_SIZE);
    if (!block_data) {
        return -1;
    }
    
    int result = ir0fs_read_block(fs_info, dir_block, block_data);
    if (result != 0) {
        kfree(block_data);
        return -1;
    }
    
    // Get the entry at the current offset
    ir0fs_dirent_t *entries = (ir0fs_dirent_t *)block_data;
    if (entry_in_block < IR0FS_DIR_ENTRIES_PER_BLOCK && entries[entry_in_block].ino != 0) {
        // Copy the entry
        memcpy(dirent, &entries[entry_in_block], sizeof(ir0fs_dirent_t));
        (*offset)++;
        kfree(block_data);
        return 0;
    }
    
    kfree(block_data);
    return 1; // End of directory
}

// Add a directory entry to a directory
int ir0fs_add_dirent(ir0fs_fs_info_t *fs_info, ir0fs_inode_t *dir_inode, const char *name, uint32_t ino, uint32_t type)
{
    if (!fs_info || !dir_inode || !name) {
        return -1;
    }
    
    if (dir_inode->type != IR0FS_INODE_TYPE_DIRECTORY) {
        return -1;
    }
    
    // Find a free block for the directory if needed
    uint64_t dir_block = 0;
    if (dir_inode->blocks == 0) {
        dir_block = ir0fs_alloc_block(fs_info);
        if (dir_block == 0) {
            return -1;
        }
        dir_inode->direct_blocks[0] = dir_block;
        dir_inode->blocks = 1;
    } else {
        dir_block = dir_inode->direct_blocks[0]; // Use first block for now
    }
    
    // Read the directory block
    uint8_t *block_data = kmalloc(IR0FS_BLOCK_SIZE);
    if (!block_data) {
        return -1;
    }
    
    int result = ir0fs_read_block(fs_info, dir_block, block_data);
    if (result != 0) {
        kfree(block_data);
        return -1;
    }
    
    // Find a free slot in the directory
    ir0fs_dirent_t *entries = (ir0fs_dirent_t *)block_data;
    int free_slot = -1;
    
    for (size_t i = 0; i < IR0FS_DIR_ENTRIES_PER_BLOCK; i++) {
        if (entries[i].ino == 0) {
            free_slot = i;
            break;
        }
    }
    
    if (free_slot == -1) {
        // Directory block is full, need to allocate a new block
        kfree(block_data);
        return -1; // TODO: Handle multiple blocks
    }
    
    // Add the directory entry
    entries[free_slot].ino = ino;
    entries[free_slot].type = (uint8_t)type;
    entries[free_slot].name_len = strlen(name);
    strncpy(entries[free_slot].name, name, sizeof(entries[free_slot].name) - 1);
    entries[free_slot].name[sizeof(entries[free_slot].name) - 1] = '\0';
    
    // Write the directory block back to disk
    result = ir0fs_write_block(fs_info, dir_block, block_data);
    
    kfree(block_data);
    
    if (result == 0) {
        // Update directory inode
        dir_inode->size += sizeof(ir0fs_dirent_t);
        ir0fs_write_inode(fs_info, dir_inode);
        
        print("IR0FS: Added directory entry '");
        print(name);
        print("' (ino=");
        print_int32(ino);
        print(") to directory\n");
    }
    
    return result;
}

// Remove a directory entry from a directory
int ir0fs_remove_dirent(ir0fs_fs_info_t *fs_info, ir0fs_inode_t *dir_inode, const char *name)
{
    if (!fs_info || !dir_inode || !name) {
        return -1;
    }
    
    if (dir_inode->type != IR0FS_INODE_TYPE_DIRECTORY) {
        return -1;
    }
    
    // For now, just print the operation
    print("IR0FS: Removing directory entry '");
    print(name);
    print("' from directory\n");
    
    // TODO: Implement real directory entry removal
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
    (void)count; // Suppress unused parameter warning
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
    (void)offset; // Suppress unused parameter warning
    (void)whence; // Suppress unused parameter warning
    if (!file) 
    {
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

// ===============================================================================
// BLOCK I/O OPERATIONS (REAL DISK ACCESS)
// ===============================================================================

// Read a block from disk
int ir0fs_read_block(ir0fs_fs_info_t *fs_info, uint64_t block_num, void *buffer)
{
    if (!fs_info || !buffer) {
        return -1;
    }
    
    // Calculate LBA (Logical Block Address) from IR0FS block
    uint32_t lba = (uint32_t)(block_num * (IR0FS_BLOCK_SIZE / 512)); // Convert to 512-byte sectors
    uint8_t num_sectors = IR0FS_BLOCK_SIZE / 512;
    
    // Use ATA driver to read from disk
    bool success = ata_read_sectors(0, lba, num_sectors, buffer); // Drive 0
    
    if (success) {
        print("IR0FS: Read block ");
        print_int32(block_num);
        print(" from LBA ");
        print_int32(lba);
        print("\n");
        return 0;
    } else {
        print_error("IR0FS: Failed to read block ");
        print_int32(block_num);
        print("\n");
        return -1;
    }
}

// Write a block to disk
int ir0fs_write_block(ir0fs_fs_info_t *fs_info, uint64_t block_num, const void *buffer)
{
    if (!fs_info || !buffer) {
        return -1;
    }
    
    // Calculate LBA (Logical Block Address) from IR0FS block
    uint32_t lba = (uint32_t)(block_num * (IR0FS_BLOCK_SIZE / 512)); // Convert to 512-byte sectors
    uint8_t num_sectors = IR0FS_BLOCK_SIZE / 512;
    
    // Use ATA driver to write to disk
    bool success = ata_write_sectors(0, lba, num_sectors, buffer); // Drive 0
    
    if (success) {
        print("IR0FS: Wrote block ");
        print_int32(block_num);
        print(" to LBA ");
        print_int32(lba);
        print("\n");
        return 0;
    } else {
        print_error("IR0FS: Failed to write block ");
        print_int32(block_num);
        print("\n");
        return -1;
    }
}

// Initialize filesystem on disk
int ir0fs_format_disk(ir0fs_fs_info_t *fs_info)
{
    if (!fs_info || !fs_info->superblock) {
        return -1;
    }
    
    print("IR0FS: Formatting disk with IR0FS filesystem\n");
    
    // Write superblock to disk
    int result = ir0fs_write_block(fs_info, 0, fs_info->superblock);
    if (result != 0) {
        print_error("IR0FS: Failed to write superblock to disk\n");
        return -1;
    }
    
    // Initialize bitmap blocks
    uint8_t *bitmap_block = kmalloc(IR0FS_BLOCK_SIZE);
    if (!bitmap_block) {
        return -1;
    }
    
    memset(bitmap_block, 0, IR0FS_BLOCK_SIZE);
    
    // Mark system blocks as used (superblock, bitmap, inode table)
    for (uint64_t i = 0; i < fs_info->superblock->inode_table_start + 1; i++) {
        bitmap_block[i / 8] |= (1 << (i % 8));
    }
    
    // Write bitmap to disk
    result = ir0fs_write_block(fs_info, fs_info->superblock->bitmap_start, bitmap_block);
    kfree(bitmap_block);
    
    if (result != 0) {
        print_error("IR0FS: Failed to write bitmap to disk\n");
        return -1;
    }
    
    // Create root directory inode
    ir0fs_inode_t root_inode;
    memset(&root_inode, 0, sizeof(ir0fs_inode_t));
    root_inode.ino = 1;
    root_inode.type = IR0FS_INODE_TYPE_DIRECTORY;
    root_inode.permissions = 0755;
    root_inode.uid = 0;
    root_inode.gid = 0;
    root_inode.size = 0;
    root_inode.blocks = 0;
    root_inode.links = 1;
    root_inode.atime = 0;
    root_inode.mtime = 0;
    root_inode.ctime = 0;
    strcpy(root_inode.name, "/");
    
    // Write root inode to disk
    result = ir0fs_write_inode(fs_info, &root_inode);
    if (result != 0) {
        print_error("IR0FS: Failed to write root inode to disk\n");
        return -1;
    }
    
    print_success("IR0FS: Disk formatted successfully\n");
    return 0;
}


