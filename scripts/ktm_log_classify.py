#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""
KTM — classify QEMU serial / smoke logs into known failure patterns.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path


def classify(text: str) -> list[tuple[str, str, str]]:
    hits: list[tuple[str, str, str]] = []
    has_panic = "KERNEL PANIC" in text or "DOUBLE PANIC" in text
    has_read_ok = "SYS_READ_RETURN_OK" in text
    has_echo_ok = "ASH_COMMAND_ECHO_OK" in text
    has_wait_stall = (
        re.search(r"\[WAIT_EXIT_AUDIT\]\[sys_wait4\] entry", text)
        and "ASH_COMMAND_ECHO_OK" not in text
        and not re.search(r"\[WAIT_EXIT_AUDIT\]\[sys_wait4\] return", text[-4000:])
    )

    if re.search(r"SCHED_PICK.*prev_state=.*0000000000000002", text) and not has_read_ok:
        hits.append((
            "SCHED_BLOCKED_NO_PREEMPT",
            "medium",
            "Runnable task exists but blocked waiter was not preempted — "
            "check sched_try_preempt_blocked and rr_promote_process.",
        ))

    # Legacy gate tag: only when panic/context lifetime broke, not on every IRQ.
    if has_panic and (
        re.search(r"CONTEXT_LIFETIME_BROKEN", text)
        or re.search(r"\[KTM\]\[SCHED_GATE\]", text)
    ):
        hits.append((
            "SCHED_IRQ_WITHOUT_FRAME",
            "high",
            "Context switch from IRQ without a saved user iretq frame — use "
            "sched_irq_preempt_from_frame at ISR stub exit.",
        ))

    if has_read_ok and has_echo_ok and not has_panic:
        hits.append((
            "SMOKE_ASH_INTERACTIVE_OK",
            "info",
            "Full ash interactive path: TTY read unblocked and echo reached stdout.",
        ))
    elif has_read_ok and not has_echo_ok and not has_panic:
        hits.append((
            "TTY_READ_UNBLOCK_OK",
            "info",
            "TTY wake + preempt completed sys_read; echo/exec still pending.",
        ))

    if has_wait_stall and has_read_ok and not has_echo_ok and not has_panic:
        hits.append((
            "FORK_WAIT_ECHO_STALL",
            "medium",
            "Parent entered wait4 after fork but child never printed hi — "
            "check timer IRQ preempt (sched_irq_may_preempt PIT) and "
            "rr_promote_process(child) after fork.",
        ))

    if "TTY_CANON_LINE_READY" in text and not has_read_ok:
        hits.append((
            "TTY_WAKE_READ_STALL",
            "medium",
            "Canonical line ready but read() did not complete — check logger "
            "blocking (pause), scheduler resched, and prev==next READY path.",
        ))

    if "[KTM][PANIC_CLASS] KERNEL_JUMP_BAD_RIP" in text or (
        re.search(r"KERNEL_DEREF_USERPTR", text) and
        re.search(r"kernel_fault_rip=", text)
    ):
        hits.append((
            "KERNEL_JUMP_BAD_RIP",
            "high",
            "Kernel RIP points into userspace range — inspect task->rip/cs and "
            "blocked-syscall resume (irq_frame_saved / poll_resume_via_arch).",
        ))

    if "RUNIT_STAGE1_OK" in text and "RUNIT_STAGE2_OK" not in text:
        hits.append((
            "RUNIT_BOOT_INCOMPLETE",
            "medium",
            "Runit stopped after stage1 — poll/selfpipe or stage2 exec failure.",
        ))

    if has_panic and not any(h[0] in (
        "SCHED_IRQ_WITHOUT_FRAME",
        "KERNEL_JUMP_BAD_RIP",
    ) for h in hits):
        hits.append((
            "KERNEL_PANIC_UNCLASSIFIED",
            "low",
            "Inspect [KTM][CTX], [PF_AUDIT], and [CONTRACT] lines in the log.",
        ))

    return hits


def main() -> int:
    parser = argparse.ArgumentParser(description="KTM smoke log classifier")
    parser.add_argument("log", nargs="?", type=Path, help="log file (default: stdin)")
    args = parser.parse_args()

    text = args.log.read_text(errors="replace") if args.log else sys.stdin.read()
    hits = classify(text)

    if not hits:
        print("KTM_CLASSIFY: no known pattern")
        return 0

    print("KTM_CLASSIFY:")
    for name, conf, hint in hits:
        print(f"  - {name} ({conf})")
        print(f"    {hint}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
