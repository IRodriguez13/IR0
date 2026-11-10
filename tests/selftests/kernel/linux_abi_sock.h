/* SPDX-License-Identifier: GPL-3.0-only */
/* Case list for linux_abi_sock.c (RED: fails on dev 629036ef). */
CONF_CASE(sock_bad_family_eafnosupport)
CONF_CASE(sock_send_non_socket_enotsock)
CONF_CASE(sock_bind_twice_einval)
CONF_CASE(sock_connect_missing_path_enoent)
CONF_CASE(sock_getsockname_returns_path)
CONF_CASE(sock_unix_dgram_supported)
