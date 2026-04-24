/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026 Iván Rodriguez
 *
 * File: cmd_false.c
 * Description: Debug binary: false
 */

#include "debug_bins.h"

static int cmd_false_handler(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    debug_serial_fail("false", "status=1");
    return 1;
}

struct debug_command cmd_false = {
    .name = "false",
    .handler = cmd_false_handler,
    .usage = "false",
    .description = "Return failure"
};
