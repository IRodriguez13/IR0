#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Compare normalized Linux and IR0 ABI audit traces."""

from __future__ import annotations

import json
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any


@dataclass
class CompareResult:
    contract: str
    ok: bool
    divergences: list[str] = field(default_factory=list)
    notes: list[str] = field(default_factory=list)

    def to_dict(self) -> dict[str, Any]:
        return {
            "contract": self.contract,
            "ok": self.ok,
            "divergences": self.divergences,
            "notes": self.notes,
        }


def _load(path: Path) -> dict:
    return json.loads(path.read_text())


def compare_brk(
    linux: dict,
    ir0: dict,
    grow_bytes: int,
    host_test_ok: bool | None,
    ktest_ok: bool | None,
) -> CompareResult:
    res = CompareResult(contract="brk", ok=True)

    l_steps = linux.get("audit_steps") or linux.get("strace_steps") or []
    i_steps = ir0.get("audit_steps") or []

    if len(l_steps) < 2:
        res.ok = False
        res.divergences.append("linux trace has fewer than 2 brk steps")
    if len(i_steps) < 2:
        res.ok = False
        res.divergences.append("ir0 trace has fewer than 2 brk steps")

    if len(l_steps) >= 2 and len(i_steps) >= 2:
        l0, l1 = l_steps[0], l_steps[1]
        i0, i1 = i_steps[0], i_steps[1]

        if l0.get("ret", -1) <= 0:
            res.ok = False
            res.divergences.append(f"linux brk(0) invalid ret={l0.get('ret')}")
        if i0.get("ret", -1) <= 0:
            res.ok = False
            res.divergences.append(f"ir0 brk(0) invalid ret={i0.get('ret')}")

        l_expected = l0.get("ret", 0) + grow_bytes
        i_expected = i0.get("ret", 0) + grow_bytes

        if l1.get("ret") != l_expected:
            res.ok = False
            res.divergences.append(
                f"linux brk grow: expected ret={l_expected} got={l1.get('ret')}"
            )
        if i1.get("ret") != i_expected:
            res.ok = False
            res.divergences.append(
                f"ir0 brk grow: expected ret={i_expected} got={i1.get('ret')}"
            )

        l_delta = l1.get("ret", 0) - l0.get("ret", 0)
        i_delta = i1.get("ret", 0) - i0.get("ret", 0)
        if l_delta != grow_bytes:
            res.ok = False
            res.divergences.append(f"linux brk delta={l_delta} expected={grow_bytes}")
        if i_delta != grow_bytes:
            res.ok = False
            res.divergences.append(f"ir0 brk delta={i_delta} expected={grow_bytes}")
        if l_delta != i_delta:
            res.ok = False
            res.divergences.append(
                f"brk delta mismatch linux={l_delta} ir0={i_delta} (absolute brk may differ by design)"
            )

        res.notes.append(
            f"linux brk0=0x{l0.get('ret', 0):x} -> 0x{l1.get('ret', 0):x} (delta=0x{l_delta:x})"
        )
        res.notes.append(
            f"ir0 brk0=0x{i0.get('ret', 0):x} -> 0x{i1.get('ret', 0):x} (delta=0x{i_delta:x})"
        )

    if host_test_ok is False:
        res.ok = False
        res.divergences.append("host test elf_initial_brk_abi FAILED")
    elif host_test_ok is True:
        res.notes.append("host test elf_initial_brk_abi OK")

    if ktest_ok is False:
        res.ok = False
        res.divergences.append("ktest brk_post_exec FAILED")
    elif ktest_ok is True:
        res.notes.append("ktest brk_post_exec OK")

    return res


def _find_step(steps: list[dict], op: str) -> dict | None:
    for s in steps:
        if s.get("op") == op:
            return s
    return None


