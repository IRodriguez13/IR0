#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
#
# Headless TUI smoke: allocate a PTY so kmang/usmang curses can start, then quit.
# Docker clone gates have no controlling TTY; without a PTY the TUIs exit 2.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

python3 - "$ROOT" <<'PY'
import os
import pty
import select
import struct
import sys
import tempfile
import time
import fcntl
import termios

root = sys.argv[1]


def run_tui(
    argv: list[str], expect: str, timeout: float = 8.0,
    *, cwd: str | None = None, env: dict[str, str] | None = None,
) -> None:
    pid, fd = pty.fork()
    if pid == 0:
        os.environ["TERM"] = "xterm-256color"
        if env:
            os.environ.update(env)
        if cwd:
            os.chdir(cwd)
        os.execvp(argv[0], argv)
    # CI PTYs otherwise frequently report 0x0 or 24x80.  Exercise the layout
    # users actually get while retaining a deterministic headless smoke.
    fcntl.ioctl(fd, termios.TIOCSWINSZ, struct.pack("HHHH", 32, 120, 0, 0))
    buf = b""
    deadline = time.time() + timeout
    sent_q = False
    while time.time() < deadline:
        ready, _, _ = select.select([fd], [], [], 0.2)
        if ready:
            try:
                chunk = os.read(fd, 4096)
            except OSError:
                break
            if not chunk:
                break
            buf += chunk
        text = buf.decode("utf-8", "replace")
        if expect in text and not sent_q:
            os.write(fd, b"q")
            sent_q = True
        if sent_q and (time.time() + 0.4) >= deadline:
            break
    try:
        os.close(fd)
    except OSError:
        pass
    _pid, status = os.waitpid(pid, 0)
    text = buf.decode("utf-8", "replace")
    if expect not in text:
        sys.stderr.write(f"✗ TUI missing {expect!r} in {argv}\n{text[-2000:]}\n")
        raise SystemExit(1)
    if os.WIFEXITED(status) and os.WEXITSTATUS(status) not in (0, 1):
        # curses.wrapper may return 0; a hard crash is non-zero > 1.
        sys.stderr.write(f"✗ TUI {argv} exit {os.WEXITSTATUS(status)}\n")
        raise SystemExit(1)
    print(f"[OK]  TUI {argv[0]} ({expect})")


run_tui(
    [
        "python3",
        f"{root}/scripts/kernel_manager.py",
        "--machine-dir",
        "/tmp/ir0-tui-kmang",
        "--arch",
        "x86_64",
        "--profile",
        "minimal",
        "--machine",
        "ci",
        "tui",
    ],
    "IR0 Kernel Manager",
)
isd = os.environ.get("IR0_ISD_ROOT", os.path.join(root, "..", "ISD"))
run_tui(
    [
        "python3",
        f"{root}/scripts/userspace_manager.py",
        "--isd-root",
        isd,
        "--profile",
        "minimal",
        "tui",
    ],
    "IR0 Userspace Manager",
)
run_tui(
    ["python3", f"{root}/scripts/kconfig/menuconfig.py"],
    "IR0 Kernel Configuration",
    cwd=root,
)
with tempfile.TemporaryDirectory(prefix="isdconfig-tui-") as temp:
    config = os.path.join(temp, "custom.isdconfig")
    run_tui(
        [
            "python3", f"{isd}/scripts/isdconfig.py",
            "--profile", "custom", "--config", config, "menu",
        ],
        "ISD Distribution Configuration",
        cwd=isd,
        env={"ISD_CONFIG": config},
    )
    if os.path.exists(config):
        raise SystemExit("✗ isdconfig discard unexpectedly wrote the config")
print("✓ release-check-tui OK")
PY
