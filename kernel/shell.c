/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * IR0 Kernel - Built-in Shell
 * Copyright (C) 2025 Iván Rodriguez
 *
 * Interactive shell running in Ring 3 user space
 */

#include "shell.h"
#include <ir0/syscall.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>

/* Syscall numbers are defined in ir0/syscall.h */

/* ========================================================================== */
/* VGA TEXT MODE                                                              */
/* ========================================================================== */

#define VGA_WIDTH  80
#define VGA_HEIGHT 25
#define VGA_BUFFER ((volatile uint16_t *)0xB8000)

static int cursor_pos = 0;

static void vga_putchar(char c, uint8_t color)
{
	if (c == '\n')
	{
		cursor_pos = (cursor_pos / VGA_WIDTH + 1) * VGA_WIDTH;
		if (cursor_pos >= VGA_WIDTH * VGA_HEIGHT)
		{
			/* Scroll screen */
			for (int i = 0; i < (VGA_HEIGHT - 1) * VGA_WIDTH; i++)
				VGA_BUFFER[i] = VGA_BUFFER[i + VGA_WIDTH];
			for (int i = (VGA_HEIGHT - 1) * VGA_WIDTH;
			     i < VGA_HEIGHT * VGA_WIDTH; i++)
				VGA_BUFFER[i] = 0x0F20;
			cursor_pos = (VGA_HEIGHT - 1) * VGA_WIDTH;
		}
	}
	else if (c == '\b')
	{
		if (cursor_pos > 0)
		{
			cursor_pos--;
			VGA_BUFFER[cursor_pos] = (color << 8) | ' ';
		}
	}
	else
	{
		VGA_BUFFER[cursor_pos] = (color << 8) | c;
		cursor_pos++;
		if (cursor_pos >= VGA_WIDTH * VGA_HEIGHT)
		{
			/* Scroll screen */
			for (int i = 0; i < (VGA_HEIGHT - 1) * VGA_WIDTH; i++)
				VGA_BUFFER[i] = VGA_BUFFER[i + VGA_WIDTH];
			for (int i = (VGA_HEIGHT - 1) * VGA_WIDTH;
			     i < VGA_HEIGHT * VGA_WIDTH; i++)
				VGA_BUFFER[i] = 0x0F20;
			cursor_pos = (VGA_HEIGHT - 1) * VGA_WIDTH;
		}
	}
}

static void vga_print(const char *str, uint8_t color)
{
	while (*str)
		vga_putchar(*str++, color);
}

static void vga_clear(void)
{
	for (int i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++)
		VGA_BUFFER[i] = 0x0F20;
	cursor_pos = 0;
}

/* ========================================================================== */
/* STRING UTILITIES                                                           */
/* ========================================================================== */

static int str_starts_with(const char *str, const char *prefix)
{
	while (*prefix)
	{
		if (*str++ != *prefix++)
			return 0;
	}
	return 1;
}

static const char *skip_whitespace(const char *str)
{
	while (*str == ' ' || *str == '\t')
		str++;
	return str;
}

static const char *get_arg(const char *cmd, const char *command)
{
	if (!str_starts_with(cmd, command))
		return NULL;
	
	cmd += strlen(command);
	return skip_whitespace(cmd);
}

/* ========================================================================== */
/* COMMAND IMPLEMENTATIONS                                                    */
/* ========================================================================== */

static void cmd_help(void)
{
	vga_print("IR0 Shell - Available commands:\n", 0x0F);
	vga_print("  help      - Show this help\n", 0x0F);
	vga_print("  clear     - Clear screen\n", 0x0F);
	vga_print("  ls [-l] [DIR] - List directory contents (-l for details)\n", 0x0F);
	vga_print("  cat FILE  - Display file contents\n", 0x0F);
	vga_print("  mkdir DIR - Create directory\n", 0x0F);
	vga_print("  rmdir DIR - Remove empty directory\n", 0x0F);
	vga_print("  rm [-r] FILE - Remove file or empty directory\n", 0x0F);
	vga_print("  cd [DIR]  - Change directory\n", 0x0F);
	vga_print("  pwd       - Print working directory\n", 0x0F);
	vga_print("  ps        - List processes\n", 0x0F);
	vga_print("  echo TEXT - Print text\n", 0x0F);
	vga_print("  exec FILE - Execute binary\n", 0x0F);
	vga_print("  exit      - Exit shell\n", 0x0F);
}

