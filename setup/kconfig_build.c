/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Configuration Build Library
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: kconfig_build.c
 * Description: C library for executing build commands from menuconfig GUI
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <errno.h>
#include <ctype.h>

#define MAX_PATH_LEN 4096
#define MAX_CMD_LEN 8192
#define MAX_ARGS 256
#define MAX_LINE_LEN 2048
#define MAX_SUBSYSTEMS 64
#define MAX_FILES_PER_SUBSYSTEM 256

/**
 * Execute a command and return exit status
 * @param command: Command to execute (can include arguments)
 * @param kernel_root: Root directory of kernel
 * @return: Exit status (0 = success, non-zero = failure)
 */
int kconfig_execute_command(const char *command, const char *kernel_root)
{
    if (!command || !kernel_root)
        return -1;

    pid_t pid = fork();
    
    if (pid < 0)
    {
        fprintf(stderr, "Error: fork() failed: %s\n", strerror(errno));
        return -1;
    }

    if (pid == 0)
    {
        /* Child process */
        if (chdir(kernel_root) != 0)
        {
            fprintf(stderr, "Error: chdir() failed: %s\n", strerror(errno));
            exit(1);
        }

        /* Execute command using shell */
        execl("/bin/sh", "sh", "-c", command, (char *)NULL);
        
        /* Should not reach here */
        fprintf(stderr, "Error: execl() failed: %s\n", strerror(errno));
        exit(1);
    }
    else
    {
        /* Parent process - wait for child */
        int status;
        waitpid(pid, &status, 0);
        return WEXITSTATUS(status);
    }
}

/**
 * Build a single file using unibuild
 * @param file_path: Path to file relative to kernel root
 * @param kernel_root: Root directory of kernel
 * @return: Exit status (0 = success, non-zero = failure)
 */
int kconfig_build_file(const char *file_path, const char *kernel_root)
{
    if (!file_path || !kernel_root)
        return -1;

    char command[MAX_CMD_LEN];
    snprintf(command, sizeof(command), 
             "bash scripts/unibuild.sh \"%s\"", file_path);

    return kconfig_execute_command(command, kernel_root);
}

/**
 * Build multiple files using unibuild
 * @param file_paths: Array of file paths (relative to kernel root)
 * @param num_files: Number of files
 * @param kernel_root: Root directory of kernel
 * @return: Exit status (0 = success, non-zero = failure)
 */
int kconfig_build_files(const char **file_paths, int num_files, const char *kernel_root)
{
    if (!file_paths || num_files <= 0 || !kernel_root)
        return -1;

    int status = 0;
    for (int i = 0; i < num_files; i++)
    {
        if (file_paths[i])
        {
            int result = kconfig_build_file(file_paths[i], kernel_root);
            if (result != 0)
                status = result;
        }
    }

    return status;
}

/**
 * Build subsystem using build_from_config.py
 * @param config_file: Path to .config file
 * @param subsystems_json: Path to subsystems.json
 * @param arch: Architecture (e.g., "x86-64")
 * @param kernel_root: Root directory of kernel
 * @return: Exit status (0 = success, non-zero = failure)
 */
int kconfig_build_from_config(const char *config_file, const char *subsystems_json, 
                               const char *arch, const char *kernel_root)
{
    if (!config_file || !subsystems_json || !arch || !kernel_root)
        return -1;

    char command[MAX_CMD_LEN];
    snprintf(command, sizeof(command),
             "python3 scripts/kconfig/build_from_config.py \"%s\" \"%s\" \"%s\"",
             config_file, subsystems_json, arch);

    return kconfig_execute_command(command, kernel_root);
}

/**
 * Get kernel root directory (searches from current directory up)
 * @param buffer: Buffer to store path
 * @param buffer_size: Size of buffer
 * @return: 0 on success, -1 on failure
 */
