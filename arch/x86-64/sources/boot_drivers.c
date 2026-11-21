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
#include <ir0/idle.h>
#include <ir0/input_backend.h>
#include <ir0/resource_registry.h>
#include <config.h>
#include <drivers/input/ps2.h>
#include <drivers/storage/ahci.h>
#include <drivers/storage/nvme.h>
#include <drivers/virtio/virtio_9p.h>
#include <interrupt/keyboard.h>

#if CONFIG_ENABLE_MOUSE
#include <drivers/input/ps2_mouse.h>
#endif

#if CONFIG_ENABLE_PC_SPEAKER
#include <drivers/char/pc_speaker.h>
#endif

#if CONFIG_ENABLE_STORAGE_ATA
#include <drivers/storage/ata.h>
#endif

#if CONFIG_ENABLE_SOUND
#include <sound/sound_blaster.h>
#include <sound/adlib.h>
#include <drivers/dma/dma.h>
#endif

#if CONFIG_ENABLE_BLUETOOTH
#include <ir0/bluetooth.h>
#endif

#if CONFIG_ENABLE_USB_HOST
#include <ir0/usb_host.h>
#endif

static int boot_init_ps2_controller(void)
{
	ps2_init();
	return 0;
}

static int boot_init_keyboard(void)
{
	keyboard_init();
	/* Drain bytes delivered without IRQ1. */
	kernel_idle_poll_register(input_kbd_poll_ps2);
	return 0;
}

static int boot_init_mouse(void)
{
#if CONFIG_ENABLE_MOUSE && CONFIG_INIT_MOUSE_DRIVER
	ps2_mouse_init();
#endif
	return 0;
}

static int boot_init_pc_speaker(void)
{
#if CONFIG_ENABLE_PC_SPEAKER && CONFIG_INIT_PC_SPEAKER
    pc_speaker_init();
#endif
    return 0;
}

static int boot_init_sound(void)
{
#if CONFIG_ENABLE_SOUND && CONFIG_INIT_SOUND_DRIVERS
    sb16_init();
    adlib_init();
    resource_register_ioport(DMA1_PORT_START, DMA1_PORT_END, "dma1");
#endif
    return 0;
}

static int boot_init_storage_ata(void)
{
#if CONFIG_ENABLE_STORAGE_ATA && CONFIG_INIT_STORAGE_ATA
    ata_init();
#endif
    return 0;
}

static int boot_init_storage_block(void)
{
#if CONFIG_ENABLE_STORAGE_ATA_BLOCK && CONFIG_INIT_STORAGE_ATA_BLOCK
    ata_block_register();
#endif
    ahci_probe();
    nvme_probe();
    return 0;
}

static int boot_init_bluetooth(void)
{
#if CONFIG_ENABLE_BLUETOOTH && CONFIG_INIT_BLUETOOTH_DRIVER
    return ir0_bluetooth_register_driver();
#else
    return 0;
#endif
}

static int boot_init_usb_host(void)
{
#if CONFIG_ENABLE_USB_HOST && CONFIG_INIT_USB_HOST
    return ir0_usb_host_init();
#else
    return 0;
#endif
}

static int boot_init_hostshare_9p(void)
{
	/* Optional: absent without QEMU -virtfs; non-fatal for normal boots. */
	(void)virtio_9p_pci_probe();
	return 0;
}

/*
 * Same order as before the split: within each stage the bootstrap runs
 * entries in registration order, after the generic network stack.
 */
void ir0_boot_arch_drivers(void)
{
	const int ps2_boot = CONFIG_INIT_PS2_CONTROLLER;
	const int ps2_mouse_boot = (CONFIG_ENABLE_MOUSE && CONFIG_INIT_MOUSE_DRIVER);

	i8042_irq_init();
	driver_bootstrap_register(DRIVER_BOOT_STAGE_INPUT, "ps2_controller",
				  boot_init_ps2_controller, ps2_boot);
	driver_bootstrap_register(DRIVER_BOOT_STAGE_INPUT, "ps2_keyboard",
				  boot_init_keyboard, ps2_boot);
	driver_bootstrap_register(DRIVER_BOOT_STAGE_INPUT, "ps2_mouse",
				  boot_init_mouse, ps2_mouse_boot);
	driver_bootstrap_register(DRIVER_BOOT_STAGE_PLATFORM, "pc_speaker",
				  boot_init_pc_speaker,
				  (CONFIG_ENABLE_PC_SPEAKER && CONFIG_INIT_PC_SPEAKER));
	driver_bootstrap_register(DRIVER_BOOT_STAGE_PLATFORM, "hostshare_9p",
				  boot_init_hostshare_9p, 1);
	driver_bootstrap_register(DRIVER_BOOT_STAGE_STORAGE, "ata_core",
				  boot_init_storage_ata,
				  (CONFIG_ENABLE_STORAGE_ATA && CONFIG_INIT_STORAGE_ATA));
	driver_bootstrap_register(DRIVER_BOOT_STAGE_STORAGE, "ata_block",
				  boot_init_storage_block,
				  (CONFIG_ENABLE_STORAGE_ATA_BLOCK && CONFIG_INIT_STORAGE_ATA_BLOCK));
	driver_bootstrap_register(DRIVER_BOOT_STAGE_AUDIO, "sound_stack",
				  boot_init_sound,
				  (CONFIG_ENABLE_SOUND && CONFIG_INIT_SOUND_DRIVERS));
	driver_bootstrap_register(DRIVER_BOOT_STAGE_NETWORK, "bluetooth_stack",
				  boot_init_bluetooth,
				  (CONFIG_ENABLE_BLUETOOTH && CONFIG_INIT_BLUETOOTH_DRIVER));
	driver_bootstrap_register(DRIVER_BOOT_STAGE_PLATFORM, "usb_host",
				  boot_init_usb_host,
				  (CONFIG_ENABLE_USB_HOST && CONFIG_INIT_USB_HOST));
}
