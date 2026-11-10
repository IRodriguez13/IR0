/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * File: devfs_block.c
 * Description: /dev/disk whole-disk and partition device handlers.
 */

#include "devfs_internal.h"


/*
 * Block I/O is 64-bit LBA through ir0_block_*. Device/partition size is the
 * only bound; do not clamp to UINT32_MAX (that rejected large swap images).
 */

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

struct devfs_disk_io {
    const char *name;
    uint64_t lba;
    size_t bytes;
    size_t in_sector;
};

/*
 * Resolve a byte range against the whole disk or partition extent, clamp it
 * to the 32-bit backend LBA window, and keep the intra-sector offset.
 */
static int devfs_disk_prepare_io(const devfs_entry_t *entry, off_t offset,
                                 size_t count, struct devfs_disk_io *io)
{
    uint8_t disk_id;
    partition_info_t pinfo;
    int is_whole = 0;
    uint64_t sector_off;
    uint64_t available_sectors;
    size_t sectors;

    if (!io)
        return -EINVAL;

    io->name = NULL;
    io->lba = 0;
    io->bytes = 0;
    io->in_sector = 0;

    if (offset < 0)
        return -EINVAL;
    if (devfs_resolve_disk_geo(entry, &disk_id, &pinfo, &is_whole) != 0)
        return -ENODEV;

    io->name = ir0_block_legacy_name(disk_id);
    if (!io->name || !ir0_block_name_is_present(io->name))
        return -ENODEV;

    sector_off = (uint64_t)offset / 512;
    io->in_sector = (size_t)((uint64_t)offset % 512);

    if (!is_whole)
    {
        if (sector_off >= pinfo.total_sectors)
            return 0;
        if (pinfo.start_lba > UINT64_MAX - sector_off)
            return -EIO;
        io->lba = pinfo.start_lba + sector_off;
        available_sectors = pinfo.total_sectors - sector_off;
    }
    else
    {
        uint64_t disk_sectors = ir0_block_sector_count_by_name(io->name);

        if (sector_off >= disk_sectors)
            return 0;
        io->lba = sector_off;
        available_sectors = disk_sectors - sector_off;
    }

    if (available_sectors > (uint64_t)(SIZE_MAX / 512))
        available_sectors = (uint64_t)(SIZE_MAX / 512);
    sectors = (size_t)available_sectors;
    if (sectors == 0)
        return 0;
    if (count > sectors * 512)
        count = sectors * 512;
    io->bytes = count;
    return 0;
}

static int devfs_disk_read_interrupted(void)
{
    if (current_process && signal_pending(current_process))
    {
        return 1;
    }
    return 0;
}