int kconfig_get_kernel_root(char *buffer, size_t buffer_size)
{
    if (!buffer || buffer_size == 0)
        return -1;

    char current_path[MAX_PATH_LEN];
    if (getcwd(current_path, sizeof(current_path)) == NULL)
        return -1;

    char *path = current_path;
    char test_path[MAX_PATH_LEN + 64]; /* Extra space for appended paths */

    /* Search up the directory tree for kernel root indicators */
    while (1)
    {
        /* Check for kernel root indicators */
        int len = snprintf(test_path, sizeof(test_path), "%s/Makefile", path);
        if (len > 0 && len < (int)sizeof(test_path) && access(test_path, F_OK) == 0)
        {
            /* Also check for scripts/unibuild.sh */
            len = snprintf(test_path, sizeof(test_path), "%s/scripts/unibuild.sh", path);
            if (len > 0 && len < (int)sizeof(test_path) && access(test_path, F_OK) == 0)
            {
                len = snprintf(buffer, buffer_size, "%s", path);
                if (len > 0 && len < (int)buffer_size)
                {
                    return 0;
                }
            }
        }

        /* Move up one directory */
        char *parent = strrchr(path, '/');
        if (!parent || parent == path)
            break;

        *parent = '\0';
    }

    return -1;
}

/**
 * Load configuration from .config file
 * @param config_file: Path to .config file
 * @param selected_subsystems: Array to store selected subsystem IDs (output)
 * @param max_subsystems: Maximum number of subsystems
 * @return: Number of selected subsystems, or -1 on error
 */
static int load_config_file(const char *config_file, char selected_subsystems[][64], int max_subsystems)
{
    FILE *fp = fopen(config_file, "r");
    if (!fp)
        return -1;
    
    int count = 0;
    char line[MAX_LINE_LEN];
    
    while (fgets(line, sizeof(line), fp) && count < max_subsystems)
    {
        /* Remove newline */
        size_t len = strlen(line);
        if (len > 0 && line[len - 1] == '\n')
            line[len - 1] = '\0';
        
        /* Skip comments and empty lines */
        if (line[0] == '#' || line[0] == '\0')
            continue;
        
        /* Check for SUBSYSTEM_*=y pattern */
        if (strncmp(line, "SUBSYSTEM_", 11) == 0)
        {
            char *eq = strchr(line, '=');
            if (eq && strcmp(eq + 1, "y") == 0)
            {
                /* Extract subsystem ID */
                *eq = '\0';
                const char *subsys_id = line + 11; /* Skip "SUBSYSTEM_" */
                if (strlen(subsys_id) < 64)
                {
                    strncpy(selected_subsystems[count], subsys_id, 63);
                    selected_subsystems[count][63] = '\0';
                    count++;
                }
            }
        }
    }
    
    fclose(fp);
    return count;
}

/**
 * Parse JSON to extract files for a subsystem (simple JSON parser)
 * Note: This is a simplified parser that works with the specific JSON structure
 */
static int parse_subsystem_files(const char *subsystems_json, const char *subsystem_id, 
                                  const char *arch, char files[][MAX_PATH_LEN], int max_files)
{
    FILE *fp = fopen(subsystems_json, "r");
    if (!fp)
        return -1;
    
    /* Read entire file */
    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    
    char *content = malloc(size + 1);
    if (!content)
    {
        fclose(fp);
        return -1;
    }
    
    size_t bytes_read = fread(content, 1, size, fp);
    if (bytes_read != (size_t)size)
    {
        free(content);
        fclose(fp);
        return -1;
    }
    content[size] = '\0';
    fclose(fp);
    
    /* First find "subsystems" section */
    char *subsystems_start = strstr(content, "\"subsystems\":");
    if (!subsystems_start)
    {
        free(content);
        return 0;
    }
    
    /* Simple search for subsystem within subsystems */
    char search_pattern[256]; /* Increased size to avoid truncation */
    size_t subsystem_id_len = strlen(subsystem_id);
    if (subsystem_id_len > 200) /* Limit to prevent overflow */
    {
        free(content);
        return -1;
    }
    snprintf(search_pattern, sizeof(search_pattern), "\"%s\":", subsystem_id);
    
    char *subsys_start = strstr(subsystems_start, search_pattern);
    if (!subsys_start)
    {
        free(content);
        return 0;
    }
    
    /* Find "files" section within this subsystem */
    char *files_start = strstr(subsys_start, "\"files\":");
    if (!files_start)
    {
        free(content);
        return 0;
    }
    
    /* Find files section for this architecture */
    char arch_pattern[128];
    snprintf(arch_pattern, sizeof(arch_pattern), "\"%s\":", arch);
    
    char *arch_start = strstr(files_start, arch_pattern);
    if (!arch_start)
    {
        free(content);
        return 0;
    }
    
    /* Find array start */
    char *array_start = strchr(arch_start, '[');
    if (!array_start)
    {
        free(content);
        return 0;
    }
    
    int file_count = 0;
    char *current = array_start + 1;
    
    /* Parse file paths */
    while (*current && file_count < max_files)
    {
        /* Skip whitespace */
        while (*current && isspace(*current))
            current++;
        
        if (*current == ']')
            break;
        
        if (*current == '"')
        {
            current++; /* Skip opening quote */
            char *file_start = current;
            char *file_end = strchr(current, '"');
            
            if (file_end)
            {
                size_t file_len = file_end - file_start;
                if (file_len < MAX_PATH_LEN)
                {
                    strncpy(files[file_count], file_start, file_len);
                    files[file_count][file_len] = '\0';
                    file_count++;
                }
                current = file_end + 1;
            }
            else
                break;
        }
        else
            current++;
    }
    
    free(content);
    return file_count;
}

