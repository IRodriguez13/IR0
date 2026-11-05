/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * File: devfs_block.c
 * Description: /dev/disk whole-disk and partition device handlers.
 */

#include "devfs_internal.h"


typedef struct devfs_disk_ctx {
    uint8_t disk_id;
    uint8_t is_whole;
    uint8_t partition_number;
} devfs_disk_ctx_t;

/*
 * Resolve whole-disk vs partition extents for disk_ops (/dev/hd*).
 */
static int devfs_resolve_disk_geo(const devfs_entry_t *entry, uint8_t *disk_out,
				  partition_info_t *part_out, int *whole_out)
{
    if (!entry || entry->device_id == DEVFS_DISK_AGGREGATE_ID)
        return -ENODEV;

    devfs_disk_ctx_t *cx = entry->driver_data;
    if (!cx)
        return -ENODEV;

    *disk_out = cx->disk_id;

    if (cx->is_whole)
    {
        *whole_out = 1;
        return 0;
    }
    if (!part_out || !whole_out)
        return -EINVAL;

    if (get_partition_info(cx->disk_id, cx->partition_number, part_out) != 0)
        return -ENODEV;
    *whole_out = 0;
    return 0;
}

int64_t dev_disk_read(devfs_entry_t *entry, void *buf, size_t count, off_t offset)
{
    if (!buf)
        return -1;
    if (entry->device_id != DEVFS_DISK_AGGREGATE_ID)
    {
        uint8_t disk_id;
        partition_info_t pinfo;
        int is_whole = 0;

        if (devfs_resolve_disk_geo(entry, &disk_id, &pinfo, &is_whole) != 0)
            return -ENODEV;
        const char *disk_name = ir0_block_legacy_name(disk_id);
        if (!disk_name || !ir0_block_name_is_present(disk_name))
            return -ENODEV;
        if (offset < 0)
            return -1;
        uint64_t sector_off = (uint64_t)((unsigned long)offset / 512);
        size_t num_sectors = (count + 511) / 512;
        if (num_sectors == 0)
            return 0;
        uint64_t start_lba = sector_off;
        if (!is_whole)
        {
            if (sector_off >= pinfo.total_sectors)
                return 0;
            start_lba = pinfo.start_lba + sector_off;
            if (num_sectors > pinfo.total_sectors - sector_off)
                num_sectors = (size_t)(pinfo.total_sectors - sector_off);
        }
        else
        {
            uint64_t disk_sectors = ir0_block_sector_count_by_name(disk_name);
            if (sector_off >= disk_sectors)
                return 0;
            if (sector_off + num_sectors > disk_sectors)
                num_sectors = (size_t)(disk_sectors - sector_off);
        }
        char *dst = (char *)buf;
        size_t bytes_done = 0;
        while (num_sectors > 0)
        {
            uint8_t n = (num_sectors > 255) ? 255 : (uint8_t)num_sectors;

            /* cat /dev/hda must be interruptible (Ctrl+C → EINTR). */
            if (current_process &&
                signals_pause_should_interrupt(current_process))
            {
                handle_signals();
                if (bytes_done > 0)
                    return (int64_t)bytes_done;
                return -EINTR;
            }
            if (ir0_block_read_by_name(disk_name, (uint32_t)start_lba, n, dst))
                return (int64_t)bytes_done;
            bytes_done += (size_t)n * 512;
            start_lba += n;
            num_sectors -= n;
            dst += (size_t)n * 512;
        }
        return (int64_t)((bytes_done < count) ? bytes_done : count);
    }
    /* Aggregate /dev/disk: generate listing */

    /* Generate df-like output: Filesystem          Size */
    /* Support offset for seeking within output */
    char output[1024];
    size_t output_len = 0;

    /* Validate offset */
    if (offset < 0)
        return -1;

    int n = snprintf(output + output_len, sizeof(output) - output_len,
                     "Filesystem          Size\n");
    if (n > 0 && (size_t)n < sizeof(output) - output_len)
        output_len += (size_t)n;

    n = snprintf(output + output_len, sizeof(output) - output_len,
                 "----------------------------------\n");
    if (n > 0 && (size_t)n < sizeof(output) - output_len)
        output_len += (size_t)n;

    int found_drives = 0;
    int total_devs = ir0_block_count();
    for (int i = 0; i < total_devs; i++)
    {
        const char *disk_name = ir0_block_name_at(i);
        if (!disk_name || !ir0_block_name_is_present(disk_name))
            continue;

        found_drives++;
        char devname[16];
        int len = snprintf(devname, sizeof(devname), "/dev/%s", disk_name);
        if (len < 0 || len >= (int)sizeof(devname))
            continue;

        /* ir0_block_sector_count_by_name() returns size in 512-byte sectors */
        uint64_t size = ir0_block_sector_count_by_name(disk_name);
        if (size == 0)
        {
            n = snprintf(output + output_len, sizeof(output) - output_len,
                        "%-20s (empty)\n", devname);
        }
        else
        {
            char size_str[32];
            /* Same calculation: sectors / (2 * 1024 * 1024) = GB */
            uint64_t size_gb = size / (2 * 1024 * 1024);
            if (size_gb > 0)
            {
                /* Convert uint64_t to string manually since snprintf doesn't support %llu */
                char num_str[32];
                char *p = num_str;
                uint64_t tmp = size_gb;
                if (tmp == 0)
                {
                    *p++ = '0';
                }
                else
                {
                    char rev[32];
                    int idx = 0;
                    while (tmp > 0)
                    {
                        rev[idx++] = '0' + (tmp % 10);
                        tmp /= 10;
                    }
                    while (idx > 0)
                        *p++ = rev[--idx];
                }
                *p = '\0';
                len = snprintf(size_str, sizeof(size_str), "%sG", num_str);
            }
            else
            {
                /* sectors / (2 * 1024) = MB */
                uint64_t size_mb = size / (2 * 1024);
                char num_str[32];
                char *p = num_str;
                uint64_t tmp = size_mb;
                if (tmp == 0)
                {
                    *p++ = '0';
                }
                else
                {
                    char rev[32];
                    int idx = 0;
                    while (tmp > 0)
                    {
                        rev[idx++] = '0' + (tmp % 10);
                        tmp /= 10;
                    }
                    while (idx > 0)
                        *p++ = rev[--idx];
                }
                *p = '\0';
                len = snprintf(size_str, sizeof(size_str), "%sM", num_str);
            }

            if (len > 0 && len < (int)sizeof(size_str))
            {
                n = snprintf(output + output_len, sizeof(output) - output_len,
                            "%-20s %s\n", devname, size_str);
            }
            else
            {
                n = 0;
            }
        }

        if (n > 0 && (size_t)n < sizeof(output) - output_len)
            output_len += (size_t)n;
    }

    if (found_drives == 0)
    {
        n = snprintf(output + output_len, sizeof(output) - output_len,
                    "No drives detected\n");
        if (n > 0 && (size_t)n < sizeof(output) - output_len)
            output_len += (size_t)n;
    }

    /* Support offset: skip bytes if offset is beyond start */
    size_t start_pos = (size_t)offset;
    if (start_pos >= output_len)
    {
        /* Offset beyond end of output - return empty */
        return 0;
    }

    /* Copy to user buffer (respecting offset and count) */
    size_t available = output_len - start_pos;
    size_t copy_size = (available < count) ? available : count;
    if (copy_size > 0)
    {
        memcpy(buf, output + start_pos, copy_size);
    }

    return (int64_t)copy_size;
}

