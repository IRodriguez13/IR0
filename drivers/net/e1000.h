/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025 Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: e1000.h
 * Description: Intel e1000 network card driver definitions
 */

#pragma once

#include <stdint.h>
#include <stddef.h>

/* PCI configuration space access constants */
#define PCI_CONFIG_ADDRESS_PORT 0xCF8
#define PCI_CONFIG_DATA_PORT    0xCFC
#define PCI_ENABLE_BIT          0x80000000

/* PCI configuration register offsets */
#define PCI_REG_COMMAND         0x04
#define PCI_REG_BAR0            0x10
#define PCI_REG_BAR1            0x14

/* PCI command register bits */
#define PCI_CMD_IO_SPACE        (1 << 0)
#define PCI_CMD_MEM_SPACE       (1 << 1)
#define PCI_CMD_BUS_MASTER      (1 << 2)

/* PCI definitions for Intel e1000 */
#define E1000_VENDOR_ID         0x8086
/* Common e1000 device IDs */
#define E1000_DEVICE_ID_82540EM 0x100E
#define E1000_DEVICE_ID_82545EM 0x100F
#define E1000_DEVICE_ID_82541PI 0x107C
#define E1000_DEVICE_ID_82541EI 0x1013
#define E1000_DEVICE_ID_82546EB 0x1010
#define E1000_DEVICE_ID_82547EI 0x1019

/* E1000 Descriptor ring sizes */
#define E1000_TX_RING_SIZE      16
#define E1000_RX_RING_SIZE      16
#define E1000_TX_BUFFER_SIZE    2048
#define E1000_RX_BUFFER_SIZE    2048

/* E1000 Register offsets (MMIO) */
#define E1000_REG_CTRL          0x0000  /* Device Control */
#define E1000_REG_STATUS        0x0008  /* Device Status */
#define E1000_REG_EECD          0x0010  /* EEPROM/Flash Control/Data */
#define E1000_REG_EERD          0x0014  /* EEPROM Read */
#define E1000_REG_CTRL_EXT      0x0018  /* Extended Device Control */
#define E1000_REG_ICR           0x00C0  /* Interrupt Cause Read */
#define E1000_REG_ICS           0x00C8  /* Interrupt Cause Set */
#define E1000_REG_IMS           0x00D0  /* Interrupt Mask Set/Read */
#define E1000_REG_IMC           0x00D8  /* Interrupt Mask Clear */
#define E1000_REG_RCTL          0x0100  /* Receive Control */
#define E1000_REG_TCTL          0x0400  /* Transmit Control */
#define E1000_REG_TIPG          0x0410  /* Transmit Inter Packet Gap */
#define E1000_REG_TDBAL          0x3800 /* TX Descriptor Base Address Low */
#define E1000_REG_TDBAH          0x3804 /* TX Descriptor Base Address High */
#define E1000_REG_TDLEN          0x3808 /* TX Descriptor Length */
#define E1000_REG_TDH            0x3810 /* TX Descriptor Head */
#define E1000_REG_TDT            0x3818 /* TX Descriptor Tail */
#define E1000_REG_RDBAL          0x2800 /* RX Descriptor Base Address Low */
#define E1000_REG_RDBAH          0x2804 /* RX Descriptor Base Address High */
#define E1000_REG_RDLEN          0x2808 /* RX Descriptor Length */
#define E1000_REG_RDH            0x2810 /* RX Descriptor Head */
#define E1000_REG_RDT            0x2818 /* RX Descriptor Tail */
#define E1000_REG_RAL            0x5400 /* Receive Address Low (MAC) */
#define E1000_REG_RAH            0x5404 /* Receive Address High (MAC) */