/**
 * Generate dynamic Makefile based on selected subsystems
 * @param config_file: Path to .config file
 * @param subsystems_json: Path to subsystems.json
 * @param arch: Architecture (e.g., "x86-64")
 * @param kernel_root: Root directory of kernel
 * @return: 0 on success, -1 on error
 */
int kconfig_generate_makefile(const char *config_file, const char *subsystems_json,
                               const char *arch, const char *kernel_root)
{
    if (!config_file || !subsystems_json || !arch || !kernel_root)
        return -1;
    
    /* Load selected subsystems */
    char selected_subsystems[MAX_SUBSYSTEMS][64];
    int num_selected = load_config_file(config_file, selected_subsystems, MAX_SUBSYSTEMS);
    
    if (num_selected < 0)
    {
        fprintf(stderr, "Error: Failed to load config file: %s\n", config_file);
        return -1;
    }
    
    if (num_selected == 0)
    {
        fprintf(stderr, "Warning: No subsystems selected\n");
    }
    
    /* Create .build directory in setup */
    char build_dir[MAX_PATH_LEN];
    snprintf(build_dir, sizeof(build_dir), "%s/setup/.build", kernel_root);
    
    if (mkdir(build_dir, 0755) != 0 && errno != EEXIST)
    {
        fprintf(stderr, "Error: Failed to create build directory: %s\n", strerror(errno));
        return -1;
    }
    
    /* Generate Makefile path */
    char makefile_path[MAX_PATH_LEN];
    size_t build_dir_len = strlen(build_dir);
    if (build_dir_len + 18 > MAX_PATH_LEN) /* 18 = strlen("/Makefile.dynamic") */
    {
        fprintf(stderr, "Error: Build directory path too long\n");
        return -1;
    }
    snprintf(makefile_path, sizeof(makefile_path), "%s/Makefile.dynamic", build_dir);
    
    FILE *fp = fopen(makefile_path, "w");
    if (!fp)
    {
        fprintf(stderr, "Error: Failed to create Makefile: %s\n", strerror(errno));
        return -1;
    }
    
    /* Write Makefile header */
    fprintf(fp, "# ===============================================================================\n");
    fprintf(fp, "# IR0 KERNEL DYNAMIC MAKEFILE\n");
    fprintf(fp, "# ===============================================================================\n");
    fprintf(fp, "# Auto-generated by kconfig_build library\n");
    fprintf(fp, "# DO NOT EDIT MANUALLY - This file is regenerated on each configuration change\n");
    fprintf(fp, "# ===============================================================================\n\n");
    
    fprintf(fp, "KERNEL_ROOT := %s\n", kernel_root);
    fprintf(fp, "ARCH := %s\n\n", arch);
    
    /* Write compiler configuration (from main Makefile) */
    fprintf(fp, "# Compiler configuration\n");
    fprintf(fp, "CC = gcc\n");
    fprintf(fp, "LD = ld\n");
    fprintf(fp, "ASM = nasm\n");
    fprintf(fp, "NASM = nasm\n\n");
    
    fprintf(fp, "# Flags\n");
    fprintf(fp, "CFLAGS = -m64 -ffreestanding -mcmodel=large -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -nostdlib -lgcc -I./includes -I./ -g -Wall -Wextra -fno-stack-protector -fno-builtin\n");
    fprintf(fp, "LDFLAGS = -T kernel/linker.ld -z max-page-size=0x1000\n");
    fprintf(fp, "NASMFLAGS = -f elf64\n");
    fprintf(fp, "ASMFLAGS = -f elf64\n\n");
    
    /* Include paths */
    fprintf(fp, "# Include paths\n");
    fprintf(fp, "CFLAGS += -I$(KERNEL_ROOT)\n");
    fprintf(fp, "CFLAGS += -I$(KERNEL_ROOT)/includes\n");
    fprintf(fp, "CFLAGS += -I$(KERNEL_ROOT)/includes/ir0\n");
    fprintf(fp, "CFLAGS += -I$(KERNEL_ROOT)/mm\n");
    fprintf(fp, "CFLAGS += -I$(KERNEL_ROOT)/arch/common\n");
    fprintf(fp, "CFLAGS += -I$(KERNEL_ROOT)/arch/$(ARCH)/include\n");
    fprintf(fp, "CFLAGS += -I$(KERNEL_ROOT)/include\n");
    fprintf(fp, "CFLAGS += -I$(KERNEL_ROOT)/kernel\n");
    fprintf(fp, "CFLAGS += -I$(KERNEL_ROOT)/drivers\n");
    fprintf(fp, "CFLAGS += -I$(KERNEL_ROOT)/fs\n");
    fprintf(fp, "CFLAGS += -I$(KERNEL_ROOT)/interrupt\n");
    fprintf(fp, "CFLAGS += -I$(KERNEL_ROOT)/mm\n");
    fprintf(fp, "CFLAGS += -I$(KERNEL_ROOT)/scheduler\n\n");
    
    /* Collect all object files from selected subsystems */
    fprintf(fp, "# Object files from selected subsystems\n");
    fprintf(fp, "OBJS =\n"); /* Initialize OBJS variable */
    int total_files = 0;
    
    for (int i = 0; i < num_selected; i++)
    {
        char files[MAX_FILES_PER_SUBSYSTEM][MAX_PATH_LEN];
        int num_files = parse_subsystem_files(subsystems_json, selected_subsystems[i], arch, files, MAX_FILES_PER_SUBSYSTEM);
        
        if (num_files > 0)
        {
            fprintf(fp, "# Subsystem: %s (%d files)\n", selected_subsystems[i], num_files);
            for (int j = 0; j < num_files; j++)
            {
                /* Convert source file to object file */
                char obj_file[MAX_PATH_LEN];
                strncpy(obj_file, files[j], sizeof(obj_file) - 1);
                obj_file[sizeof(obj_file) - 1] = '\0';
                
                /* Replace extension */
                char *ext = strrchr(obj_file, '.');
                if (ext)
                {
                    if (strcmp(ext, ".c") == 0)
                        strcpy(ext, ".o");
                    else if (strcmp(ext, ".asm") == 0)
                        strcpy(ext, ".o");
                    else if (strcmp(ext, ".cpp") == 0)
                        strcpy(ext, ".o");
                }
                
                fprintf(fp, "OBJS += %s\n", obj_file);
                total_files++;
            }
            fprintf(fp, "\n");
        }
    }
    
    fprintf(fp, "# Total: %d object file(s) from %d subsystem(s)\n\n", total_files, num_selected);
    
    /* Build rules */
    fprintf(fp, "# Build rules\n");
    fprintf(fp, ".PHONY: all clean\n\n");
    
    fprintf(fp, "all: $(OBJS)\n");
    fprintf(fp, "\t@echo \"✓ Compiled %d object file(s)\"\n\n", total_files);
    
    fprintf(fp, "# Compile C files\n");
    fprintf(fp, "%%.o: %%.c\n");
    fprintf(fp, "\t@echo \"  CC      $<\"\n");
    fprintf(fp, "\t@$(CC) $(CFLAGS) -c $< -o $@\n\n");
    
    fprintf(fp, "# Compile C++ files\n");
    fprintf(fp, "%%.o: %%.cpp\n");
    fprintf(fp, "\t@echo \"  CXX     $<\"\n");
    fprintf(fp, "\t@g++ -m64 -ffreestanding -fno-exceptions -fno-rtti -fno-threadsafe-statics \\\n");
    fprintf(fp, "\t\t-mcmodel=large -mno-red-zone -mno-mmx -mno-sse -mno-sse2 \\\n");
    fprintf(fp, "\t\t-nostdlib -lgcc -g -Wall -Wextra -fno-stack-protector -fno-builtin \\\n");
    fprintf(fp, "\t\t-I./cpp/include $(CFLAGS) -c $< -o $@\n\n");
    
    fprintf(fp, "# Compile ASM files\n");
    fprintf(fp, "%%.o: %%.asm\n");
    fprintf(fp, "\t@echo \"  ASM     $<\"\n");
    fprintf(fp, "\t@$(ASM) $(ASMFLAGS) $< -o $@\n\n");
    
    fprintf(fp, "# Clean rule\n");
    fprintf(fp, "clean:\n");
    fprintf(fp, "\t@echo \"Cleaning object files...\"\n");
    fprintf(fp, "\t@rm -f $(OBJS)\n");
    fprintf(fp, "\t@echo \"✓ Clean complete\"\n\n");
    
    fclose(fp);
    
    return 0;
}

