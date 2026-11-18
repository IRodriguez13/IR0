#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""
Shell smoke tags derived from a QEMU serial log.

The kernel never knows which userspace program runs, so it cannot say that
a shell echoed a command. With ir0.tty_smoke=1 it only reports generic TTY
events (TTY_CANON_LINE_READY, SYS_READ_RETURN_OK); the console output is
mirrored to serial, and this module reads it to add the shell tags the
interactive smokes and the KTM classifier use:

  ASH_COMMAND_ECHO_OK  a line that is exactly "hi" after the shell banner
                       (the output of ``echo hi``, not the echoed command)
  ASH_COMMAND_EXEC_OK  a line that is exactly "/" after the banner (``pwd``)
"""

from __future__ import annotations

import re

SHELL_BANNER = "BusyBox v"
_ECHO_HI_RE = re.compile(r"(?:^|\n)\r?hi\r?\n")
_PWD_ROOT_RE = re.compile(r"(?:^|\n)\r?/\r?\n")


def derive_shell_tags(text: str) -> list[str]:
    """Return the shell tags the serial *text* proves, in a fixed order."""
    start = text.find(SHELL_BANNER)
    if start < 0:
        return []
    tail = text[start:]
    tags = []
    if _ECHO_HI_RE.search(tail):
        tags.append("ASH_COMMAND_ECHO_OK")
    if _PWD_ROOT_RE.search(tail):
        tags.append("ASH_COMMAND_EXEC_OK")
    return tags


def with_shell_tags(text: str) -> str:
    """*text* plus one line per derived tag it does not already contain."""
    extra = [tag for tag in derive_shell_tags(text) if tag not in text]
    if not extra:
        return text
    return text + "\n" + "\n".join(extra) + "\n"


def _self_test() -> None:
    log = ("boot\nBusyBox v1.36.1 built-in shell (ash)\n# echo hi\r\n"
           "hi\r\n# pwd\r\n/\r\n")
    assert derive_shell_tags(log) == ["ASH_COMMAND_ECHO_OK",
                                      "ASH_COMMAND_EXEC_OK"]
    assert derive_shell_tags("# echo hi\r\n") == []
    assert derive_shell_tags("BusyBox v1\n# echo hi\r\n") == []
    assert "ASH_COMMAND_ECHO_OK" in with_shell_tags(log)


if __name__ == "__main__":
    _self_test()
    print("serial_shell_tags self-test OK")