/* Control Register bits */
#define E1000_CTRL_FD           (1 << 0)  /* Full Duplex */
#define E1000_CTRL_LRST         (1 << 3)  /* Link Reset */
#define E1000_CTRL_ASDE         (1 << 5)  /* Auto-Speed Detection Enable */
#define E1000_CTRL_SLU          (1 << 6)  /* Set Link Up */
#define E1000_CTRL_ILOS         (1 << 7)  /* Invert Loss of Signal */
#define E1000_CTRL_SPEED_SHIFT  8         /* Speed selection */
#define E1000_CTRL_FRCSPD       (1 << 11) /* Force Speed */
#define E1000_CTRL_FRCDPX       (1 << 12) /* Force Duplex */
#define E1000_CTRL_RST          (1 << 26) /* Device Reset */
#define E1000_CTRL_PHY_RST      (1 << 31) /* PHY Reset */

/* Status Register bits */
#define E1000_STATUS_FD         (1 << 0)  /* Full Duplex */
#define E1000_STATUS_LU         (1 << 1)  /* Link Up */
#define E1000_STATUS_SPEED_SHIFT 6        /* Speed */
#define E1000_STATUS_SPEED_MASK  0x3
#define E1000_STATUS_TXOFF       (1 << 22) /* Transmit Paused */
#define E1000_STATUS_RXOFF       (1 << 21) /* Receive Paused */

/* Interrupt bits */
#define E1000_ICR_TXDW          (1 << 0)  /* Transmit Descriptor Written Back */
#define E1000_ICR_TXQE          (1 << 1)  /* Transmit Queue Empty */
#define E1000_ICR_LSC           (1 << 2)  /* Link Status Change */
#define E1000_ICR_RXSEQ         (1 << 3)  /* RX Sequence Error */
#define E1000_ICR_RXDMT0        (1 << 4)  /* RX Descriptor Minimum Threshold */
#define E1000_ICR_RXO           (1 << 6)  /* RX Overrun */
#define E1000_ICR_RXT0          (1 << 7)  /* RX Timer Interrupt */
#define E1000_ICR_MDAC          (1 << 9)  /* MDI/O Access Complete */
#define E1000_ICR_RXCFG         (1 << 10) /* RX /C/ Ordered Sets */
#define E1000_ICR_PHYINT        (1 << 12) /* PHY Interrupt */
#define E1000_ICR_GPI_SDP0      (1 << 13) /* General Purpose Interrupts */
#define E1000_ICR_GPI_SDP1      (1 << 14)
#define E1000_ICR_GPI_SDP2      (1 << 15)
#define E1000_ICR_GPI_SDP3      (1 << 16)
#define E1000_ICR_TXD_LOW       (1 << 15) /* Transmit Descriptor Low Threshold */

/* Receive Control Register bits */
#define E1000_RCTL_EN           (1 << 1)  /* Enable */
#define E1000_RCTL_SBP          (1 << 2)  /* Store Bad Packets */
#define E1000_RCTL_UPE          (1 << 3)  /* Unicast Promiscuous Enable */
#define E1000_RCTL_MPE          (1 << 4)  /* Multicast Promiscuous Enable */
#define E1000_RCTL_LPE          (1 << 5)  /* Long Packet Enable */
#define E1000_RCTL_LBM_SHIFT    6         /* Loopback Mode */
#define E1000_RCTL_LBM_MASK     0x3
#define E1000_RCTL_RDMTS_SHIFT  8         /* RX Descriptor Minimum Threshold Size */
#define E1000_RCTL_BAM          (1 << 15) /* Broadcast Accept Mode */
#define E1000_RCTL_VFE          (1 << 18) /* VLAN Filter Enable */
#define E1000_RCTL_CFIEN        (1 << 19) /* Canonical Form Indicator Enable */
#define E1000_RCTL_CFI          (1 << 20) /* Canonical Form Indicator Bit Value */
#define E1000_RCTL_DPF          (1 << 22) /* Discard Pause Frames */
#define E1000_RCTL_PMCF         (1 << 23) /* Pass MAC Control Frames */
#define E1000_RCTL_BSIZE_SHIFT  16        /* Buffer Size */
#define E1000_RCTL_BSEX         (1 << 25) /* Buffer Size Extension */
#define E1000_RCTL_SECRC        (1 << 26) /* Strip Ethernet CRC */