/**
 * Build using dynamic Makefile
 * @param kernel_root: Root directory of kernel
 * @return: Exit status (0 = success, non-zero = failure)
 */
int kconfig_build_dynamic_makefile(const char *kernel_root)
{
    if (!kernel_root)
        return -1;
    
    char makefile_path[MAX_PATH_LEN];
    snprintf(makefile_path, sizeof(makefile_path), "%s/setup/.build/Makefile.dynamic", kernel_root);
    
    /* Check if Makefile exists */
    if (access(makefile_path, F_OK) != 0)
    {
        fprintf(stderr, "Error: Dynamic Makefile not found. Run configuration first.\n");
        return -1;
    }
    
    char command[MAX_CMD_LEN];
    snprintf(command, sizeof(command),
             "make -f %s -C %s all", makefile_path, kernel_root);
    
    return kconfig_execute_command(command, kernel_root);
}

/**
 * Clean using dynamic Makefile
 * @param kernel_root: Root directory of kernel
 * @return: Exit status (0 = success, non-zero = failure)
 */
int kconfig_clean_dynamic_makefile(const char *kernel_root)
{
    if (!kernel_root)
        return -1;
    
    char makefile_path[MAX_PATH_LEN];
    snprintf(makefile_path, sizeof(makefile_path), "%s/setup/.build/Makefile.dynamic", kernel_root);
    
    /* Check if Makefile exists */
    if (access(makefile_path, F_OK) != 0)
    {
        fprintf(stderr, "Warning: Dynamic Makefile not found. Nothing to clean.\n");
        return 0; /* Not an error if Makefile doesn't exist */
    }
    
    char command[MAX_CMD_LEN];
    snprintf(command, sizeof(command),
             "make -f %s -C %s clean", makefile_path, kernel_root);
    
    return kconfig_execute_command(command, kernel_root);
}

