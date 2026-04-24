/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026 Iván Rodriguez
 *
 * File: cmd_true.c
 * Description: Debug binary: true
 */

#include "debug_bins.h"

static int cmd_true_handler(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    debug_serial_ok("true");
    return 0;
}

struct debug_command cmd_true = {
    .name = "true",
    .handler = cmd_true_handler,
    .usage = "true",
    .description = "Return success"
};