/* Transmit Control Register bits */
#define E1000_TCTL_EN           (1 << 1)  /* Enable */
#define E1000_TCTL_PSP          (1 << 3)  /* Pad Short Packets */
#define E1000_TCTL_CT_SHIFT     4         /* Collision Threshold */
#define E1000_TCTL_COLD_SHIFT   12        /* Collision Distance */
#define E1000_TCTL_SWXOFF       (1 << 22) /* Software XOFF Transmission */
#define E1000_TCTL_RTLC         (1 << 24) /* Re-transmit on Late Collision */
#define E1000_TCTL_NRTU         (1 << 25) /* No Re-transmit on Underrun */

/* Transmit Descriptor Control bits */
#define E1000_TXD_CMD_EOP       (1 << 0)  /* End of Packet */
#define E1000_TXD_CMD_IFCS      (1 << 1)  /* Insert FCS */
#define E1000_TXD_CMD_IC        (1 << 2)  /* Insert Checksum */
#define E1000_TXD_CMD_RS        (1 << 3)  /* Report Status */
#define E1000_TXD_CMD_RPS       (1 << 4)  /* Report Packet Sent */
#define E1000_TXD_CMD_VLE       (1 << 6)  /* VLAN Packet Enable */
#define E1000_TXD_CMD_IDE       (1 << 7)  /* Interrupt Delay Enable */
#define E1000_TXD_STAT_DD       (1 << 0)  /* Descriptor Done */
#define E1000_TXD_STAT_EC       (1 << 1)  /* Excess Collisions */
#define E1000_TXD_STAT_LC       (1 << 2)  /* Late Collision */

/* Receive Descriptor Status bits */
#define E1000_RXD_STAT_DD       (1 << 0)  /* Descriptor Done */
#define E1000_RXD_STAT_EOP      (1 << 1)  /* End of Packet */
#define E1000_RXD_STAT_IXSM     (1 << 2)  /* Ignore Checksum Indication */
#define E1000_RXD_STAT_VP       (1 << 3)  /* VLAN Packet */
#define E1000_RXD_STAT_UDPCS    (1 << 4)  /* UDP Checksum Calculated on Packet */
#define E1000_RXD_STAT_TCPCS    (1 << 5)  /* TCP Checksum Calculated on Packet */
#define E1000_RXD_STAT_IPCS     (1 << 6)  /* IP Checksum Calculated on Packet */
#define E1000_RXD_STAT_PIF      (1 << 7)  /* Passed In-exact Filter */
#define E1000_RXD_STAT_IPIDV    (1 << 8)  /* IP Identification Valid */
#define E1000_RXD_STAT_CE       (1 << 9)  /* CRC Error or Alignment Error */
#define E1000_RXD_STAT_SE       (1 << 10) /* Symbol Error */
#define E1000_RXD_STAT_SEQ      (1 << 11) /* Sequence Error */
#define E1000_RXD_STAT_CXE      (1 << 12) /* Carrier Extension Error */
#define E1000_RXD_STAT_TCPE     (1 << 13) /* TCP/UDP Checksum Error */
#define E1000_RXD_STAT_IPE      (1 << 14) /* IP Checksum Error */
#define E1000_RXD_STAT_RXE      (1 << 15) /* RX Data Error */

/* E1000 Transmit Descriptor */
struct e1000_tx_desc {
    uint64_t buffer_addr;  /* Address of data buffer */
    uint16_t length;       /* Data buffer length */
    uint8_t cso;           /* Checksum offset */
    uint8_t cmd;           /* Command */
    uint8_t status;        /* Status */
    uint8_t css;           /* Checksum start */
    uint16_t special;
} __attribute__((packed));

/* E1000 Receive Descriptor */
struct e1000_rx_desc {
    uint64_t buffer_addr;  /* Address of data buffer */
    uint16_t length;       /* Data buffer length */
    uint16_t csum;         /* Checksum */
    uint8_t status;        /* Status */
    uint8_t errors;        /* Errors */
    uint16_t special;
} __attribute__((packed));

/* Public API */
int e1000_init(void);
void e1000_send(void *data, size_t len);
void e1000_handle_interrupt(void);
void e1000_get_mac(uint8_t mac[6]);