#ifdef BUILD_STANDALONE
/* Standalone test program */
int main(int argc, char *argv[])
{
    if (argc < 2)
    {
        fprintf(stderr, "Usage: %s <command> [args...]\n", argv[0]);
        fprintf(stderr, "Commands:\n");
        fprintf(stderr, "  build-file <file> [kernel_root]\n");
        fprintf(stderr, "  build-config <config> <subsystems_json> <arch> [kernel_root]\n");
        return 1;
    }

    char kernel_root[MAX_PATH_LEN];
    if (argc > 2 && argv[argc - 1][0] != '-')
    {
        strncpy(kernel_root, argv[argc - 1], sizeof(kernel_root) - 1);
    }
    else
    {
        if (kconfig_get_kernel_root(kernel_root, sizeof(kernel_root)) != 0)
        {
            fprintf(stderr, "Error: Could not find kernel root\n");
            return 1;
        }
    }

    if (strcmp(argv[1], "build-file") == 0)
    {
        if (argc < 3)
        {
            fprintf(stderr, "Usage: %s build-file <file> [kernel_root]\n", argv[0]);
            return 1;
        }
        return kconfig_build_file(argv[2], kernel_root);
    }
    else if (strcmp(argv[1], "build-config") == 0)
    {
        if (argc < 5)
        {
            fprintf(stderr, "Usage: %s build-config <config> <subsystems_json> <arch> [kernel_root]\n", argv[0]);
            return 1;
        }
        return kconfig_build_from_config(argv[2], argv[3], argv[4], kernel_root);
    }
    else
    {
        fprintf(stderr, "Unknown command: %s\n", argv[1]);
        return 1;
    }
}
#endif