static void cmd_clear(void)
{
	vga_clear();
	
	/* Show banner after clear */
	vga_print("IR0 Shell v0.0.1 pre-release 1\n", 0x0B);
	vga_print("Type 'help' for available commands\n\n", 0x07);
}

static void cmd_ls(const char *args)
{
	int detailed = 0;
	const char *path;

	if (!args || *args == '\0')
	{
		path = "/";
	}
	else if (str_starts_with(args, "-l "))
	{
		detailed = 1;
		path = skip_whitespace(args + 2);
		if (*path == '\0')
			path = "/";
	}
	else if (str_starts_with(args, "-l"))
	{
		detailed = 1;
		path = "/";
	}
	else
	{
		path = args;
	}

	if (detailed)
		syscall(SYS_LS_DETAILED, (uint64_t)path, 0, 0);
	else
		syscall(SYS_LS, (uint64_t)path, 0, 0);
}

static void cmd_cat(const char *filename)
{
	if (!filename || *filename == '\0')
	{
		vga_print("Usage: cat <filename>\n", 0x0C);
		return;
	}
	
	syscall(SYS_CAT, (uint64_t)filename, 0, 0);
}

static void cmd_mkdir(const char *dirname)
{
	if (!dirname || *dirname == '\0')
	{
		vga_print("Usage: mkdir <dirname>\n", 0x0C);
		return;
	}
	
	int64_t result = syscall(SYS_MKDIR, (uint64_t)dirname, 0755, 0);
	if (result < 0)
		vga_print("mkdir: failed\n", 0x0C);
}

static void cmd_rmdir(const char *dirname)
{
	if (!dirname || *dirname == '\0')
	{
		vga_print("Usage: rmdir <dirname>\n", 0x0C);
		return;
	}
	
	int64_t result = syscall(SYS_RMDIR, (uint64_t)dirname, 0, 0);
	if (result < 0)
		vga_print("rmdir: failed\n", 0x0C);
}

static void cmd_ps(void)
{
	vga_print("PID  STATE  COMMAND\n", 0x0F);
	vga_print("---  -----  -------\n", 0x0F);
	/* TODO: Implement process listing syscall */
	vga_print("  1  RUN    init\n", 0x0F);
}

static void cmd_echo(const char *text)
{
	if (text && *text)
		vga_print(text, 0x0F);
	vga_print("\n", 0x0F);
}

static void cmd_exec(const char *filename)
{
	if (!filename || *filename == '\0')
	{
		vga_print("Usage: exec <filename>\n", 0x0C);
		return;
	}
	
	int64_t result = syscall(SYS_EXEC, (uint64_t)filename, 0, 0);
	if (result < 0)
		vga_print("exec: failed\n", 0x0C);
}

static void cmd_exit(void)
{
	syscall(SYS_EXIT, 0, 0, 0);
}

static void cmd_cd(const char *dirname)
{
	if (!dirname || *dirname == '\0')
		dirname = "/";
	
	int64_t result = syscall(SYS_CHDIR, (uint64_t)dirname, 0, 0);
	if (result < 0)
		vga_print("cd: failed\n", 0x0C);
}

static void cmd_pwd(void)
{
	char cwd[256];
	int64_t result = syscall(SYS_GETCWD, (uint64_t)cwd, sizeof(cwd), 0);
	if (result >= 0)
	{
		vga_print(cwd, 0x0F);
		vga_print("\n", 0x0F);
	}
	else
	{
		vga_print("pwd: failed\n", 0x0C);
	}
}