int64_t dev_disk_write(devfs_entry_t *entry, const void *buf, size_t count, off_t offset)
{
    if (entry->device_id == DEVFS_DISK_AGGREGATE_ID)
        return count;
    if (!buf)
        return -1;
    uint8_t disk_id;
    partition_info_t pinfo;
    int is_whole = 0;

    if (devfs_resolve_disk_geo(entry, &disk_id, &pinfo, &is_whole) != 0)
        return -ENODEV;
    const char *disk_name = ir0_block_legacy_name(disk_id);
    if (!disk_name || !ir0_block_name_is_present(disk_name))
        return -ENODEV;
    if (offset < 0)
        return -1;
    uint64_t sector_off = (uint64_t)((unsigned long)offset / 512);
    size_t num_sectors = (count + 511) / 512;
    if (num_sectors == 0)
        return 0;
    uint64_t start_lba = sector_off;
    if (!is_whole)
    {
        if (sector_off >= pinfo.total_sectors)
            return 0;
        start_lba = pinfo.start_lba + sector_off;
        if (num_sectors > pinfo.total_sectors - sector_off)
            num_sectors = (size_t)(pinfo.total_sectors - sector_off);
    }
    else
    {
        uint64_t disk_sectors = ir0_block_sector_count_by_name(disk_name);
        if (sector_off >= disk_sectors)
            return 0;
        if (sector_off + num_sectors > disk_sectors)
            num_sectors = (size_t)(disk_sectors - sector_off);
    }
    const char *src = (const char *)buf;
    size_t bytes_done = 0;
    while (num_sectors > 0)
    {
        uint8_t n = (num_sectors > 255) ? 255 : (uint8_t)num_sectors;
        if (ir0_block_write_by_name(disk_name, (uint32_t)start_lba, n, src))
            return (int64_t)bytes_done;
        bytes_done += (size_t)n * 512;
        start_lba += n;
        num_sectors -= n;
        src += (size_t)n * 512;
    }
    return (int64_t)((bytes_done < count) ? bytes_done : count);
}