int64_t dev_disk_read(devfs_entry_t *entry, void *buf, size_t count, off_t offset)
{
    if (!buf)
        return -1;
    if (entry->device_id != DEVFS_DISK_AGGREGATE_ID)
    {
        struct devfs_disk_io io;
        uint8_t bounce[512];
        char *dst = (char *)buf;
        size_t done = 0;
        int rc;

        rc = devfs_disk_prepare_io(entry, offset, count, &io);
        if (rc != 0)
            return rc;
        if (io.bytes == 0)
            return 0;

        /* A non-zero offset starts inside the first sector. */
        if (io.in_sector != 0)
        {
            size_t chunk = 512 - io.in_sector;

            if (chunk > io.bytes)
                chunk = io.bytes;
            rc = ir0_block_read_by_name(io.name, io.lba, 1, bounce);
            if (rc != 0)
                return done ? (int64_t)done : rc;
            memcpy(dst, bounce + io.in_sector, chunk);
            done += chunk;
            io.lba++;
        }

        while (io.bytes - done >= 512)
        {
            size_t remaining = io.bytes - done;
            size_t sectors = remaining / 512;
            uint8_t n;

            /* cat /dev/hda must be interruptible (Ctrl+C → EINTR). */
            if (devfs_disk_read_interrupted())
                return done ? (int64_t)done : -EINTR;

            if (sectors > 255)
                sectors = 255;
            n = (uint8_t)sectors;
            rc = ir0_block_read_by_name(io.name, io.lba, n, dst + done);
            if (rc != 0)
                return done ? (int64_t)done : rc;
            done += (size_t)n * 512;
            io.lba += n;
        }

        /* A short tail must not read past the caller's buffer. */
        if (done < io.bytes)
        {
            size_t tail = io.bytes - done;

            rc = ir0_block_read_by_name(io.name, io.lba, 1, bounce);
            if (rc != 0)
                return done ? (int64_t)done : rc;
            memcpy(dst + done, bounce, tail);
            done += tail;
        }
        return (int64_t)done;
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

    {
        struct devfs_disk_io io;
        uint8_t bounce[512];
        const char *src = (const char *)buf;
        size_t done = 0;
        int rc;

        rc = devfs_disk_prepare_io(entry, offset, count, &io);
        if (rc != 0)
            return rc;
        if (io.bytes == 0)
            return 0;

        /* A non-zero offset starts inside the first sector: read-modify-write. */
        if (io.in_sector != 0)
        {
            size_t chunk = 512 - io.in_sector;

            if (chunk > io.bytes)
                chunk = io.bytes;
            rc = ir0_block_read_by_name(io.name, io.lba, 1, bounce);
            if (rc != 0)
                return done ? (int64_t)done : rc;
            memcpy(bounce + io.in_sector, src, chunk);
            rc = ir0_block_write_by_name(io.name, io.lba, 1, bounce);
            if (rc != 0)
                return done ? (int64_t)done : rc;
            done += chunk;
            io.lba++;
        }

        while (io.bytes - done >= 512)
        {
            size_t remaining = io.bytes - done;
            size_t sectors = remaining / 512;
            uint8_t n;

            if (sectors > 255)
                sectors = 255;
            n = (uint8_t)sectors;
            rc = ir0_block_write_by_name(io.name, io.lba, n, src + done);
            if (rc != 0)
                return done ? (int64_t)done : rc;
            done += (size_t)n * 512;
            io.lba += n;
        }

        /* A short tail must not overwrite the rest of the sector. */
        if (done < io.bytes)
        {
            size_t tail = io.bytes - done;

            rc = ir0_block_read_by_name(io.name, io.lba, 1, bounce);
            if (rc != 0)
                return done ? (int64_t)done : rc;
            memcpy(bounce, src + done, tail);
            rc = ir0_block_write_by_name(io.name, io.lba, 1, bounce);
            if (rc != 0)
                return done ? (int64_t)done : rc;
            done += tail;
        }
        return (int64_t)done;
    }
}

/*
 * Legacy DISK_* ioctls take a 32-bit sector number relative to the opened
 * node.  A partition node must reject anything past its extent, and the
 * absolute LBA must stay inside the 32-bit window that legacy backends
 * (notably ATA) truncate to.
 */
static int devfs_disk_ioctl_lba(uint64_t start_lba, uint64_t total_sectors,
                                uint32_t req_lba, uint32_t *out_lba)
{
    uint64_t lba;

    if (!out_lba)
        return -1;
    if ((uint64_t)req_lba >= total_sectors)
        return -1;
    lba = start_lba + (uint64_t)req_lba;
    if (lba > UINT32_MAX)
        return -1;
    *out_lba = (uint32_t)lba;
    return 0;
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
        /*
         * Fail closed: an unreadable partition entry must not fall back to
         * the caller-supplied drive and reach the whole disk instead.
         */
        if (devfs_resolve_disk_geo(entry, &disk_id, &pinfo, &is_whole) != 0)
            return -1;
        if (!is_whole)
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
                    uint32_t lba = req->lba;
                    if (have_disk_ctx && !is_whole &&
                        devfs_disk_ioctl_lba(part_start_lba, part_sectors,
                                             req->lba, &lba) != 0)
                        return -1;
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
                    uint32_t lba = req->lba;
                    if (have_disk_ctx && !is_whole &&
                        devfs_disk_ioctl_lba(part_start_lba, part_sectors,
                                             req->lba, &lba) != 0)
                        return -1;
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
                    uint64_t bytes = sectors > UINT64_MAX / 512u
                                         ? UINT64_MAX
                                         : sectors * 512u;
                    if (geom->size_sectors)
                        *geom->size_sectors = sectors;
                    if (geom->size_bytes)
                        *geom->size_bytes = bytes;
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

            char *nm = kmalloc(28);
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
