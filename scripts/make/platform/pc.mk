# SPDX-License-Identifier: GPL-3.0-only
#
# PC platform devices (x86-64): i8042 PS/2, PIT, HPET, local APIC, ACPI power
# management, CMOS RTC, 16550 COM ports, PC speaker, ISA sound and DMA, ATA,
# and the PCI devices probed through configuration mechanism #1 (AHCI, NVMe,
# NICs, USB host, virtio-9p). Their boot entries are registered by
# arch/x86-64/sources/boot_drivers.c. The root Makefile includes exactly one
# scripts/make/platform/<platform>.mk; generic code never names these devices.

PLATFORM_OBJS = \
	drivers/input/ps2.o \
	drivers/input/ps2_set1.o \
	drivers/input/ps2_mouse_pkt.o \
	drivers/timer/pit/pit.o \
	drivers/timer/hpet/hpet.o \
	drivers/timer/hpet/find_hpet.o \
	drivers/timer/acpi/acpi_pm.o \
	drivers/timer/lapic/lapic.o \
	drivers/serial/serial.o \
	drivers/timer/rtc/rtc.o \
	drivers/storage/ahci.o \
	drivers/storage/nvme.o \
	drivers/virtio/virtio_9p_pci.o

ifneq ($(CONFIG_ENABLE_MOUSE),n)
PLATFORM_OBJS += drivers/input/ps2_mouse.o
endif

ifneq ($(CONFIG_ENABLE_NETWORKING),n)
PLATFORM_OBJS += drivers/net/net_drivers_pc.o
ifneq ($(CONFIG_DRV_NIC_RTL8139),n)
PLATFORM_OBJS += drivers/net/rtl8139.o
endif
ifneq ($(CONFIG_DRV_NIC_VIRTIO_NET),n)
PLATFORM_OBJS += drivers/net/virtio_net.o
endif
endif

# Sound Blaster 16 + Adlib OPL2 + ISA DMA
ifneq ($(CONFIG_ENABLE_SOUND),n)
PLATFORM_OBJS += \
	sound/sound_blaster.o \
	sound/adlib.o \
	drivers/dma/dma.o
endif

# Bluetooth: the HCI stack reaches its controller over a COM port.
ifneq ($(CONFIG_ENABLE_BLUETOOTH),n)
PLATFORM_OBJS += \
	drivers/bluetooth/hci_uart.o \
	drivers/bluetooth/hci_core.o \
	drivers/bluetooth/bluetooth_iface.o \
	drivers/bluetooth/bt_device.o \
	drivers/bluetooth/bt_sysfs.o \
	drivers/bluetooth/bluetooth_init.o
else
PLATFORM_OBJS += drivers/bluetooth/bluetooth_iface_stub.o
endif

ifneq ($(CONFIG_ENABLE_USB_HOST),n)
PLATFORM_OBJS += drivers/usb/usb_host.o
endif

ifneq ($(CONFIG_ENABLE_PC_SPEAKER),n)
PLATFORM_OBJS += drivers/char/pc_speaker.o
endif

ifneq ($(CONFIG_ENABLE_STORAGE_ATA),n)
PLATFORM_OBJS += \
	drivers/storage/ata.o \
	drivers/storage/ata_helpers.o
ifneq ($(CONFIG_ENABLE_STORAGE_ATA_BLOCK),n)
PLATFORM_OBJS += drivers/storage/ata_block.o
endif
endif