def compare_wait4(
    linux: dict,
    ir0: dict,
    child_exit_status: int,
    ktest_ok: bool | None,
) -> CompareResult:
    res = CompareResult(contract="wait4", ok=True)
    expected_status = child_exit_status << 8

    l_steps = linux.get("audit_steps") or linux.get("strace_steps") or []
    i_steps = ir0.get("audit_steps") or []

    if len(l_steps) < 2:
        res.ok = False
        res.divergences.append("linux trace has fewer than 2 wait4 steps")
    if len(i_steps) < 2:
        res.ok = False
        res.divergences.append("ir0 trace has fewer than 2 wait4 steps")

    if len(l_steps) >= 2 and len(i_steps) >= 2:
        l_fork = _find_step(l_steps, "fork")
        l_wait = _find_step(l_steps, "wait4")
        i_fork = _find_step(i_steps, "fork")
        i_wait = _find_step(i_steps, "wait4")

        for label, fork_s, wait_s in (
            ("linux", l_fork, l_wait),
            ("ir0", i_fork, i_wait),
        ):
            if not fork_s or not wait_s:
                res.ok = False
                res.divergences.append(f"{label} trace missing fork or wait4 step")
                continue
            if fork_s.get("ret", -1) <= 0:
                res.ok = False
                res.divergences.append(f"{label} fork invalid ret={fork_s.get('ret')}")
            if wait_s.get("ret", -1) <= 0:
                res.ok = False
                res.divergences.append(f"{label} wait4 invalid ret={wait_s.get('ret')}")
            if wait_s.get("ret") != fork_s.get("ret"):
                res.ok = False
                res.divergences.append(
                    f"{label} wait4 pid mismatch: fork={fork_s.get('ret')} wait={wait_s.get('ret')}"
                )
            got_status = wait_s.get("status")
            if got_status is None:
                res.ok = False
                res.divergences.append(f"{label} wait4 missing status word")
            elif got_status != expected_status:
                res.ok = False
                res.divergences.append(
                    f"{label} wait4 status=0x{got_status:x} expected=0x{expected_status:x} (exit={child_exit_status})"
                )

        if l_fork and l_wait and i_fork and i_wait:
            res.notes.append(
                f"linux fork={l_fork.get('ret')} wait4 status=0x{(l_wait.get('status') or 0):x}"
            )
            res.notes.append(
                f"ir0 fork={i_fork.get('ret')} wait4 status=0x{(i_wait.get('status') or 0):x}"
            )

    if ktest_ok is False:
        res.ok = False
        res.divergences.append("ktest wait4_status FAILED")
    elif ktest_ok is True:
        res.notes.append("ktest wait4_status OK")

    return res


def compare_read(
    linux: dict,
    ir0: dict,
    pipe_read_len: int,
    pipe_data_hex: str,
    ebadf_errno: int,
    ktest_ok: bool | None,
) -> CompareResult:
    res = CompareResult(contract="read", ok=True)

    l_steps = linux.get("audit_steps") or linux.get("strace_steps") or []
    i_steps = ir0.get("audit_steps") or []

    if len(l_steps) < 3:
        res.ok = False
        res.divergences.append("linux trace has fewer than 3 read steps")
    if len(i_steps) < 3:
        res.ok = False
        res.divergences.append("ir0 trace has fewer than 3 read steps")

    checks = (
        ("read_pipe", pipe_read_len, pipe_data_hex, None),
        ("read_eof", 0, None, None),
        ("read_ebadf", -1, None, ebadf_errno),
    )

    for op, exp_ret, exp_hex, exp_errno in checks:
        l_s = _find_step(l_steps, op)
        i_s = _find_step(i_steps, op)
        if not l_s or not i_s:
            res.ok = False
            res.divergences.append(f"missing {op} step (linux={bool(l_s)} ir0={bool(i_s)})")
            continue

        for label, step in (("linux", l_s), ("ir0", i_s)):
            got_ret = step.get("ret")
            if got_ret != exp_ret:
                res.ok = False
                res.divergences.append(
                    f"{label} {op}: ret={got_ret} expected={exp_ret}"
                )
            if exp_hex is not None:
                got_hex = (step.get("data_hex") or "").lower()
                if got_hex != exp_hex.lower():
                    res.ok = False
                    res.divergences.append(
                        f"{label} {op}: data_hex={got_hex} expected={exp_hex}"
                    )
            if exp_errno is not None:
                if step.get("errno") != exp_errno:
                    res.ok = False
                    res.divergences.append(
                        f"{label} {op}: errno={step.get('errno')} expected={exp_errno}"
                    )

        if l_s.get("ret") != i_s.get("ret"):
            res.ok = False
            res.divergences.append(
                f"{op} ret mismatch linux={l_s.get('ret')} ir0={i_s.get('ret')}"
            )
        if exp_hex is not None:
            l_hex = (l_s.get("data_hex") or "").lower()
            i_hex = (i_s.get("data_hex") or "").lower()
            if l_hex != i_hex:
                res.ok = False
                res.divergences.append(
                    f"{op} data mismatch linux={l_hex} ir0={i_hex}"
                )

    l_pipe = _find_step(l_steps, "read_pipe")
    i_pipe = _find_step(i_steps, "read_pipe")
    if l_pipe and i_pipe:
        res.notes.append(
            f"linux read_pipe ret={l_pipe.get('ret')} data={l_pipe.get('data_hex')}"
        )
        res.notes.append(
            f"ir0 read_pipe ret={i_pipe.get('ret')} data={i_pipe.get('data_hex')}"
        )

    if ktest_ok is False:
        res.ok = False
        res.divergences.append("ktest syscall_pipe FAILED")
    elif ktest_ok is True:
        res.notes.append("ktest syscall_pipe OK")

    return res


MMAP_FAILED_RET = 0xFFFFFFFFFFFFFFFF


def _mmap_ok(ret: int | None) -> bool:
    return ret is not None and ret != MMAP_FAILED_RET


