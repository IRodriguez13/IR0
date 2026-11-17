/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: boot_drivers.c
 * Description: PC platform devices registered with the driver bootstrap.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ir0/boot_arch.h>
#include <ir0/driver_bootstrap.h>
#include <config.h>
#include <drivers/input/ps2.h>
#include <interrupt/keyboard.h>

#if CONFIG_ENABLE_MOUSE
#include <drivers/input/ps2_mouse.h>
#endif

static int boot_init_ps2_controller(void)
{
	ps2_init();
	return 0;
}

static int boot_init_keyboard(void)
{
	keyboard_init();
	return 0;
}

static int boot_init_mouse(void)
{
#if CONFIG_ENABLE_MOUSE && CONFIG_INIT_MOUSE_DRIVER
	ps2_mouse_init();
#endif
	return 0;
}

void ir0_boot_arch_drivers(void)
{
	const int ps2_boot = CONFIG_INIT_PS2_CONTROLLER;
	const int ps2_mouse_boot = (CONFIG_ENABLE_MOUSE && CONFIG_INIT_MOUSE_DRIVER);

	driver_bootstrap_register(DRIVER_BOOT_STAGE_INPUT, "ps2_controller",
				  boot_init_ps2_controller, ps2_boot);
	driver_bootstrap_register(DRIVER_BOOT_STAGE_INPUT, "ps2_keyboard",
				  boot_init_keyboard, ps2_boot);
	driver_bootstrap_register(DRIVER_BOOT_STAGE_INPUT, "ps2_mouse",
				  boot_init_mouse, ps2_mouse_boot);
}
