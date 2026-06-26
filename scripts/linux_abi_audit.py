#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""
linux_abi_audit.py — Linux↔IR0 ground-truth audit orchestrator (D1.19).

Runs paired workloads, host tests, ktests where configured, and emits one report.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts" / "linux_abi"))

from compare import CompareResult, compare_brk, compare_wait4, render_markdown  # noqa: E402


def load_contracts() -> dict:
    path = ROOT / "scripts" / "linux_abi" / "contracts.json"
    return json.loads(path.read_text())


def run_cmd(cmd: list[str], *, cwd: Path | None = None) -> int:
    print(f"  RUN  {' '.join(cmd)}")
    return subprocess.run(cmd, cwd=cwd or ROOT, check=False).returncode


def build_brk_probe(report_dir: Path) -> Path:
    return build_static_probe(
        report_dir / "brk_probe",
        ROOT / "scripts" / "linux_abi" / "workloads" / "brk_probe.c",
    )


def build_wait4_probe(report_dir: Path) -> Path:
    return build_static_probe(
        report_dir / "wait4_probe",
        ROOT / "scripts" / "linux_abi" / "workloads" / "wait4_probe.c",
    )


def build_static_probe(out: Path, src: Path) -> Path:
    out.parent.mkdir(parents=True, exist_ok=True)
    musl_cc = "musl-gcc"
    try:
        subprocess.run(["musl-gcc", "--version"], capture_output=True, check=True)
    except (FileNotFoundError, subprocess.CalledProcessError):
        musl_cc = "gcc"
    rc = run_cmd([musl_cc, "-static", "-Os", "-o", str(out), str(src)])
    if rc != 0 or not out.is_file():
        raise RuntimeError(f"failed to build {out.name} with {musl_cc}")
    return out


def run_host_brk_test() -> bool | None:
    if os.environ.get("LINUX_ABI_SKIP_HOST"):
        return None
    proc = subprocess.run(
        ["make", "-s", "-C", "tests/host", "run"],
        cwd=ROOT,
        capture_output=True,
        text=True,
        check=False,
    )
    combined = proc.stdout + proc.stderr
    m = re.search(r"elf_initial_brk_abi\s*\.\.\.\s*(PASS|FAIL)", combined)
    if m:
        return m.group(1) == "PASS"
    return proc.returncode == 0


def run_ktest_brk(name: str) -> bool | None:
    if os.environ.get("LINUX_ABI_SKIP_KTEST"):
        return None
    rc = run_cmd(["make", "-s", "kernel-tests"])
    log = Path("/tmp/ktest.log")
    if not log.is_file():
        return False if rc != 0 else None
    text = log.read_text(errors="replace")
    if f"[KTEST] {name} ... PASS" in text:
        return True
    if f"[KTEST] {name} ... FAIL" in text:
        return False
    return rc == 0


def audit_brk(report_dir: Path, cfg: dict) -> CompareResult:
    linux_dir = report_dir / "linux" / "brk"
    ir0_dir = report_dir / "ir0" / "brk"
    grow = int(cfg.get("grow_bytes", 4096))
    ktest_log = Path("/tmp/ktest.log")

    build_brk_probe(report_dir)

    sh_linux = ROOT / "scripts" / "linux_abi" / "run_linux_brk.sh"
    sh_ir0 = ROOT / "scripts" / "linux_abi" / "run_ir0_brk.sh"

    host_ok = run_host_brk_test()
    ktest_name = cfg.get("ktest", "brk_post_exec")

    if os.environ.get("LINUX_ABI_SKIP_KTEST"):
        ktest_ok = None
    elif ktest_log.is_file() and "[KTEST] brk_post_exec ... PASS" in ktest_log.read_text(errors="replace"):
        ktest_ok = True
        print(f"  NOTE  reusing brk ktest evidence from {ktest_log}")
    else:
        ktest_ok = None

    if run_cmd(["bash", str(sh_linux), str(linux_dir)]) != 0:
        res = CompareResult(
            contract="brk",
            ok=False,
            divergences=["Linux brk workload script failed"],
        )
        if host_ok is False:
            res.divergences.append("host test elf_initial_brk_abi FAILED")
        return res

    if run_cmd(["bash", str(sh_ir0), str(ir0_dir)]) != 0:
        ir0_trace_path = ir0_dir / "trace.json"
        if ir0_trace_path.is_file():
            ir0_trace = json.loads(ir0_trace_path.read_text())
            if len(ir0_trace.get("audit_steps") or []) >= 2:
                pass
            else:
                res = CompareResult(
                    contract="brk",
                    ok=False,
                    divergences=["IR0 brk workload script failed"],
                )
                if host_ok is False:
                    res.divergences.append("host test elf_initial_brk_abi FAILED")
                return res
        else:
            res = CompareResult(
                contract="brk",
                ok=False,
                divergences=["IR0 brk workload script failed"],
            )
            if host_ok is False:
                res.divergences.append("host test elf_initial_brk_abi FAILED")
            return res

    linux_trace = json.loads((linux_dir / "trace.json").read_text())
    if not (ir0_dir / "trace.json").is_file():
        ir0_trace = {"audit_steps": []}
    else:
        ir0_trace = json.loads((ir0_dir / "trace.json").read_text())
    res = compare_brk(linux_trace, ir0_trace, grow, host_ok, None)

    if ktest_ok is None:
        ktest_ok = run_ktest_brk(ktest_name)
    if ktest_ok is False:
        res.ok = False
        res.divergences.append(f"ktest {ktest_name} FAILED")
    elif ktest_ok is True:
        res.notes.append(f"ktest {ktest_name} OK")

    if host_ok is False:
        res.ok = False
        if "host test elf_initial_brk_abi FAILED" not in res.divergences:
            res.divergences.append("host test elf_initial_brk_abi FAILED")
    elif host_ok is True and "host test elf_initial_brk_abi OK" not in res.notes:
        res.notes.append("host test elf_initial_brk_abi OK")

    return res


