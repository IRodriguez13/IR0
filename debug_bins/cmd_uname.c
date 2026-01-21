/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel - Debug Binary: uname
 * Copyright (C) 2026 Iván Rodriguez
 *
 * Print system information command (uses only syscalls)
 */

#include "debug_bins.h"
#include <ir0/version.h>
#include <string.h>

static int cmd_uname_handler(int argc, char **argv)
{
    const char *option = (argc > 1) ? argv[1] : "-a";
    
    if (strcmp(option, "-a") == 0 || strcmp(option, "--all") == 0 || option[0] == '\0')
    {
        /* Show all information */
        const char *arch = "x86_64";
#ifdef __i386__
        arch = "i386";
#elif defined(__aarch64__)
        arch = "aarch64";
#elif defined(__arm__)
        arch = "arm";
#endif
        
        char buffer[512];
        int len = snprintf(buffer, sizeof(buffer),
                          "IR0 %s %s #%s SMP %s %s %s\n",
                          IR0_BUILD_HOST,
                          IR0_VERSION_STRING,
                          IR0_BUILD_NUMBER,
                          IR0_BUILD_DATE,
                          IR0_BUILD_TIME,
                          arch);
        
        if (len > 0 && len < (int)sizeof(buffer))
        {
            debug_write(buffer);
        }
    }
    else if (strcmp(option, "-s") == 0 || strcmp(option, "--kernel-name") == 0)
    {
        debug_writeln("IR0");
    }
    else if (strcmp(option, "-r") == 0 || strcmp(option, "--kernel-release") == 0)
    {
        char buffer[64];
        snprintf(buffer, sizeof(buffer), "%s\n", IR0_VERSION_STRING);
        debug_write(buffer);
    }
    else if (strcmp(option, "-v") == 0 || strcmp(option, "--kernel-version") == 0)
    {
        char buffer[256];
        snprintf(buffer, sizeof(buffer), "#%s SMP %s %s by %s@%s\n",
                IR0_BUILD_NUMBER,
                IR0_BUILD_DATE,
                IR0_BUILD_TIME,
                IR0_BUILD_USER,
                IR0_BUILD_HOST);
        debug_write(buffer);
    }
    else if (strcmp(option, "-m") == 0 || strcmp(option, "--machine") == 0 ||
             strcmp(option, "-p") == 0 || strcmp(option, "--processor") == 0 ||
             strcmp(option, "-i") == 0 || strcmp(option, "--hardware-platform") == 0)
    {
        const char *arch = "x86_64";
#ifdef __i386__
        arch = "i386";
#elif defined(__aarch64__)
        arch = "aarch64";
#elif defined(__arm__)
        arch = "arm";
#endif
        debug_writeln(arch);
    }
    else if (strcmp(option, "--help") == 0 || strcmp(option, "-h") == 0)
    {
        debug_writeln("Usage: uname [OPTION]...");
        debug_writeln("Print system information");
        debug_writeln("");
        debug_writeln("Options:");
        debug_writeln("  -a, --all           Print all information");
        debug_writeln("  -s, --kernel-name   Print kernel name");
        debug_writeln("  -r, --kernel-release Print kernel release");
        debug_writeln("  -v, --kernel-version Print kernel version");
        debug_writeln("  -m, --machine       Print machine hardware name");
        debug_writeln("  -p, --processor     Print processor type");
        debug_writeln("  -i, --hardware-platform Print hardware platform");
        debug_writeln("  -h, --help          Print this help");
    }
    else
    {
        debug_write_err("uname: invalid option: ");
        debug_write_err(option);
        debug_write_err("\n");
        debug_write_err("Try 'uname --help' for more information\n");
        return 1;
    }
    
    return 0;
}

struct debug_command cmd_uname = {
    .name = "uname",
    .handler = cmd_uname_handler,
    .usage = "uname [-a|-s|-r|-v|-m|-p|-i]",
    .description = "Print system information"
};

