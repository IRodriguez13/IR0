# SPDX-License-Identifier: GPL-3.0-only
#
# PC platform devices (x86-64): port I/O, i8042 PS/2, PIT, HPET, local APIC,
# ACPI power management, CMOS RTC, 16550 COM ports and PCI NICs probed through
# configuration mechanism #1. The root Makefile includes exactly one
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
	drivers/timer/rtc/rtc.o

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
