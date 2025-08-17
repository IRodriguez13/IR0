// fs/tools/ir0fs_tools.h - IR0FS Management Tools
#pragma once

#include "../ir0fs.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// ===============================================================================
// TOOL CONSTANTS
// ===============================================================================

#define IR0FS_TOOL_VERSION "1.0.0"
#define IR0FS_TOOL_BUILD_DATE __DATE__
#define IR0FS_TOOL_BUILD_TIME __TIME__

// ===============================================================================
// TOOL STRUCTURES
// ===============================================================================

// Filesystem statistics
typedef struct {
    uint64_t total_blocks;
    uint64_t free_blocks;
    uint64_t total_inodes;
    uint64_t free_inodes;
    uint64_t total_files;
    uint64_t total_directories;
    uint64_t total_symlinks;
    uint64_t fragmentation_percent;
    uint64_t last_fsck;
    uint64_t last_defrag;
} ir0fs_stats_t;

// Filesystem health report
typedef struct {
    bool superblock_valid;
    bool bitmap_valid;
    bool inode_table_valid;
    bool journal_valid;
    uint32_t orphaned_blocks;
    uint32_t orphaned_inodes;
    uint32_t corrupted_inodes;
    uint32_t journal_errors;
    uint32_t checksum_errors;
} ir0fs_health_t;

// ===============================================================================
// TOOL FUNCTIONS
// ===============================================================================

// Filesystem creation and management
int ir0fs_tool_format(const char *device_path, const char *volume_name, uint64_t size_mb);
int ir0fs_tool_mount(const char *device_path, const char *mount_point);
int ir0fs_tool_umount(const char *mount_point);
int ir0fs_tool_remount(const char *device_path, const char *mount_point);

// Filesystem analysis and maintenance
int ir0fs_tool_fsck(const char *device_path, bool repair);
int ir0fs_tool_defrag(const char *device_path, bool verbose);
int ir0fs_tool_stats(const char *device_path, ir0fs_stats_t *stats);
int ir0fs_tool_health(const char *device_path, ir0fs_health_t *health);

// File and directory operations
int ir0fs_tool_mkdir(const char *path);
int ir0fs_tool_rmdir(const char *path);
int ir0fs_tool_touch(const char *path);
int ir0fs_tool_rm(const char *path);
int ir0fs_tool_cp(const char *src, const char *dst);
int ir0fs_tool_mv(const char *src, const char *dst);
int ir0fs_tool_ln(const char *target, const char *link, bool symbolic);

// File content operations
int ir0fs_tool_cat(const char *path);
int ir0fs_tool_echo(const char *path, const char *content);
int ir0fs_tool_hexdump(const char *path, uint64_t offset, uint64_t length);

// Filesystem inspection
int ir0fs_tool_ls(const char *path, bool long_format);
int ir0fs_tool_tree(const char *path, int max_depth);
int ir0fs_tool_find(const char *path, const char *pattern);
int ir0fs_tool_du(const char *path);

// Advanced operations
int ir0fs_tool_backup(const char *device_path, const char *backup_path);
int ir0fs_tool_restore(const char *backup_path, const char *device_path);
int ir0fs_tool_resize(const char *device_path, uint64_t new_size_mb);
int ir0fs_tool_convert(const char *src_device, const char *dst_device, const char *src_fs_type);

// Debug and development tools
int ir0fs_tool_debug_superblock(const char *device_path);
int ir0fs_tool_debug_inode(const char *device_path, uint32_t ino);
int ir0fs_tool_debug_block(const char *device_path, uint64_t block);
int ir0fs_tool_debug_journal(const char *device_path);

// ===============================================================================
// UTILITY FUNCTIONS
// ===============================================================================

// Display functions
void ir0fs_tool_print_stats(const ir0fs_stats_t *stats);
void ir0fs_tool_print_health(const ir0fs_health_t *health);
void ir0fs_tool_print_progress(uint64_t current, uint64_t total, const char *operation);
void ir0fs_tool_print_error(const char *message);
void ir0fs_tool_print_success(const char *message);
void ir0fs_tool_print_warning(const char *message);

// Conversion functions
uint64_t ir0fs_tool_mb_to_blocks(uint64_t mb);
uint64_t ir0fs_tool_blocks_to_mb(uint64_t blocks);
char *ir0fs_tool_format_size(uint64_t bytes);
char *ir0fs_tool_format_time(uint64_t timestamp);

// Validation functions
bool ir0fs_tool_validate_device(const char *device_path);
bool ir0fs_tool_validate_path(const char *path);
bool ir0fs_tool_validate_size(uint64_t size_mb);

// ===============================================================================
// COMMAND LINE INTERFACE
// ===============================================================================

// Main tool entry point
int ir0fs_tool_main(int argc, char *argv[]);

// Command handlers
int ir0fs_tool_cmd_format(int argc, char *argv[]);
int ir0fs_tool_cmd_mount(int argc, char *argv[]);
int ir0fs_tool_cmd_umount(int argc, char *argv[]);
int ir0fs_tool_cmd_fsck(int argc, char *argv[]);
int ir0fs_tool_cmd_defrag(int argc, char *argv[]);
int ir0fs_tool_cmd_stats(int argc, char *argv[]);
int ir0fs_tool_cmd_health(int argc, char *argv[]);
int ir0fs_tool_cmd_ls(int argc, char *argv[]);
int ir0fs_tool_cmd_cat(int argc, char *argv[]);
int ir0fs_tool_cmd_debug(int argc, char *argv[]);

// Help functions
void ir0fs_tool_print_usage(void);
void ir0fs_tool_print_help(const char *command);
void ir0fs_tool_print_version(void);
