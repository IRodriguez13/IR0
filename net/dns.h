/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025 Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: dns.h
 * Description: DNS (Domain Name System) client header and API definitions
 */

#pragma once

#include <ir0/net.h>
#include <stdint.h>
#include <stddef.h>

/* DNS Protocol Constants */
#define DNS_PORT 53
#define DNS_MAX_QUERY_LEN 512
#define DNS_DEFAULT_TIMEOUT_MS 5000

/* DNS Record Types */
#define DNS_TYPE_A 1      /* IPv4 address */
#define DNS_TYPE_NS 2     /* Name server */
#define DNS_TYPE_CNAME 5  /* Canonical name */
#define DNS_TYPE_MX 15    /* Mail exchange */

/* DNS Class */
#define DNS_CLASS_IN 1    /* Internet */

/* DNS Query Flags */
#define DNS_FLAG_RD (1 << 8)  /* Recursion Desired */

/* DNS Response Codes */
#define DNS_RCODE_NOERROR 0
#define DNS_RCODE_FORMERR 1
#define DNS_RCODE_SERVFAIL 2
#define DNS_RCODE_NXDOMAIN 3

/* DNS API */
int dns_init(void);
ip4_addr_t dns_resolve(const char *domain_name, ip4_addr_t dns_server_ip);

