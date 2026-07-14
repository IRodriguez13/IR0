#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Host→guest TCP connector for F8 listen smoke (QEMU user-net hostfwd)."""

from __future__ import annotations

import argparse
import socket
import sys
import time


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", type=int, default=18777)
    ap.add_argument("--payload", default="LISTENOK\n")
    ap.add_argument("--timeout", type=float, default=60.0)
    ap.add_argument("--out", default="")
    args = ap.parse_args()

    deadline = time.time() + args.timeout
    last_err: OSError | None = None
    while time.time() < deadline:
        try:
            sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            sock.settimeout(2.0)
            sock.connect(("127.0.0.1", args.port))
            sock.sendall(args.payload.encode())
            # Half-close so guest can observe FIN → recv EOF.
            try:
                sock.shutdown(socket.SHUT_WR)
            except OSError:
                pass
            time.sleep(0.4)
            sock.close()
            if args.out:
                with open(args.out, "w", encoding="utf-8") as fh:
                    fh.write("F8_TCP_LISTEN_HOST_OK\n")
            print("F8_TCP_LISTEN_HOST_OK")
            return 0
        except OSError as exc:
            last_err = exc
            time.sleep(0.25)
    print(f"tcp_wire_host_connector: connect failed: {last_err}", file=sys.stderr)
    return 1


if __name__ == "__main__":
    sys.exit(main())
