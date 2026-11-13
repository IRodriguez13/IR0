/* SPDX-License-Identifier: GPL-3.0-only */
/* Case list for linux_abi_uaccess.c (green: make ir0-selftest-uaccess-abi). */
CONF_CASE(uaccess_pipe2_flags_before_copy)
CONF_CASE(uaccess_wait4_bad_status_reaps)
CONF_CASE(uaccess_clock_gettime_clock_first)
CONF_CASE(uaccess_futex_unknown_op_enosys)
CONF_CASE(uaccess_recvmsg_fault_keeps_data_and_rights)
CONF_CASE(uaccess_sendmsg_fault_sends_nothing)
CONF_CASE(uaccess_recvfrom_bad_addr_consumes)
CONF_CASE(uaccess_mmap_fixed_beyond_user_enomem)
CONF_CASE(uaccess_mprotect_beyond_user_enomem)
