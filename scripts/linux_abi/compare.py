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
