/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once

#include <ir0/mm.h>

/* Address-space-aware TLB coherence requested by common MM code. */
void mm_tlb_sync_page(address_space_root_t root, uintptr_t va);
void mm_tlb_sync_all(address_space_root_t root);
