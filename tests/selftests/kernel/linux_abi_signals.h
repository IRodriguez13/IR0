/* SPDX-License-Identifier: GPL-3.0-only */
/* Case list for linux_abi_signals.c: every case is fixed (green group). */
CONF_CASE(sig_kill_stop_not_blockable)
CONF_CASE(sig_sa_resethand_resets)
CONF_CASE(sig_sigsuspend_returns_eintr)
CONF_CASE(sig_nanosleep_eintr_from_other_process)
CONF_CASE(sig_wait4_bad_options_einval)
CONF_CASE(sig_handler_gets_signum)
CONF_CASE(sig_kill_all_spares_caller)
CONF_CASE(sig_socket_read_restarts)
CONF_CASE(sig_sigreturn_delivers_unmasked)
CONF_CASE(sig_default_action_terminates)
CONF_CASE(sig_kill_cpu_bound_child)
