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

/* Within a stage the bootstrap runs these after the generic network stack. */
static const struct driver_boot_entry pc_boot_drivers[] = {
	{ "ps2_controller", boot_init_ps2_controller, DRIVER_BOOT_STAGE_INPUT,
	  CONFIG_INIT_PS2_CONTROLLER },
	{ "ps2_keyboard", boot_init_keyboard, DRIVER_BOOT_STAGE_INPUT,
	  CONFIG_INIT_PS2_CONTROLLER },
	{ "ps2_mouse", boot_init_mouse, DRIVER_BOOT_STAGE_INPUT,
	  CONFIG_ENABLE_MOUSE && CONFIG_INIT_MOUSE_DRIVER },
	{ "pc_speaker", boot_init_pc_speaker, DRIVER_BOOT_STAGE_PLATFORM,
	  CONFIG_ENABLE_PC_SPEAKER && CONFIG_INIT_PC_SPEAKER },
	{ "hostshare_9p", boot_init_hostshare_9p, DRIVER_BOOT_STAGE_PLATFORM, 1 },
	{ "ata_core", boot_init_storage_ata, DRIVER_BOOT_STAGE_STORAGE,
	  CONFIG_ENABLE_STORAGE_ATA && CONFIG_INIT_STORAGE_ATA },
	{ "ata_block", boot_init_storage_block, DRIVER_BOOT_STAGE_STORAGE,
	  CONFIG_ENABLE_STORAGE_ATA_BLOCK && CONFIG_INIT_STORAGE_ATA_BLOCK },
	{ "sound_stack", boot_init_sound, DRIVER_BOOT_STAGE_AUDIO,
	  CONFIG_ENABLE_SOUND && CONFIG_INIT_SOUND_DRIVERS },
	{ "bluetooth_stack", boot_init_bluetooth, DRIVER_BOOT_STAGE_NETWORK,
	  CONFIG_ENABLE_BLUETOOTH && CONFIG_INIT_BLUETOOTH_DRIVER },
	{ "usb_host", boot_init_usb_host, DRIVER_BOOT_STAGE_PLATFORM,
	  CONFIG_ENABLE_USB_HOST && CONFIG_INIT_USB_HOST },
};

const struct driver_boot_table ir0_boot_arch_driver_table =
	DRIVER_BOOT_TABLE(pc_boot_drivers);
