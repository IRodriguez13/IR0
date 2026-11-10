#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Host contract for the QEMU smoke autokill verdict (scripts/smoke_autokill.py).

Fail patterns count only until the success tags are complete: a selftest
runs as PID 1 and the kernel panics when init returns after its last "ok"
line, often in the same serial flush.
"""

from __future__ import annotations

import contextlib
import io
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))

import smoke_autokill as autokill  # noqa: E402

DONE = "ok 2 last_case"
PANIC = "KERNEL PANIC: Attempted to kill init! exitcode=0x00000000"


def new_state(log: Path) -> autokill.MonitorState:
    return autokill.MonitorState(
        log_path=log,
        success_tags=["ok 1 first_case", DONE],
        success_mode="all",
        fail_res=autokill.compile_patterns(autokill.DEFAULT_FAIL_RES),
        timeout_sec=10,
        stale_sec=0,
        max_log_bytes=1 << 20,
    )


def check(cond: bool, what: str) -> int:
    if cond:
        return 0
    print(f"smoke autokill contract: {what}", file=sys.stderr)
    return 1


def run_guest(tmp: Path, name: str, text: str) -> int:
    """Run a fake guest that prints @text in one write, then idles."""
    guest = (
        "import sys, time\n"
        f"sys.stdout.write({text!r})\n"
        "sys.stdout.flush()\n"
        "time.sleep(30)\n"
    )
    with contextlib.redirect_stdout(io.StringIO()):
        return autokill.run_smoke(
            log_path=tmp / f"{name}.log",
            qemu_cmd=[sys.executable, "-c", guest],
            success_tags=["ok 1 first_case", DONE],
            success_mode="all",
            fail_res=list(autokill.DEFAULT_FAIL_RES),
            timeout_sec=20,
            stale_sec=0,
        )


def main() -> int:
    errors = 0
    with tempfile.TemporaryDirectory(prefix="ir0-autokill-test.") as tmpdir:
        tmp = Path(tmpdir)

        # Panic printed after every success tag, in the same chunk.
        state = new_state(tmp / "a.log")
        autokill.ingest_lines(state, f"ok 1 first_case\n{DONE}\n{PANIC}\n")
        errors += check(autokill.success_satisfied(state), "tags not seen")
        errors += check(
            not autokill.first_fail_match(state),
            "panic after the success tags was judged a failure",
        )

        # Panic printed before the tags are complete still fails.
        state = new_state(tmp / "b.log")
        autokill.ingest_lines(state, f"ok 1 first_case\n{PANIC}\n{DONE}\n")
        errors += check(
            autokill.first_fail_match(state) == PANIC,
            "panic before the last success tag was ignored",
        )

        # A tag split across serial reads completes on the later chunk.
        state = new_state(tmp / "c.log")
        autokill.ingest_lines(state, "ok 1 first_case\nok 2 la")
        errors += check(not autokill.success_satisfied(state), "half tag matched")
        autokill.ingest_lines(state, f"st_case\n{PANIC}\n")
        errors += check(autokill.success_satisfied(state), "split tag missed")
        errors += check(
            not autokill.first_fail_match(state),
            "panic after a split success tag was judged a failure",
        )

        # The whole poll loop: one flush carrying the last tag and the panic.
        errors += check(
            run_guest(tmp, "pass", f"ok 1 first_case\n{DONE}\n{PANIC}\n") == 0,
            "run_smoke failed a guest that panicked after its success tags",
        )
        errors += check(
            run_guest(tmp, "fail", f"ok 1 first_case\n{PANIC}\n{DONE}\n") == 1,
            "run_smoke passed a guest that panicked before its last tag",
        )

    if errors:
        return 1
    print("smoke autokill contract ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
