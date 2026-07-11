#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Self-test for scripts/ktm_log_classify.py pattern table."""

from __future__ import annotations

import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))

from ktm_log_classify import classify  # noqa: E402


def _names(hits: list) -> set[str]:
    return {h[0] for h in hits}


def main() -> int:
    failed = 0

    stall = classify("TTY_CANON_LINE_READY\nTTY_READERS_WAKE_OK\n")
    if "TTY_WAKE_READ_STALL" not in _names(stall):
        print("FAIL: expected TTY_WAKE_READ_STALL")
        failed += 1

    ok = classify(
        "TTY_CANON_LINE_READY\nSYS_READ_RETURN_OK\nASH_COMMAND_ECHO_OK\n"
    )
    if "SMOKE_ASH_INTERACTIVE_OK" not in _names(ok):
        print("FAIL: expected SMOKE_ASH_INTERACTIVE_OK")
        failed += 1

    fork_stall = classify(
        "SYS_READ_RETURN_OK\n[WAIT_EXIT_AUDIT][sys_wait4] entry parent_pid=6\n"
    )
    if "FORK_WAIT_ECHO_STALL" not in _names(fork_stall):
        print("FAIL: expected FORK_WAIT_ECHO_STALL")
        failed += 1

    rip = classify(
        "KERNEL PANIC\nCONTEXT_LIFETIME_BROKEN\n[KTM][PANIC_CLASS] KERNEL_JUMP_BAD_RIP\n"
    )
    if "SCHED_IRQ_WITHOUT_FRAME" not in _names(rip):
        print("FAIL: expected SCHED_IRQ_WITHOUT_FRAME on panic+CONTEXT_LIFETIME")
        failed += 1

    ev = classify(
        "[KTM][EV] SCHED_IRQ_TIMER_PREEMPT\n"
        "[KTM][EV] WAIT_PROMOTE_CHILD pid=00000008\n"
        "SYS_READ_RETURN_OK\nASH_COMMAND_ECHO_OK\n"
    )
    if "SMOKE_ASH_INTERACTIVE_OK" not in _names(ev):
        print("FAIL: expected SMOKE_ASH_INTERACTIVE_OK with KTM events")
        failed += 1

    if failed:
        print(f"KTM classify selftest: {failed} failure(s)")
        return 1

    print("KTM classify selftest: OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
