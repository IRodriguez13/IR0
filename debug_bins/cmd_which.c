/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel - which command (debug bin)
 * Copyright (C) 2026 Iván Rodriguez
 *
 * Locate debug shell commands in the builtin and debug_bins registries.
 */

#include "debug_bins.h"

extern struct debug_command *debug_commands[];

static int cmd_which_handler(int argc, char **argv)
{
    int found = 0;
    const char *builtins[] = { "help", "clear", "exit", NULL };

    if (argc < 2)
    {
        debug_writeln_err("usage: which COMMAND...");
        return 1;
    }

    for (int a = 1; a < argc; a++)
    {
        const char *name = argv[a];
        int matched = 0;

        for (int i = 0; builtins[i] != NULL; i++)
        {
            if (strcmp(name, builtins[i]) == 0)
            {
                debug_write("builtin ");
                debug_writeln(name);
                matched = 1;
                found++;
                break;
            }
        }

        if (matched)
            continue;

        for (int i = 0; debug_commands[i] != NULL; i++)
        {
            if (strcmp(name, debug_commands[i]->name) == 0)
            {
                debug_write("debug_bins ");
                debug_writeln(name);
                matched = 1;
                found++;
                break;
            }
        }

        if (!matched)
        {
            debug_write_err("which: not found: ");
            debug_writeln_err(name);
        }
    }

    return (found > 0) ? 0 : 1;
}

struct debug_command cmd_which = {
    .name = "which",
    .handler = cmd_which_handler,
    .usage = "which COMMAND...",
    .description = "Locate builtin/debug_bins commands",
    .flags = NULL
};

