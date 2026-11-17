/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: hpet.c
 * Description: IR0 kernel source/header file
 */

#include "hpet.h"
#include <ir0/oops.h>
#include <drivers/timer/pit/pit.h> // para outb, inb 
#include <vga.h>
#include <string.h>

#define HPET_GENERAL_CAPABILITY       0x00
#define HPET_GENERAL_CONFIGURATION    0x10
#define HPET_MAIN_COUNTER             0xF0

#define HPET_TIMER0_CONFIG            0x100
#define HPET_TIMER0_COMPARATOR        0x108
#define HPET_TIMER0_FSB_ROUTE         0x110 // para MSI (a futuro)

#define HPET_ENABLE_CNF               (1 << 0) // Habilita el contador general
#define HPET_LEGACY_REPLACEMENT       (1 << 1) // Usa IRQ0 para HPET (reemplaza PIT)
#define HPET_TN_INT_ENB_CNF           (1 << 2) // Habilita interrupciones en el timer
#define HPET_TN_TYPE_CNF              (1 << 3) // 1 = periódica, 0 = una sola vez
#define HPET_TN_VAL_SET_CNF           (1 << 6) // Usar valor inmediato en comparador
#define HPET_TN_32MODE_CNF            (1 << 8) // Usar modo 32-bit si querés

volatile uint64_t* hpet_base = 0; // Puntero base mapeado a la tabla HPET