def compare_mmap(
    linux: dict,
    ir0: dict,
    page_size: int,
    none_size: int,
    verify_data_hex: str,
    ebadf_errno: int,
    ktest_ok: bool | None,
) -> CompareResult:
    res = CompareResult(contract="mmap", ok=True)

    l_steps = linux.get("audit_steps") or linux.get("strace_steps") or []
    i_steps = ir0.get("audit_steps") or []

    required_ops = (
        "mmap_anon_rw",
        "mmap_verify_rw",
        "mmap_anon_none",
        "mmap_fixed",
        "munmap_rw",
        "mmap_bad_nanon",
        "mmap_bad_fd",
    )

    if len(l_steps) < len(required_ops):
        res.ok = False
        res.divergences.append(
            f"linux trace has {len(l_steps)} mmap steps, need >={len(required_ops)}"
        )
    if len(i_steps) < len(required_ops):
        res.ok = False
        res.divergences.append(
            f"ir0 trace has {len(i_steps)} mmap steps, need >={len(required_ops)}"
        )

    for op in required_ops:
        l_s = _find_step(l_steps, op)
        i_s = _find_step(i_steps, op)
        if not l_s or not i_s:
            res.ok = False
            res.divergences.append(f"missing {op} step (linux={bool(l_s)} ir0={bool(i_s)})")
            continue

        for label, step in (("linux", l_s), ("ir0", i_s)):
            ret = step.get("ret")
            if op in ("mmap_bad_nanon", "mmap_bad_fd"):
                if _mmap_ok(ret):
                    res.ok = False
                    res.divergences.append(f"{label} {op}: expected MAP_FAILED got 0x{ret:x}")
                if step.get("errno") != ebadf_errno:
                    res.ok = False
                    res.divergences.append(
                        f"{label} {op}: errno={step.get('errno')} expected={ebadf_errno}"
                    )
                continue

            if op == "munmap_rw":
                if ret != 0:
                    res.ok = False
                    res.divergences.append(f"{label} {op}: ret=0x{ret:x} expected 0")
                continue

            if not _mmap_ok(ret):
                res.ok = False
                res.divergences.append(f"{label} {op}: MAP_FAILED errno={step.get('errno')}")
                continue

            exp_len = page_size if op != "mmap_anon_none" else none_size
            if step.get("len") != exp_len:
                res.ok = False
                res.divergences.append(
                    f"{label} {op}: len={step.get('len')} expected={exp_len}"
                )

            if op == "mmap_verify_rw":
                got_hex = (step.get("data_hex") or "").lower()
                if got_hex != verify_data_hex.lower():
                    res.ok = False
                    res.divergences.append(
                        f"{label} {op}: data_hex={got_hex} expected={verify_data_hex}"
                    )

            if op == "mmap_fixed":
                req = step.get("req", 0)
                if ret != req:
                    res.ok = False
                    res.divergences.append(
                        f"{label} {op}: ret=0x{ret:x} req=0x{req:x} (MAP_FIXED mismatch)"
                    )

        if op == "mmap_verify_rw":
            l_hex = (l_s.get("data_hex") or "").lower()
            i_hex = (i_s.get("data_hex") or "").lower()
            if l_hex != i_hex:
                res.ok = False
                res.divergences.append(f"{op} data mismatch linux={l_hex} ir0={i_hex}")

        if op in ("mmap_bad_nanon", "mmap_bad_fd"):
            if l_s.get("errno") != i_s.get("errno"):
                res.ok = False
                res.divergences.append(
                    f"{op} errno mismatch linux={l_s.get('errno')} ir0={i_s.get('errno')}"
                )

    l_rw = _find_step(l_steps, "mmap_anon_rw")
    i_rw = _find_step(i_steps, "mmap_anon_rw")
    if l_rw and i_rw and _mmap_ok(l_rw.get("ret")) and _mmap_ok(i_rw.get("ret")):
        res.notes.append(
            f"linux mmap_anon_rw ok len={l_rw.get('len')} (addr not compared)"
        )
        res.notes.append(
            f"ir0 mmap_anon_rw ok len={i_rw.get('len')} ret=0x{i_rw.get('ret'):x}"
        )

    if ktest_ok is False:
        res.ok = False
        res.divergences.append("ktest mmap_null_placement FAILED")
    elif ktest_ok is True:
        res.notes.append("ktest mmap_null_placement OK")

    return res


def render_markdown(results: list[CompareResult], meta: dict) -> str:
    lines = [
        "# Linux ABI audit report",
        "",
        f"> Generated: {meta.get('generated', 'unknown')}",
        f"> Contracts run: {', '.join(meta.get('contracts', []))}",
        "",
    ]

    for r in results:
        status = "PASS" if r.ok else "FAIL"
        lines.append(f"## {r.contract} — {status}")
        lines.append("")
        if r.notes:
            lines.append("Notes:")
            for n in r.notes:
                lines.append(f"- {n}")
            lines.append("")
        if r.divergences:
            lines.append("First divergences:")
            for d in r.divergences:
                lines.append(f"- {d}")
            lines.append("")
        lines.append("---")
        lines.append("")

    overall = all(r.ok for r in results)
    lines.append(f"## Overall: {'PASS' if overall else 'FAIL'}")
    lines.append("")
    return "\n".join(lines)
