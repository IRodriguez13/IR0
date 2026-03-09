/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Implementación de la capa de callbacks para drivers
 * Copyright (C) 2025  Iván Rodriguez
 *
 * Registro central de callbacks de inicialización. El kernel invoca
 * driver_layer_run_all_inits() sin conocer drivers concretos.
 */

#include "driver_layer.h"
#include <drivers/serial/serial.h>
#include <ir0/logging.h>
#include <string.h>

struct driver_init_entry {
	const char *name;
	driver_init_cb_t init_fn;
};

static struct driver_init_entry s_inits[DRIVER_LAYER_MAX_INITS];
static int s_count;

int driver_layer_register_init(const char *name, driver_init_cb_t init_fn)
{
	if (!name || !init_fn || s_count >= DRIVER_LAYER_MAX_INITS)
		return -1;
	s_inits[s_count].name = name;
	s_inits[s_count].init_fn = init_fn;
	s_count++;
	return 0;
}

void driver_layer_run_all_inits(void)
{
	serial_print("[DRIVERS] Running registered driver inits...\n");
	for (int i = 0; i < s_count; i++) {
		s_inits[i].init_fn();
	}
	serial_print("[DRIVERS] All driver inits completed\n");
}