int64_t dev_disk_ioctl(devfs_entry_t *entry, uint64_t request, void *arg)
{
    uint8_t disk_id = 0;
    partition_info_t pinfo;
    memset(&pinfo, 0, sizeof(pinfo));

    int is_whole = 0;

    uint64_t part_start_lba = 0;
    uint64_t part_sectors = 0;

    int have_disk_ctx = (entry &&
        entry->device_id != DEVFS_DISK_AGGREGATE_ID &&
        entry->driver_data != NULL);

    if (have_disk_ctx)
    {
        if (devfs_resolve_disk_geo(entry, &disk_id, &pinfo, &is_whole) != 0)
            have_disk_ctx = 0;
        else if (!is_whole)
        {
            part_start_lba = pinfo.start_lba;
            part_sectors = pinfo.total_sectors;
        }
    }

    switch (request)
    {
        case DISK_READ_SECTOR:
            if (arg)
            {
                typedef struct {
                    uint8_t drive;
                    uint32_t lba;
                    void *buffer;
                } disk_sector_req_t;
                disk_sector_req_t *req = (disk_sector_req_t *)arg;
                if (req && req->buffer)
                {
                    uint8_t d = have_disk_ctx ? disk_id : req->drive;
                    const char *disk_name = ir0_block_legacy_name(d);
                    uint32_t lba = (uint32_t)req->lba;
                    if (have_disk_ctx && !is_whole)
                        lba = (uint32_t)(part_start_lba + req->lba);
                    if (disk_name && ir0_block_name_is_present(disk_name) &&
                        ir0_block_read_by_name(disk_name, lba, 1, req->buffer) == 0)
                        return 512;
                }
            }
            return -1;

        case DISK_WRITE_SECTOR:
            if (arg)
            {
                typedef struct {
                    uint8_t drive;
                    uint32_t lba;
                    const void *buffer;
                } disk_sector_req_t;
                disk_sector_req_t *req = (disk_sector_req_t *)arg;
                if (req && req->buffer)
                {
                    uint8_t d = have_disk_ctx ? disk_id : req->drive;
                    const char *disk_name = ir0_block_legacy_name(d);
                    uint32_t lba = (uint32_t)req->lba;
                    if (have_disk_ctx && !is_whole)
                        lba = (uint32_t)(part_start_lba + req->lba);
                    if (disk_name && ir0_block_name_is_present(disk_name) &&
                        ir0_block_write_by_name(disk_name, lba, 1, req->buffer) == 0)
                        return 512;
                }
            }
            return -1;

        case DISK_GET_GEOMETRY:
            if (arg)
            {
                typedef struct {
                    uint8_t drive;
                    uint64_t *size_sectors;
                    uint64_t *size_bytes;
                } disk_geometry_t;
                disk_geometry_t *geom = (disk_geometry_t *)arg;
                if (geom)
                {
                    uint8_t d = have_disk_ctx ? disk_id : geom->drive;
                    const char *disk_name = ir0_block_legacy_name(d);
                    if (!disk_name || !ir0_block_name_is_present(disk_name))
                        return -1;
                    uint64_t sectors =
                        (have_disk_ctx && !is_whole) ? part_sectors : ir0_block_sector_count_by_name(disk_name);
                    if (geom->size_sectors)
                        *geom->size_sectors = sectors;
                    if (geom->size_bytes)
                        *geom->size_bytes = sectors * 512;
                    return 0;
                }
            }
            return -1;

        default:
            return -1;
    }
}

