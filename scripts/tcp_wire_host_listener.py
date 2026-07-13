#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Minimal TCP listener for F8-3 wire smoke (host side, guest connects via 10.0.2.2)."""

from __future__ import annotations

import argparse
import socket
import sys


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", type=int, default=8888)
    ap.add_argument("--expect", default="WIRETCP")
    ap.add_argument("--timeout", type=float, default=45.0)
    ap.add_argument("--out", default="")
    args = ap.parse_args()

    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind(("0.0.0.0", args.port))
    srv.listen(1)
    srv.settimeout(args.timeout)
    try:
        conn, _addr = srv.accept()
        conn.settimeout(5.0)
        data = conn.recv(4096)
        conn.close()
    except OSError as exc:
        print(f"tcp_wire_host_listener: accept failed: {exc}", file=sys.stderr)
        return 1
    finally:
        srv.close()

    if args.expect.encode() not in data:
        print(f"tcp_wire_host_listener: expected {args.expect!r}, got {data!r}", file=sys.stderr)
        return 1

    if args.out:
        with open(args.out, "w", encoding="utf-8") as fh:
            fh.write("F8_TCP_WIRE_HOST_OK\n")
    print("F8_TCP_WIRE_HOST_OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
