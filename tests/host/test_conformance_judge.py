#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Host contract for scripts/ir0_conformance.py result classification."""

from __future__ import annotations

import contextlib
import io
import shutil
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))

import ir0_conformance as conf  # noqa: E402

CASES_H = "CONF_CASE(a_pass)\nCONF_CASE(b_known_fail)\nCONF_CASE(c_new_fail)\n" \
    "CONF_CASE(d_tconf)\nCONF_CASE(e_fixed)\nCONF_CASE(f_never_ran)\n"
LOG = """[boot] noise
KTAP version 1
1..6
ok 1 a_pass
not ok 2 b_known_fail
# at line 3: S0(SYS_x)
not ok 3 c_new_fail
ok 4 d_tconf # SKIP TCONF
ok 5 e_fixed
[kernel] KERNEL PANIC: test
"""


def judge(tmp: Path, baseline: str, log: str = LOG) -> tuple[int, str]:
    (tmp / "cases.h").write_text(CASES_H)
    (tmp / "baseline.txt").write_text(baseline)
    (tmp / "guest.log").write_text(log)
    out = io.StringIO()
    with contextlib.redirect_stdout(out), contextlib.redirect_stderr(out):
        rc = conf.judge_conformance(tmp / "guest.log", tmp / "baseline.txt",
                                    "x86_64", tmp / "s.json", False,
                                    tmp / "cases.h")
    return rc, out.getvalue()


def main() -> int:
    tmp = Path(tempfile.mkdtemp(prefix="ir0-conf-judge."))
    try:
        base = ("b_known_fail FAIL  # kernel bug\nd_tconf TCONF\n"
                "e_fixed FAIL\nf_never_ran BROK\n")
        rc, out = judge(tmp, base)
        if rc == 0 or "REGRESSION c_new_fail" not in out:
            print("a new failure was not a regression:\n" + out, file=sys.stderr)
            return 1
        if "REGRESSION b_known_fail" in out or "FIXED e_fixed" not in out:
            print("baseline handling is wrong:\n" + out, file=sys.stderr)
            return 1
        if "guest never printed" not in out:
            print("a missing done tag was accepted:\n" + out, file=sys.stderr)
            return 1
        done_log = LOG + "not ok 6 f_never_ran # ERROR BROK signal=11\n" + conf.CONF_DONE + "\n"
        rc, out = judge(tmp, base + "c_new_fail FAIL\n", done_log)
        if rc != 0:
            print("a run matching its baseline failed:\n" + out, file=sys.stderr)
            return 1
        rc, out = judge(tmp, base + "c_new_fail FAIL\n",
                        done_log.replace("[kernel] KERNEL PANIC: test\n", "")
                        + "KERNEL PANIC: init exited\n")
        if rc != 0:
            print("a panic after the done tag failed the run:\n" + out, file=sys.stderr)
            return 1
        rc, out = judge(tmp, base + "c_new_fail FAIL\nz_gone FAIL\n", done_log)
        if rc == 0 or "STALE" not in out:
            print("a stale baseline entry was accepted:\n" + out, file=sys.stderr)
            return 1
        fuzz_ok = ("# batch 1 calls=100 child=exit:0\n" + conf.FUZZ_DONE
                   + "\nKERNEL PANIC: init exited\n")
        (tmp / "f.log").write_text(fuzz_ok)
        with contextlib.redirect_stdout(io.StringIO()):
            if conf.judge_fuzz(tmp / "f.log", "arm64", None) != 0:
                print("a healthy fuzz run failed", file=sys.stderr)
                return 1
        (tmp / "f.log").write_text("# batch 1 calls=3 child=hung\n" + conf.FUZZ_LOST
                                   + " reason=pipe batch=1\n")
        with contextlib.redirect_stdout(io.StringIO()), \
                contextlib.redirect_stderr(io.StringIO()):
            if conf.judge_fuzz(tmp / "f.log", "arm64", None) == 0:
                print("a lost-health fuzz run passed", file=sys.stderr)
                return 1
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    print("test_conformance_judge: ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
