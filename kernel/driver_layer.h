/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Capa de callbacks unificada para drivers
 * Copyright (C) 2025  Iván Rodriguez
 *
 * Interfaz que desacopla el kernel de los drivers concretos. Cada driver
 * se registra con sus callbacks; el kernel itera sobre ellos sin conocer
 * implementaciones específicas. Facilita testing (mocks) y escalabilidad.
 */

#ifndef IR0_KERNEL_DRIVER_LAYER_H
#define IR0_KERNEL_DRIVER_LAYER_H

#include <stdint.h>

#define DRIVER_LAYER_MAX_INITS 32

/**
 * driver_init_cb_t - Callback de inicialización de un driver
 *
 * El kernel invoca el callback sin conocer la implementación concreta.
 * Compatible con funciones void (*)(void) o int (*)(void).
 */
typedef void (*driver_init_cb_t)(void);

/**
 * driver_layer_register_init - Registra un callback de init
 * @name: Nombre del driver (para logging)
 * @init_fn: Función de inicialización
 *
 * Returns: 0 en éxito, -1 si la tabla está llena
 */
int driver_layer_register_init(const char *name, driver_init_cb_t init_fn);

/**
 * driver_layer_run_all_inits - Ejecuta todos los callbacks registrados
 *
 * Itera sobre la tabla e invoca cada init_fn.
 */
void driver_layer_run_all_inits(void);

/**
 * DRIVER_LAYER_INIT - Macro para auto-registro vía constructor
 * @name: Identificador único del driver
 * @init_fn: Función de inicialización
 *
 * Uso en cada driver:
 *   DRIVER_LAYER_INIT(ps2, ps2_init);
 */
#define DRIVER_LAYER_INIT(name, init_fn) \
	static void __attribute__((constructor)) _drv_layer_##name##_reg(void) { \
		driver_layer_register_init(#name, (driver_init_cb_t)(init_fn)); \
	}

#endif /* IR0_KERNEL_DRIVER_LAYER_H */
