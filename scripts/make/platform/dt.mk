# SPDX-License-Identifier: GPL-3.0-only
#
# Device-tree platforms (arm64: QEMU virt, Raspberry Pi). The board's core
# devices (GIC, PL011, PL031, generic timer, virtio-mmio) live in
# scripts/make/arch-sources.mk; this list holds the optional device drivers.

# No Bluetooth transport on these boards yet: the interface reports -ENOSYS.
PLATFORM_OBJS = drivers/bluetooth/bluetooth_iface_stub.o

ifneq ($(CONFIG_ENABLE_NETWORKING),n)
PLATFORM_OBJS += drivers/net/net_drivers_dt.o
ifneq ($(CONFIG_DRV_NIC_BCMGENET),n)
PLATFORM_OBJS += drivers/net/bcmgenet.o
endif
endif
