/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: nvme.h
 * Description: NVMe MVP — probe, block backend (detect + read).
 *
 * Reference: https://wiki.osdev.org/NVMe
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <stdint.h>

void nvme_probe(void);
int nvme_disk_present(void);
uint64_t nvme_sector_count(void);