def audit_wait4(report_dir: Path, cfg: dict) -> CompareResult:
    linux_dir = report_dir / "linux" / "wait4"
    ir0_dir = report_dir / "ir0" / "wait4"
    child_exit = int(cfg.get("child_exit_status", 42))
    ktest_name = cfg.get("ktest", "wait4_status")
    ktest_log = Path("/tmp/ktest.log")

    build_wait4_probe(report_dir)

    sh_linux = ROOT / "scripts" / "linux_abi" / "run_linux_wait4.sh"
    sh_ir0 = ROOT / "scripts" / "linux_abi" / "run_ir0_wait4.sh"

    if os.environ.get("LINUX_ABI_SKIP_KTEST"):
        ktest_ok = None
    elif ktest_log.is_file() and f"[KTEST] {ktest_name} ... PASS" in ktest_log.read_text(
        errors="replace"
    ):
        ktest_ok = True
        print(f"  NOTE  reusing wait4 ktest evidence from {ktest_log}")
    else:
        ktest_ok = None

    if run_cmd(["bash", str(sh_linux), str(linux_dir)]) != 0:
        return CompareResult(
            contract="wait4",
            ok=False,
            divergences=["Linux wait4 workload script failed"],
        )

    if run_cmd(["bash", str(sh_ir0), str(ir0_dir)]) != 0:
        ir0_trace_path = ir0_dir / "trace.json"
        if ir0_trace_path.is_file():
            ir0_trace = json.loads(ir0_trace_path.read_text())
            if len(ir0_trace.get("audit_steps") or []) < 2:
                return CompareResult(
                    contract="wait4",
                    ok=False,
                    divergences=["IR0 wait4 workload script failed"],
                )
        else:
            return CompareResult(
                contract="wait4",
                ok=False,
                divergences=["IR0 wait4 workload script failed"],
            )

    linux_trace = json.loads((linux_dir / "trace.json").read_text())
    if not (ir0_dir / "trace.json").is_file():
        ir0_trace = {"audit_steps": []}
    else:
        ir0_trace = json.loads((ir0_dir / "trace.json").read_text())

    if ktest_ok is None:
        ktest_ok = run_ktest_brk(ktest_name)
    res = compare_wait4(linux_trace, ir0_trace, child_exit, None)
    if ktest_ok is False:
        res.ok = False
        if f"ktest {ktest_name} FAILED" not in res.divergences:
            res.divergences.append(f"ktest {ktest_name} FAILED")
    elif ktest_ok is True and f"ktest {ktest_name} OK" not in res.notes:
        res.notes.append(f"ktest {ktest_name} OK")

    return res


AUDITORS = {
    "brk": audit_brk,
    "wait4": audit_wait4,
}


def main() -> int:
    ap = argparse.ArgumentParser(description="Linux↔IR0 ABI ground-truth audit")
    ap.add_argument("--contract", action="append", help="Audit one contract (repeatable)")
    ap.add_argument("--all", action="store_true", help="Audit all enabled contracts")
    ap.add_argument(
        "--report-dir",
        type=Path,
        default=ROOT / "build" / "linux_abi_audit",
        help="Output directory",
    )
    args = ap.parse_args()

    cfg = load_contracts()
    report_dir: Path = args.report_dir
    report_dir.mkdir(parents=True, exist_ok=True)

    if args.all:
        ids = [c["id"] for c in cfg["contracts"] if c.get("enabled")]
    elif args.contract:
        ids = args.contract
    else:
        ids = [c["id"] for c in cfg["contracts"] if c.get("enabled")]

    results: list[CompareResult] = []
    for cid in ids:
        contract_cfg = next((c for c in cfg["contracts"] if c["id"] == cid), {})
        if cid not in AUDITORS:
            results.append(
                CompareResult(
                    contract=cid,
                    ok=False,
                    divergences=[f"no automated auditor for '{cid}' yet"],
                )
            )
            continue
        print(f"\n=== ABI audit: {cid} ===")
        results.append(AUDITORS[cid](report_dir, contract_cfg))

    meta = {
        "generated": datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
        "contracts": ids,
    }
    json_path = report_dir / "report.json"
    md_path = report_dir / "report.md"
    json_path.write_text(
        json.dumps(
            {"meta": meta, "results": [r.to_dict() for r in results]},
            indent=2,
        )
        + "\n"
    )
    md_path.write_text(render_markdown(results, meta))

    print(f"\nReport: {md_path}")
    print(f"JSON:   {json_path}")

    return 0 if all(r.ok for r in results) else 1


if __name__ == "__main__":
    sys.exit(main())
