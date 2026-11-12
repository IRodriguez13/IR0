/* SPDX-License-Identifier: GPL-3.0-only */
/* Case list for linux_abi_mm.c (green: make ir0-selftest-mm-abi). */
CONF_CASE(mm_mmap_requires_private_or_shared)
CONF_CASE(mm_mmap_huge_enomem)
CONF_CASE(mm_munmap_hole_succeeds)
CONF_CASE(mm_munmap_unaligned_einval)
CONF_CASE(mm_prot_none_faults)
CONF_CASE(mm_shared_anon_survives_fork)
CONF_CASE(time_clock_process_cputime)
CONF_CASE(time_realtime_from_rtc)
CONF_CASE(mm_unmapped_segv_handler_kills_task)
CONF_CASE(mm_fork_ro_page_stays_private)
/* Last: before the fix they stopped the IR0 arm64 guest. */
CONF_CASE(mm_mmap_fixed_over_kernel_pages_is_harmless)
CONF_CASE(mm_munmap_over_kernel_pages_is_harmless)
