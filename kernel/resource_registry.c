
/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Resource registry implementation
 * Copyright (C) 2025  Iván Rodriguez
 *
 * Data comes only from driver registration; no hardcoded tables here.
 */

#include "resource_registry.h"
#include <string.h>

#define MAX_IRQ_ENTRIES    16
#define MAX_IOPORT_ENTRIES 32

struct irq_entry {
    uint8_t irq;
    const char *name;
};

struct ioport_entry {
    uint16_t start;
    uint16_t end;
    const char *name;
};

static struct irq_entry s_irq_list[MAX_IRQ_ENTRIES];
static int s_irq_count;

static struct ioport_entry s_ioport_list[MAX_IOPORT_ENTRIES];
static int s_ioport_count;

void resource_register_irq(uint8_t irq, const char *name)
{
    if (!name || s_irq_count >= MAX_IRQ_ENTRIES)
        return;
    s_irq_list[s_irq_count].irq = irq;
    s_irq_list[s_irq_count].name = name;
    s_irq_count++;
}

void resource_register_ioport(uint16_t start, uint16_t end, const char *name)
{
    if (!name || s_ioport_count >= MAX_IOPORT_ENTRIES)
        return;
    s_ioport_list[s_ioport_count].start = start;
    s_ioport_list[s_ioport_count].end = end;
    s_ioport_list[s_ioport_count].name = name;
    s_ioport_count++;
}

void resource_foreach_irq(int (*cb)(uint8_t irq, const char *name, void *ctx), void *ctx)
{
    if (!cb)
        return;
    for (int i = 0; i < s_irq_count; i++)
    {
        if (cb(s_irq_list[i].irq, s_irq_list[i].name, ctx) != 0)
            break;
    }
}

void resource_foreach_ioport(int (*cb)(uint16_t start, uint16_t end, const char *name, void *ctx), void *ctx)
{
    if (!cb)
        return;
    for (int i = 0; i < s_ioport_count; i++)
    {
        if (cb(s_ioport_list[i].start, s_ioport_list[i].end, s_ioport_list[i].name, ctx) != 0)
            break;
    }
}
