#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""
Run a QEMU userspace smoke, tail the serial log, and kill early on pass/fail tags.

Default max wait: 180s. Heavy smokes (TCC / BusyBox / Doom) should use 90–120s via
--profile or --timeout when wired from the Makefile.
"""

from __future__ import annotations

import argparse
import re
import signal
import subprocess
import sys
import time
from dataclasses import dataclass, field
from pathlib import Path
from typing import Iterable, Sequence

DEFAULT_TIMEOUT_SEC = 180
DEFAULT_STALE_SEC = 90
TAIL_LINES = 40

# Any line matching one of these (after success tags not yet complete) → FAIL.
DEFAULT_FAIL_RES: list[str] = [
    r"KERNEL PANIC",
    r"DOUBLE PANIC",
    r"panicex\(",
    r"panic\(",
    r"GPF_IN_USERSPACE|General protection fault",
    r"\bOOM\b|OOM_CLASS|USER_RECOVERABLE",
    r"FORK_STATE.*FAILED|fork failed|FORK.*FAIL",
    r"\bASSERT\b|assertion failed",
    r"WAIT_.*_INVALID",
    r"SYSCALL_.*_INVALID",
    r"BUSYBOX_FAIL",
    r"FASE52_FAIL",
    r"EXEC_ONLY_FAIL",
    r"BUSYBOX_FAIL_REASON=|FASE52_FAIL_REASON=|EXEC_ONLY_FAIL=",
    r"\[FASE[0-9A-Z]+\]\[FAIL\]",
    r"KSTACK_CANARY_BROKEN",
]

# Last-resort success tags when --success is not overridden.
DEFAULT_SUCCESS_TAGS: list[str] = []

PROFILES: dict[str, dict[str, object]] = {
    "musl-arch-prctl": {
        "success": ["MUSL_ARCH_PRCTL_OK"],
        "timeout": 45,
        "stale_sec": 20,
    },
    "musl-pthread": {
        "success": ["MUSL_PTHREAD_OK"],
        "timeout": 60,
        "stale_sec": 20,
    },
    "fase50-busybox": {
        "success": ["FASE50E_NO_REGRESSION"],
        "timeout": 150,
        "stale_sec": 90,
    },
    "fase52-tcc": {
        "success": ["FASE52_OK"],
        "timeout": 180,
        "stale_sec": 90,
    },
    "fase51-shell": {
        "success": ["DEBUG_FASE51_GATED"],
        "timeout": 120,
        "stale_sec": 60,
    },
    "fase55d-doom": {
        "success": [
            "DOOMGENERIC_FIRST_FRAME_OK",
            "FASE55D_DOOMGENERIC_OK",
            "DOOMGENERIC_FRAME_LOOP_OK",
        ],
        "success_mode": "any",
        "timeout": 120,
        "stale_sec": 45,
    },
    "fase50-exec-only": {
        "success": ["EXEC_ONLY_STABLE_OK"],
        "timeout": 120,
        "stale_sec": 60,
    },
    "fase53a-fs-dev": {
        "success": ["FASE53A_OK"],
        "timeout": 120,
        "stale_sec": 60,
    },
    "fase53b-posix": {
        "success": ["FASE53B_OK"],
        "timeout": 120,
        "stale_sec": 60,
    },
    "fase54a-fbdev": {
        "success": ["FASE54A_OK"],
        "timeout": 120,
        "stale_sec": 60,
    },
    "fase54b-input": {
        "success": ["FASE54B_OK"],
        "timeout": 120,
        "stale_sec": 60,
    },
    "fase54c-input-det": {
        "success": ["FASE54C_OK"],
        "timeout": 120,
        "stale_sec": 60,
    },
}

SYSCALL_LINE_RES: list[str] = [
    r"\[WAIT_EXIT_AUDIT\]\[sys_\w+\]",
    r"\[EXEC_AUDIT\]\[SYSCALL\]",
    r"SERIAL: sys_\w+",
    r"SERIAL: mmap: entering syscall",
    r"\[MMAP_AUDIT\].*stage=",
    r"\[FASE50\]\[EXEC_ARGV\].*stage=sys_exec",
]

TAG_LINE_RES: list[str] = [
    r"FASE[0-9A-Z_]+_OK\b",
    r"FASE[0-9A-Z_]+_FAIL\b",
    r"DOOMGENERIC_[A-Z_]+_OK\b",
    r"BUSYBOX_[A-Z_]+",
    r"KSTACK_[A-Z_]+_OK\b",
    r"DEBUG_[A-Z_]+",
    r"\[FASE[0-9A-Z]+\]\[CLASSIFY\]",
    r"\[FASE[0-9A-Z]+\]\[FAIL\]",
    r"_FAIL_REASON=",
]


@dataclass
class MonitorState:
    log_path: Path
    success_tags: list[str]
    success_mode: str  # "all" | "any"
    fail_res: list[re.Pattern[str]]
    timeout_sec: int
    stale_sec: int
    lines: list[str] = field(default_factory=list)
    last_tag: str = ""
    last_syscall: str = ""
    last_size: int = 0
    last_growth_at: float = field(default_factory=time.monotonic)
    matched_success: str = ""
    matched_fail: str = ""
    reason: str = "running"


def compile_patterns(raw: Sequence[str]) -> list[re.Pattern[str]]:
    out: list[re.Pattern[str]] = []
    for item in raw:
        out.append(re.compile(item, re.IGNORECASE))
    return out


def line_is_tag(line: str) -> bool:
    return any(re.search(p, line) for p in TAG_LINE_RES)


def line_is_syscall(line: str) -> bool:
    return any(re.search(p, line) for p in SYSCALL_LINE_RES)


def ingest_lines(state: MonitorState, new_text: str) -> None:
    if not new_text:
        return

    for line in new_text.splitlines():
        if not line:
            continue
        state.lines.append(line)
        if line_is_tag(line):
            state.last_tag = line.strip()
        if line_is_syscall(line):
            state.last_syscall = line.strip()


def read_new_log(state: MonitorState) -> None:
    try:
        data = state.log_path.read_text(encoding="utf-8", errors="replace")
    except FileNotFoundError:
        return

    size = len(data)
    if size > state.last_size:
        ingest_lines(state, data[state.last_size:])
        state.last_size = size
        state.last_growth_at = time.monotonic()


def success_satisfied(state: MonitorState) -> bool:
    if not state.success_tags:
        return False
    text = "\n".join(state.lines)
    if state.success_mode == "any":
        return any(tag in text for tag in state.success_tags)
    return all(tag in text for tag in state.success_tags)


def first_success_match(state: MonitorState) -> str:
    text = "\n".join(state.lines)
    if state.success_mode == "any":
        for tag in state.success_tags:
            if tag in text:
                return tag
        return ""
    if all(tag in text for tag in state.success_tags):
        return ",".join(state.success_tags)
    return ""


def first_fail_match(state: MonitorState) -> str:
    for line in state.lines:
        for pat in state.fail_res:
            if pat.search(line):
                return line.strip()
    return ""


def kill_process_group(proc: subprocess.Popen[bytes]) -> None:
    if proc.poll() is not None:
        return
    try:
        proc.send_signal(signal.SIGTERM)
        proc.wait(timeout=5)
    except subprocess.TimeoutExpired:
        proc.kill()
        proc.wait(timeout=5)
    except ProcessLookupError:
        pass


def print_summary(state: MonitorState, elapsed_sec: float, verdict: str) -> None:
    print("=== SMOKE AUTOKILL SUMMARY ===")
    print(f"verdict: {verdict}")
    print(f"reason: {state.reason}")
    print(f"duration_sec: {elapsed_sec:.1f}")
    print(f"log: {state.log_path}")
    if state.matched_success:
        print(f"success_tag: {state.matched_success}")
    if state.matched_fail:
        print(f"fail_match: {state.matched_fail}")
    if state.last_tag:
        print(f"last_tag: {state.last_tag}")
    if state.last_syscall:
        print(f"last_syscall: {state.last_syscall}")
    print(f"--- last {TAIL_LINES} log lines ---")
    for line in state.lines[-TAIL_LINES:]:
        print(line)
    print("=== END SMOKE AUTOKILL SUMMARY ===")


def run_smoke(
    log_path: Path,
    qemu_cmd: list[str],
    success_tags: list[str],
    success_mode: str,
    fail_res: list[str],
    timeout_sec: int,
    stale_sec: int,
) -> int:
    log_path.parent.mkdir(parents=True, exist_ok=True)
    if log_path.exists():
        log_path.unlink()

    state = MonitorState(
        log_path=log_path,
        success_tags=success_tags,
        success_mode=success_mode,
        fail_res=compile_patterns(fail_res),
        timeout_sec=timeout_sec,
        stale_sec=stale_sec,
    )

    with log_path.open("ab", buffering=0) as log_fp:
        proc = subprocess.Popen(
            qemu_cmd,
            stdout=log_fp,
            stderr=subprocess.STDOUT,
            start_new_session=True,
        )

        start = time.monotonic()
        try:
            while True:
                read_new_log(state)
                elapsed = time.monotonic() - start

                fail_line = first_fail_match(state)
                if fail_line:
                    state.reason = "fail_tag"
                    state.matched_fail = fail_line
                    kill_process_group(proc)
                    print_summary(state, elapsed, "FAIL")
                    return 1

                if success_satisfied(state):
                    state.reason = "success_tag"
                    state.matched_success = first_success_match(state)
                    kill_process_group(proc)
                    print_summary(state, elapsed, "PASS")
                    return 0

                if proc.poll() is not None:
                    read_new_log(state)
                    if success_satisfied(state):
                        state.reason = "qemu_exit_success"
                        state.matched_success = first_success_match(state)
                        print_summary(state, elapsed, "PASS")
                        return 0
                    state.reason = "qemu_exit"
                    state.matched_fail = state.lines[-1].strip() if state.lines else ""
                    print_summary(state, elapsed, "FAIL")
                    return 1

                if elapsed >= timeout_sec:
                    state.reason = "timeout"
                    kill_process_group(proc)
                    print_summary(state, elapsed, "FAIL")
                    return 1

                if (
                    stale_sec > 0
                    and state.lines
                    and (time.monotonic() - state.last_growth_at) >= stale_sec
                ):
                    state.reason = "stale_no_progress"
                    kill_process_group(proc)
                    print_summary(state, elapsed, "FAIL")
                    return 1

                time.sleep(0.25)
        finally:
            kill_process_group(proc)

    return 1


def parse_args(argv: Sequence[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Run QEMU smoke with serial-log autokill on pass/fail tags.",
    )
    parser.add_argument("--log", required=True, help="Serial log file path")
    parser.add_argument(
        "--timeout",
        type=int,
        default=DEFAULT_TIMEOUT_SEC,
        help=f"Hard max seconds (default {DEFAULT_TIMEOUT_SEC})",
    )
    parser.add_argument(
        "--stale-sec",
        type=int,
        default=DEFAULT_STALE_SEC,
        help=f"Fail if log stops growing for N seconds (default {DEFAULT_STALE_SEC}, 0=off)",
    )
    parser.add_argument(
        "--profile",
        choices=sorted(PROFILES.keys()),
        help="Preset success tags and timeouts for a known smoke",
    )
    parser.add_argument(
        "--success",
        action="append",
        default=[],
        dest="success_tags",
        help="Success tag (repeat). Default: all required unless --success-mode any",
    )
    parser.add_argument(
        "--success-mode",
        choices=("all", "any"),
        default="all",
        help="Require all success tags (all) or any one (any)",
    )
    parser.add_argument(
        "--fail",
        action="append",
        default=[],
        dest="fail_patterns",
        help="Extra fail regex (repeat)",
    )
    parser.add_argument(
        "qemu_cmd",
        nargs=argparse.REMAINDER,
        help="QEMU command after --",
    )
    return parser.parse_args(list(argv))


def main(argv: Sequence[str] | None = None) -> int:
    args = parse_args(argv if argv is not None else sys.argv[1:])

    qemu_cmd = args.qemu_cmd
    if qemu_cmd and qemu_cmd[0] == "--":
        qemu_cmd = qemu_cmd[1:]
    if not qemu_cmd:
        print("smoke_autokill.py: missing QEMU command after --", file=sys.stderr)
        return 2

    success_tags = list(args.success_tags)
    success_mode = args.success_mode
    timeout_sec = args.timeout
    stale_sec = args.stale_sec
    fail_patterns = list(DEFAULT_FAIL_RES) + list(args.fail_patterns)

    if args.profile:
        prof = PROFILES[args.profile]
        if not success_tags:
            success_tags = list(prof.get("success", []))  # type: ignore[arg-type]
        if args.timeout == DEFAULT_TIMEOUT_SEC:
            timeout_sec = int(prof.get("timeout", DEFAULT_TIMEOUT_SEC))
        if args.stale_sec == DEFAULT_STALE_SEC:
            stale_sec = int(prof.get("stale_sec", DEFAULT_STALE_SEC))
        if success_mode == "all" and prof.get("success_mode") == "any":
            success_mode = "any"

    if not success_tags:
        print(
            "smoke_autokill.py: need --success TAG or --profile",
            file=sys.stderr,
        )
        return 2

    return run_smoke(
        log_path=Path(args.log),
        qemu_cmd=qemu_cmd,
        success_tags=success_tags,
        success_mode=success_mode,
        fail_res=fail_patterns,
        timeout_sec=timeout_sec,
        stale_sec=stale_sec,
    )


if __name__ == "__main__":
    sys.exit(main())
