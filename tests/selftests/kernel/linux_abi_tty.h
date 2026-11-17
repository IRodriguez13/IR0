/* SPDX-License-Identifier: GPL-3.0-only */
/* Case list for linux_abi_tty.c. */
CONF_CASE(tty_tiocsti_queues_input)
CONF_CASE(tty_tcsets_keeps_icrnl_clear)
CONF_CASE(tty_canon_cr_is_data_without_icrnl)
CONF_CASE(tty_vintr_without_foreground_signals_nobody)
CONF_CASE(tty_vintr_flushes_input)
CONF_CASE(tty_vintr_noflsh_keeps_input)
CONF_CASE(tty_caught_signal_keeps_input)
CONF_CASE(tty_exec_keeps_termios)
