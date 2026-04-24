/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: common_paging.h
 * Description: IR0 kernel source/header file
 */

#pragma once 
#include <stdint.h>

// Declaraciones comunes de paginación
// Las implementaciones específicas están en los archivos de cada arquitectura

// Esta función le dice a la CPU que empiece a paginar configurando el directorio. -- 32 bit --
void paging_set_cpu(uint32_t page_directory);

// Esta función le dice a la CPU que empiece a paginar configurando el directorio. -- 64 bit --
void paging_set_cpu_x64(uint64_t page_directory);
