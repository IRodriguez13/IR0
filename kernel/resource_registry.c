
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
#define MAX_MMIO_ENTRIES   16

struct irq_entry {
    uint8_t irq;
    const char *name;
};

struct ioport_entry {
    uint16_t start;
    uint16_t end;
    const char *name;
};

struct mmio_entry {
    uint64_t start;
    uint64_t end;
    const char *name;
};

static struct irq_entry s_irq_list[MAX_IRQ_ENTRIES];
static int s_irq_count;

static struct ioport_entry s_ioport_list[MAX_IOPORT_ENTRIES];
static int s_ioport_count;

static struct mmio_entry s_mmio_list[MAX_MMIO_ENTRIES];
static int s_mmio_count;

void resource_register_irq(uint8_t irq, const char *name)
{
    if (!name || s_irq_count >= MAX_IRQ_ENTRIES)
        return;
    s_irq_list[s_irq_count].irq = irq;
    s_irq_list[s_irq_count].name = name;
    s_irq_count++;
}

static int resource_name_eq(const char *a, const char *b)
{
    return a && b && strcmp(a, b) == 0;
}

int resource_unregister_irq(uint8_t irq, const char *name)
{
    int i;

    if (!name)
        return -ENOENT;
    for (i = 0; i < s_irq_count; i++)
    {
        if (s_irq_list[i].irq != irq ||
            !resource_name_eq(s_irq_list[i].name, name))
            continue;
        for (int j = i; j + 1 < s_irq_count; j++)
            s_irq_list[j] = s_irq_list[j + 1];
        s_irq_count--;
        return 0;
    }
    return -ENOENT;
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

int resource_unregister_ioport(uint16_t start, uint16_t end, const char *name)
{
    int i;

    if (!name)
        return -ENOENT;
    for (i = 0; i < s_ioport_count; i++)
    {
        if (s_ioport_list[i].start != start ||
            s_ioport_list[i].end != end ||
            !resource_name_eq(s_ioport_list[i].name, name))
            continue;
        for (int j = i; j + 1 < s_ioport_count; j++)
            s_ioport_list[j] = s_ioport_list[j + 1];
        s_ioport_count--;
        return 0;
    }
    return -ENOENT;
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

void resource_register_mmio(uint64_t start, uint64_t end, const char *name)
{
    if (!name || s_mmio_count >= MAX_MMIO_ENTRIES)
        return;
    s_mmio_list[s_mmio_count].start = start;
    s_mmio_list[s_mmio_count].end = end;
    s_mmio_list[s_mmio_count].name = name;
    s_mmio_count++;
}

int resource_unregister_mmio(uint64_t start, uint64_t end, const char *name)
{
    int i;

    if (!name)
        return -ENOENT;
    for (i = 0; i < s_mmio_count; i++)
    {
        if (s_mmio_list[i].start != start ||
            s_mmio_list[i].end != end ||
            !resource_name_eq(s_mmio_list[i].name, name))
            continue;
        for (int j = i; j + 1 < s_mmio_count; j++)
            s_mmio_list[j] = s_mmio_list[j + 1];
        s_mmio_count--;
        return 0;
    }
    return -ENOENT;
}

void resource_foreach_mmio(int (*cb)(uint64_t start, uint64_t end, const char *name, void *ctx), void *ctx)
{
    if (!cb)
        return;
    for (int i = 0; i < s_mmio_count; i++)
    {
        if (cb(s_mmio_list[i].start, s_mmio_list[i].end, s_mmio_list[i].name, ctx) != 0)
            break;
    }
}
