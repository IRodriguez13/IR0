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
#include <errno.h>

#define MAX_PATH_LEN 4096
#define MAX_CMD_LEN 8192
#define MAX_ARGS 256

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