static void cmd_rm(const char *args)
{
	int recursive = 0;
	const char *filename;

	if (!args || *args == '\0')
	{
		vga_print("Usage: rm [-r] <filename>\n", 0x0C);
		return;
	}

	/* Check for -r flag */
	if (str_starts_with(args, "-r "))
	{
		recursive = 1;
		filename = skip_whitespace(args + 2);
	}
	else if (str_starts_with(args, "-rf "))
	{
		recursive = 1;
		filename = skip_whitespace(args + 3);
	}
	else
	{
		filename = args;
	}

	if (!filename || *filename == '\0')
	{
		vga_print("Usage: rm [-r] <filename>\n", 0x0C);
		return;
	}

	/* Try to remove as file first */
	int64_t result = syscall(SYS_UNLINK, (uint64_t)filename, 0, 0);
	
	/* If failed and recursive flag is set, try as directory */
	if (result < 0 && recursive)
	{
		result = syscall(SYS_RMDIR, (uint64_t)filename, 0, 0);
		if (result < 0)
		{
			vga_print("rm: cannot remove '", 0x0C);
			vga_print(filename, 0x0C);
			vga_print("': Directory not empty or does not exist\n", 0x0C);
			vga_print("Note: Recursive deletion of non-empty directories not yet implemented\n", 0x0C);
		}
	}
	else if (result < 0)
	{
		vga_print("rm: cannot remove '", 0x0C);
		vga_print(filename, 0x0C);
		vga_print("': No such file or directory\n", 0x0C);
		vga_print("Hint: Use 'rm -r' for directories\n", 0x0C);
	}
}

/* ========================================================================== */
/* COMMAND DISPATCHER                                                         */
/* ========================================================================== */

static void execute_command(const char *cmd)
{
	const char *arg;

	/* Skip leading whitespace */
	cmd = skip_whitespace(cmd);
	
	/* Empty command */
	if (*cmd == '\0')
		return;

	/* Built-in commands */
	if (str_starts_with(cmd, "help"))
	{
		cmd_help();
	}
	else if (str_starts_with(cmd, "clear"))
	{
		cmd_clear();
	}
	else if ((arg = get_arg(cmd, "ls")))
	{
		cmd_ls(arg);
	}
	else if ((arg = get_arg(cmd, "cat")))
	{
		cmd_cat(arg);
	}
	else if ((arg = get_arg(cmd, "mkdir")))
	{
		cmd_mkdir(arg);
	}
	else if ((arg = get_arg(cmd, "rmdir")))
	{
		cmd_rmdir(arg);
	}
	else if ((arg = get_arg(cmd, "cd")))
	{
		cmd_cd(arg);
	}
	else if (str_starts_with(cmd, "pwd"))
	{
		cmd_pwd();
	}
	else if ((arg = get_arg(cmd, "rm")))
	{
		cmd_rm(arg);
	}
	else if (str_starts_with(cmd, "ps"))
	{
		cmd_ps();
	}
	else if ((arg = get_arg(cmd, "echo")))
	{
		cmd_echo(arg);
	}
	else if ((arg = get_arg(cmd, "exec")))
	{
		cmd_exec(arg);
	}
	else if (str_starts_with(cmd, "exit"))
	{
		cmd_exit();
	}
	else
	{
		vga_print("Unknown command: ", 0x0C);
		vga_print(cmd, 0x0C);
		vga_print("\nType 'help' for available commands\n", 0x0C);
	}
}

/* ========================================================================== */
/* SHELL MAIN LOOP                                                            */
/* ========================================================================== */

void shell_entry(void)
{
	char input[256];
	int input_pos = 0;

	vga_clear();
	vga_print("IR0 Shell v0.1\n", 0x0B);
	vga_print("Type 'help' for available commands\n\n", 0x07);

	while (1)
	{
		/* Print prompt */
		vga_print("$ ", 0x0A);

		/* Read input */
		input_pos = 0;
		while (1)
		{
			char c;
			int64_t n = syscall(SYS_READ, 0, (uint64_t)&c, 1);
			
			if (n <= 0)
				continue;

			if (c == '\n')
			{
				vga_putchar('\n', 0x0F);
				input[input_pos] = '\0';
				break;
			}
			else if (c == '\b' || c == 127)
			{
				if (input_pos > 0)
				{
					input_pos--;
					vga_putchar('\b', 0x0F);
				}
			}
			else if (c >= 32 && c < 127 && input_pos < 255)
			{
				input[input_pos++] = c;
				vga_putchar(c, 0x0F);
			}
		}

		/* Execute command */
		execute_command(input);
	}
}
