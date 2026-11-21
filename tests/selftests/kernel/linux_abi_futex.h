/* SPDX-License-Identifier: GPL-3.0-only */
/* Case list for linux_abi_futex.c (green: make ir0-selftest-futex-abi). */
CONF_CASE(futex_wait_timeout_etimedout)
CONF_CASE(futex_wait_bitset_absolute)
CONF_CASE(futex_bitset_mismatch_not_woken)
CONF_CASE(futex_requeue_moves_waiters)
CONF_CASE(futex_cmp_requeue_checks_value)
CONF_CASE(futex_private_isolated_across_processes)
CONF_CASE(futex_wake_more_than_64_waiters)
CONF_CASE(futex_robust_owner_died_wakes)
