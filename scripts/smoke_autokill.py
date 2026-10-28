#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""
Run a QEMU userspace smoke, tail the serial log, and kill early on pass/fail tags.

Default max wait: 180s. Heavy smokes (TCC / BusyBox / Doom) should use 90–120s via
--profile or --timeout when wired from the Makefile.
"""

from __future__ import annotations

import argparse
import os
import re
import signal
import subprocess
import sys
import time
from collections import deque
from dataclasses import dataclass, field
from pathlib import Path
from typing import Sequence

# Scripts live next to this file; keep the import working from any cwd.
sys.path.insert(0, str(Path(__file__).resolve().parent))

from ktm_host_safety import (  # noqa: E402  (after sys.path setup)
    DEFAULT_MEM_MIB,
    LOG_CHUNK,
    MAX_LINE_CHARS,
    MAX_LOG_BYTES,
    MAX_MEM_MIB,
    MAX_TIMEOUT_SEC,
    ensure_qemu_mem,
    parse_mem_mib,
)

DEFAULT_TIMEOUT_SEC = 180
DEFAULT_STALE_SEC = 90
TAIL_LINES = 40
READ_CHUNK = LOG_CHUNK          # 1 MiB per poll: bounded host RAM
MAX_PENDING_CHARS = MAX_LINE_CHARS  # a line without newline cannot grow forever
TAG_TAIL_CHARS = 16 * 1024      # overlap so a tag split across reads still matches
DEFAULT_MAX_LOG_MIB = MAX_LOG_BYTES // (1024 * 1024)

# Any line matching one of these (after success tags not yet complete) → FAIL.
DEFAULT_FAIL_RES: list[str] = [
    r"KERNEL PANIC",
    r"DOUBLE PANIC",
    r"panicex\(",
    r"panic\(",
    r"GPF_IN_USERSPACE|General protection fault",
    # Bare "\bOOM\b" false-positives when QEMU serial splits DOOMGENERIC as "D\\nOOM\\n…".
    # Prefer explicit OOM markers; keep [OOM_CLASS] and USER_RECOVERABLE.
    r"\[OOM\]|\[OOM_CLASS\]|\bOOM_KILL\b|\bout of memory\b|\bOut of memory\b|\bUSER_RECOVERABLE\b",
    r"FORK_STATE.*FAILED|fork failed|FORK.*FAIL",
    r"\bASSERT\b|assertion failed",
    r"WAIT_.*_INVALID",
    r"SYSCALL_.*_INVALID",
    r"BUSYBOX_FAIL",
    r"KTM_TCC_FAIL",
    r"KTM_EXEC_ONLY_FAIL",
    r"BUSYBOX_FAIL_REASON=|KTM_TCC_FAIL_REASON=|KTM_EXEC_ONLY_FAIL=",
    r"\[KTM_[A-Z0-9_]+\]\[FAIL\]",
    r"KSTACK_CANARY_BROKEN",
    r"DESK_SESSION_FAIL",
    r"DESK_CLIENT_FAIL",
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
    "busybox": {
        "success": ["KTM_BUSYBOX_NO_REGRESSION"],
        "timeout": 150,
        "stale_sec": 90,
    },
    "tcc": {
        "success": ["KTM_TCC_OK"],
        "timeout": 300,
        "stale_sec": 240,
    },
    "tcc-power-halt": {
        "success": ["SYSTEM_SHUTDOWN_HALT"],
        "timeout": 90,
        "stale_sec": 45,
    },
    "shell": {
        "success": ["KTM_SHELL_DEBUG_GATED"],
        "timeout": 120,
        "stale_sec": 60,
    },
    "doomgeneric": {
        # Wait for terminal OK tag — FRAME_LOOP alone used to autokill (mode any)
        # before KTM_DOOMGENERIC_OK was flushed → flaky post-grep FAIL.
        # Real IWAD boot + first frames often exceed 120s on loaded hosts.
        "success": ["KTM_DOOMGENERIC_OK"],
        "success_mode": "all",
        "timeout": 240,
        "stale_sec": 90,
    },
    "exec-only": {
        "success": ["KTM_EXEC_ONLY_STABLE_OK"],
        "timeout": 120,
        "stale_sec": 60,
    },
    "fs-dev": {
        "success": ["KTM_FS_DEV_OK"],
        "timeout": 120,
        "stale_sec": 60,
    },
    "posix-pseudofs": {
        "success": ["KTM_POSIX_PSEUDOFS_OK"],
        "timeout": 120,
        "stale_sec": 60,
    },
    "fbdev": {
        "success": ["KTM_FBDEV_OK"],
        "timeout": 120,
        "stale_sec": 60,
    },
    "input": {
        "success": ["KTM_INPUT_OK"],
        "timeout": 120,
        "stale_sec": 60,
    },
    "input-det": {
        "success": ["KTM_INPUT_DET_OK"],
        "timeout": 120,
        "stale_sec": 60,
    },
}
# Historical --profile names (same dicts).
PROFILES["fase50-busybox"] = PROFILES["busybox"]
PROFILES["fase52-tcc"] = PROFILES["tcc"]
PROFILES["fase51-shell"] = PROFILES["shell"]
PROFILES["fase55d-doom"] = PROFILES["doomgeneric"]
PROFILES["fase50-exec-only"] = PROFILES["exec-only"]
PROFILES["fase53a-fs-dev"] = PROFILES["fs-dev"]
PROFILES["fase53b-posix"] = PROFILES["posix-pseudofs"]
PROFILES["fase54a-fbdev"] = PROFILES["fbdev"]
PROFILES["fase54b-input"] = PROFILES["input"]
PROFILES["fase54c-input-det"] = PROFILES["input-det"]

SYSCALL_LINE_RES: list[str] = [
    r"\[WAIT_EXIT_AUDIT\]\[sys_\w+\]",
    r"\[EXEC_AUDIT\]\[SYSCALL\]",
    r"SERIAL: sys_\w+",
    r"SERIAL: mmap: entering syscall",
    r"\[MMAP_AUDIT\].*stage=",
    r"CLASSIFY.*stage=",
    r"\[KTM\]\[EXEC_ARGV\].*stage=sys_exec",
]

TAG_LINE_RES: list[str] = [
    r"FASE[0-9A-Z_]+_OK\b",
    r"FASE[0-9A-Z_]+_FAIL\b",
    r"DOOMGENERIC_[A-Z_]+_OK\b",
    r"BUSYBOX_[A-Z_]+",
    r"KSTACK_[A-Z_]+_OK\b",
    r"DEBUG_[A-Z_]+",
    r"\[FASE[0-9A-Z]+\]\[CLASSIFY\]",  # legacy
    r"\[FASE[0-9A-Z]+\] CLASSIFY",
    r"\[KTM_[A-Z0-9_]+\]\[FAIL\]",
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
    max_log_bytes: int
    # Bounded history: a runaway guest must not grow host RAM.
    lines: deque[str] = field(default_factory=lambda: deque(maxlen=TAIL_LINES * 8))
    seen_success: set[str] = field(default_factory=set)
    scan_tail: str = ""
    last_tag: str = ""
    last_syscall: str = ""
    offset: int = 0
    last_growth_at: float = field(default_factory=time.monotonic)
    matched_success: str = ""
    matched_fail: str = ""
    reason: str = "running"
    pending: str = ""


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

    # Keep a trailing partial line: QEMU serial often flushes mid-tag; treating
    # "IR0" as a finished line breaks success matching on joined lines.
    data = state.pending + new_text.replace("\0", "")
    state.pending = ""
    parts = data.split("\n")
    if not data.endswith("\n") and not data.endswith("\r"):
        state.pending = parts[-1]
        parts = parts[:-1]
    if len(state.pending) > MAX_PENDING_CHARS:
        # A line that never ends is a broken guest, not an excuse to grow RAM.
        state.pending = state.pending[-MAX_PENDING_CHARS:]

    for part in parts:
        line = part.rstrip("\r")
        if not line:
            continue
        state.lines.append(line)
        if line_is_tag(line):
            state.last_tag = line.strip()
        if line_is_syscall(line):
            state.last_syscall = line.strip()
        if not state.matched_fail:
            for pat in state.fail_res:
                if pat.search(line):
                    state.matched_fail = line.strip()
                    break

    # Bounded overlap so a success tag split across reads still matches.
    flat = data.replace("\r", "").replace("\n", "")
    state.scan_tail = (state.scan_tail + flat)[-TAG_TAIL_CHARS:]
    for tag in state.success_tags:
        if tag in state.scan_tail:
            state.seen_success.add(tag)


def read_new_log(state: MonitorState) -> None:
    try:
        size = state.log_path.stat().st_size
    except (FileNotFoundError, OSError):
        return

    # Truncation/rotation: restart from the beginning instead of seeking past EOF.
    if size < state.offset:
        state.offset = 0
        state.pending = ""
        state.scan_tail = ""

    progressed = False
    budget = READ_CHUNK
    while budget > 0 and state.offset < size:
        try:
            with state.log_path.open("rb") as fp:
                fp.seek(state.offset)
                chunk = fp.read(min(budget, size - state.offset))
        except (FileNotFoundError, OSError):
            return
        if not chunk:
            return
        state.offset += len(chunk)
        budget -= len(chunk)
        progressed = True
        ingest_lines(state, chunk.decode("utf-8", errors="replace"))
    if progressed:
        state.last_growth_at = time.monotonic()


def drain_log(state: MonitorState) -> None:
    """Read every remaining byte after QEMU exits (still bounded per call)."""
    while True:
        before = state.offset
        read_new_log(state)
        if state.offset == before:
            return


def success_satisfied(state: MonitorState) -> bool:
    if not state.success_tags:
        return False
    if state.success_mode == "any":
        return bool(state.seen_success)
    return all(tag in state.seen_success for tag in state.success_tags)


def first_success_match(state: MonitorState) -> str:
    if state.success_mode == "any":
        for tag in state.success_tags:
            if tag in state.seen_success:
                return tag
        return ""
    if all(tag in state.seen_success for tag in state.success_tags):
        return ",".join(state.success_tags)
    return ""


def first_fail_match(state: MonitorState) -> str:
    return state.matched_fail


def kill_process_group(proc: subprocess.Popen[bytes]) -> None:
    """Terminate QEMU and anything it spawned; the child is a session leader."""
    if proc.poll() is not None:
        return
    try:
        os.killpg(os.getpgid(proc.pid), signal.SIGTERM)
    except (ProcessLookupError, PermissionError):
        proc.terminate()
    try:
        proc.wait(timeout=5)
    except subprocess.TimeoutExpired:
        try:
            os.killpg(os.getpgid(proc.pid), signal.SIGKILL)
        except (ProcessLookupError, PermissionError):
            proc.kill()
        proc.wait(timeout=5)


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
    for line in list(state.lines)[-TAIL_LINES:]:
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
    max_log_mib: int = DEFAULT_MAX_LOG_MIB,
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
        max_log_bytes=max_log_mib * 1024 * 1024,
    )

    with log_path.open("ab", buffering=0) as log_fp:
        # Guest serial is QEMU stdout (-serial stdio). Host QEMU chatter
        # (version banner, GTK, warnings) goes to a sibling .qemu-stderr file.
        qemu_err_path = Path(str(log_path) + ".qemu-stderr")
        with qemu_err_path.open("ab", buffering=0) as err_fp:
            proc = subprocess.Popen(
                qemu_cmd,
                stdout=log_fp,
                stderr=err_fp,
                start_new_session=True,
            )

            start = time.monotonic()
            try:
                while True:
                    read_new_log(state)
                    elapsed = time.monotonic() - start

                    if state.offset > state.max_log_bytes:
                        state.reason = "log_too_large"
                        kill_process_group(proc)
                        print_summary(state, elapsed, "FAIL")
                        return 1

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
                        drain_log(state)
                        if success_satisfied(state):
                            state.reason = "qemu_exit_success"
                            state.matched_success = first_success_match(state)
                            print_summary(state, elapsed, "PASS")
                            return 0
                        state.reason = "qemu_exit"
                        state.matched_fail = (
                            list(state.lines)[-1].strip() if state.lines else ""
                        )
                        print_summary(state, elapsed, "FAIL")
                        return 1

                    if elapsed >= timeout_sec:
                        state.reason = "timeout"
                        kill_process_group(proc)
                        print_summary(state, elapsed, "FAIL")
                        return 1

                    if (
                        stale_sec > 0
                        and state.offset > 0
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
        "--expected-panic",
        action="store_true",
        help="Allow panic text for a smoke whose success condition is the panic itself",
    )
    parser.add_argument(
        "--mem",
        default=f"{DEFAULT_MEM_MIB}M",
        help=f"Guest RAM when the QEMU command omits -m (default {DEFAULT_MEM_MIB}M)",
    )
    parser.add_argument(
        "--max-mem-mib",
        type=int,
        default=MAX_MEM_MIB,
        help=f"Hard guest RAM ceiling in MiB (max {MAX_MEM_MIB})",
    )
    parser.add_argument(
        "--allow-low-mem",
        action="store_true",
        help="Skip the available-RAM headroom check (dangerous; CI only)",
    )
    parser.add_argument(
        "--max-log-mib",
        type=int,
        default=DEFAULT_MAX_LOG_MIB,
        help=f"Fail when the serial log exceeds this size (default {DEFAULT_MAX_LOG_MIB})",
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

    if not (1 <= args.timeout <= MAX_TIMEOUT_SEC):
        print(
            f"smoke_autokill.py: --timeout must be 1..{MAX_TIMEOUT_SEC}",
            file=sys.stderr,
        )
        return 2
    if args.stale_sec < 0:
        print("smoke_autokill.py: --stale-sec must be >= 0", file=sys.stderr)
        return 2
    if not (1 <= args.max_mem_mib <= MAX_MEM_MIB):
        print(
            f"smoke_autokill.py: --max-mem-mib must be 1..{MAX_MEM_MIB}",
            file=sys.stderr,
        )
        return 2
    if not (1 <= args.max_log_mib <= 1024):
        print("smoke_autokill.py: --max-log-mib must be 1..1024", file=sys.stderr)
        return 2
    try:
        requested_mib = parse_mem_mib(args.mem)
        qemu_cmd = ensure_qemu_mem(
            qemu_cmd,
            requested_mib=requested_mib,
            max_mib=args.max_mem_mib,
            allow_low_mem=args.allow_low_mem,
        )
    except (ValueError, RuntimeError) as exc:
        print(f"smoke_autokill.py: {exc}", file=sys.stderr)
        return 2

    success_tags = list(args.success_tags)
    success_mode = args.success_mode
    timeout_sec = args.timeout
    stale_sec = args.stale_sec
    default_fail_res = list(DEFAULT_FAIL_RES)
    if args.expected_panic:
        default_fail_res = [
            pattern for pattern in default_fail_res
            if pattern not in (r"KERNEL PANIC", r"panicex\(", r"panic\(")
        ]
    fail_patterns = default_fail_res + list(args.fail_patterns)

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

    if not (1 <= timeout_sec <= MAX_TIMEOUT_SEC):
        print(
            f"smoke_autokill.py: profile timeout must be 1..{MAX_TIMEOUT_SEC}",
            file=sys.stderr,
        )
        return 2
    if stale_sec < 0:
        print(
            "smoke_autokill.py: profile stale-sec must be >= 0",
            file=sys.stderr,
        )
        return 2
    # A stale window longer than the hard timeout can never fire; clamp it
    # instead of rejecting the many targets that only pass --timeout.
    stale_sec = min(stale_sec, timeout_sec)

    return run_smoke(
        log_path=Path(args.log),
        qemu_cmd=qemu_cmd,
        success_tags=success_tags,
        success_mode=success_mode,
        fail_res=fail_patterns,
        timeout_sec=timeout_sec,
        stale_sec=stale_sec,
        max_log_mib=args.max_log_mib,
    )


if __name__ == "__main__":
    sys.exit(main())