static devfs_disk_ctx_t disk_ctx_whole_slots[MAX_DISKS];
static devfs_node_t disk_node_whole_slots[MAX_DISKS];

#define DEV_PART_MAX_NODES 148
static devfs_disk_ctx_t disk_part_ctx[DEV_PART_MAX_NODES];
static devfs_node_t disk_part_nodes[DEV_PART_MAX_NODES];

static unsigned disk_part_ctx_used;

static const char *const disk_whole_name[MAX_DISKS] = { "hda", "hdb", "hdc", "hdd" };

void devfs_register_disk_topology(void)
{
    uint32_t next_dyn_dev = DEVFS_DISK_BASE_ID + (uint32_t)MAX_DISKS;

    disk_part_ctx_used = 0;
    memset(disk_part_ctx, 0, sizeof(disk_part_ctx));
    memset(disk_part_nodes, 0, sizeof(disk_part_nodes));

    /*
     * Present whole-disk nodes only (/dev/hda …) — stable IDs remain 20..23.
     */
    for (unsigned d = 0; d < (unsigned)MAX_DISKS; d++)
    {
        const char *dn = ir0_block_legacy_name((uint8_t)d);
        if (!dn || !ir0_block_name_is_present(dn))
            continue;

        memset(&disk_ctx_whole_slots[d], 0, sizeof(disk_ctx_whole_slots[d]));
        disk_ctx_whole_slots[d].disk_id = (uint8_t)d;
        disk_ctx_whole_slots[d].is_whole = 1;
        memset(&disk_node_whole_slots[d], 0, sizeof(disk_node_whole_slots[d]));
        disk_node_whole_slots[d].entry.name = disk_whole_name[d];
        disk_node_whole_slots[d].entry.mode = 0660;
        disk_node_whole_slots[d].entry.device_id = (uint32_t)(DEVFS_DISK_BASE_ID + d);
        disk_node_whole_slots[d].entry.driver_data = &disk_ctx_whole_slots[d];
        disk_node_whole_slots[d].ops = dev_disk.ops;
        devfs_register_node(&disk_node_whole_slots[d]);

        int pc = get_partition_count((uint8_t)d);
        if (pc <= 0)
            continue;

        for (unsigned ord = 0; ord < (unsigned)pc; ord++)
        {
            partition_info_t pi;
            if (partition_nth_on_disk((uint8_t)d, ord, &pi) != 0)
                break;
            if (disk_part_ctx_used >= DEV_PART_MAX_NODES)
                return;

            char *nm = kmalloc_try(28);
            if (!nm)
                break;
            snprintf(nm, 28u, "hd%c%u",
                     'a' + (int)d, (int)(pi.partition_number + 1u));

            unsigned ix = disk_part_ctx_used;
            memset(&disk_part_ctx[ix], 0, sizeof(disk_part_ctx[ix]));
            disk_part_ctx[ix].disk_id = (uint8_t)d;
            disk_part_ctx[ix].is_whole = 0;
            disk_part_ctx[ix].partition_number = pi.partition_number;

            memset(&disk_part_nodes[ix], 0, sizeof(disk_part_nodes[ix]));
            disk_part_nodes[ix].entry.name = nm;
            disk_part_nodes[ix].entry.mode = 0660;
            disk_part_nodes[ix].entry.device_id = next_dyn_dev++;
            disk_part_nodes[ix].entry.driver_data = &disk_part_ctx[ix];
            disk_part_nodes[ix].ops = dev_disk.ops;
            devfs_register_node(&disk_part_nodes[ix]);
            disk_part_ctx_used++;
        }
    }
}
