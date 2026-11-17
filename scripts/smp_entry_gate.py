#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""
SMP entry gate: report whether SMP work may start.

Documentation/ai_driven_dev/smp-entry-gate.md records the maintainer's
decision (2026-11-17): SMP work starts only when every criterion below
holds. Each one is reported as PASS, FAIL or UNKNOWN; the exit status is 0
only when all four pass.

1. ledgers   Every debt ledger of scripts/architecture_guard.py is empty
             (DEBT_LEDGER_FILES: non-comment lines; DEBT_LEDGER_SETS:
             entries).
2. uprog-1   No kernel source names a userspace program
             (scripts/smp_entry_gate/userspace_programs.txt), apart from the
             reviewed false positives in userspace_programs_ledger.txt.
3. decisions Interface-review decisions Q1-Q15 are DONE
             (scripts/smp_entry_gate/decisions.tsv), and the table in the
             gate document matches that file.
4. ltp       An LTP syscalls run on x86_64 and arm64 has no panic or hang
             and every non-PASS is triaged
             (scripts/smp_entry_gate/ltp_results.tsv). UNKNOWN until that
             file exists.

Usage:
    python3 scripts/smp_entry_gate.py [--verbose] [--ltp-results FILE]
    python3 scripts/smp_entry_gate.py --sync-doc
"""

from pathlib import Path
import argparse
import importlib.util
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parent.parent
DATA = ROOT / "scripts" / "smp_entry_gate"
DOC = ROOT / "Documentation" / "ai_driven_dev" / "smp-entry-gate.md"
DECISIONS = DATA / "decisions.tsv"
PROGRAMS = DATA / "userspace_programs.txt"
PROGRAM_LEDGER = DATA / "userspace_programs_ledger.txt"
LTP_RESULTS = DATA / "ltp_results.tsv"

# Kernel sources: every tree that builds into a kernel image. In-kernel
# tests, KTM mocks and the copied Linux UAPI headers are not kernel code.
KERNEL_TREES = ("arch", "block", "cpp", "drivers", "fs", "includes", "ipc",
                "kernel", "ktm", "lib", "mm", "net", "rust", "sched", "smp",
                "sound")
NOT_KERNEL = ("kernel/test/", "ktm/mocks/", "includes/uapi/")
SOURCE_SUFFIXES = (".c", ".h", ".S", ".asm", ".cpp", ".hpp", ".rs")

DECISION_IDS = tuple(f"Q{i}" for i in range(1, 16))
DECISION_STATUSES = ("DONE", "PARTIAL", "OPEN")
DOC_BEGIN = "<!-- smp-entry-gate:decisions:begin -->"
DOC_END = "<!-- smp-entry-gate:decisions:end -->"

LTP_ARCHES = ("x86_64", "arm64")
LTP_RESULTS_OK = ("PASS",)
LTP_RESULTS_TRIAGE = ("FAIL", "BROK", "TWARN", "TCONF")
LTP_RESULTS_FATAL = ("PANIC", "HANG")

PASS, FAIL, UNKNOWN = "PASS", "FAIL", "UNKNOWN"


def data_lines(path: Path):
    """Yield (lineno, line) for non-blank, non-comment lines of @path."""
    with path.open(encoding="utf-8") as handle:
        for lineno, raw in enumerate(handle, 1):
            line = raw.rstrip("\n")
            if line.strip() and not line.lstrip().startswith("#"):
                yield lineno, line


def rel(path: Path) -> str:
    try:
        return str(path.relative_to(ROOT))
    except ValueError:
        return str(path)


# --- 1. architecture_guard debt ledgers ------------------------------------

def load_architecture_guard():
    spec = importlib.util.spec_from_file_location(
        "architecture_guard", ROOT / "scripts" / "architecture_guard.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def check_ledgers():
    try:
        guard = load_architecture_guard()
        files = guard.DEBT_LEDGER_FILES
        sets = guard.DEBT_LEDGER_SETS
    except (AttributeError, ImportError, OSError, SyntaxError) as exc:
        return UNKNOWN, f"cannot read the guard's ledger list: {exc}", []
    details = []
    total = 0
    for path in files:
        count = sum(1 for _ in data_lines(path)) if path.exists() else 0
        total += count
        details.append(f"{rel(path)}: {count}")
    for name, entries in sets.items():
        total += len(entries)
        details.append(f"architecture_guard.{name}: {len(entries)}")
    status = PASS if total == 0 else FAIL
    return status, f"{total} ledger entries in {len(details)} ledgers", details


# --- 2. UPROG-1: the kernel never knows a userspace program ----------------

def kernel_sources() -> list[str]:
    try:
        out = subprocess.run(["git", "ls-files", "--", *KERNEL_TREES],
                             cwd=ROOT, check=True, text=True,
                             stdout=subprocess.PIPE).stdout.splitlines()
    except (OSError, subprocess.CalledProcessError):
        out = [rel(p) for tree in KERNEL_TREES
               for p in (ROOT / tree).rglob("*") if p.is_file()]
    return sorted(p for p in out
                  if p.endswith(SOURCE_SUFFIXES) and not p.startswith(NOT_KERNEL))


def load_program_ledger(names: set[str]):
    entries: dict[tuple[str, str], int] = {}
    errors = []
    for lineno, line in data_lines(PROGRAM_LEDGER):
        fields = line.split("#", 1)[0].split()
        if len(fields) != 2 or fields[1].lower() not in names:
            errors.append(f"{rel(PROGRAM_LEDGER)}:{lineno}: expected "
                          "'<path> <listed name>  # why'")
            continue
        key = (fields[0], fields[1].lower())
        if key in entries:
            errors.append(f"{rel(PROGRAM_LEDGER)}:{lineno}: duplicate entry")
        entries[key] = lineno
    return entries, errors


def check_uprog1():
    names = {line.strip().lower() for _, line in data_lines(PROGRAMS)}
    if not names:
        return UNKNOWN, f"{rel(PROGRAMS)} lists no names", []
    ledger, errors = load_program_ledger(names)
    pattern = re.compile(r"\b(" + "|".join(sorted(map(re.escape, names)))
                         + r")\b", re.IGNORECASE)
    hits = []
    covered = set()
    for path in kernel_sources():
        try:
            text = (ROOT / path).read_text(encoding="utf-8", errors="replace")
        except OSError:
            continue
        for lineno, line in enumerate(text.splitlines(), 1):
            for match in pattern.finditer(line):
                name = match.group(1).lower()
                if (path, name) in ledger:
                    covered.add((path, name))
                    continue
                hits.append(f"{path}:{lineno}: {name}: {line.strip()[:100]}")
    for key in sorted(set(ledger) - covered):
        errors.append(f"{rel(PROGRAM_LEDGER)}:{ledger[key]}: stale entry "
                      f"'{key[0]} {key[1]}': no hit left, drop it")
    files = len({hit.split(":", 1)[0] for hit in hits})
    status = PASS if not hits and not errors else FAIL
    summary = f"{len(hits)} userspace-program references in {files} files"
    if errors:
        summary += f", {len(errors)} ledger errors"
    return status, summary, errors + hits


# --- 3. interface-review decisions Q1-Q15 ----------------------------------

def load_decisions():
    rows = []
    errors = []
    seen = set()
    for lineno, line in data_lines(DECISIONS):
        fields = line.split("\t")
        if len(fields) != 4:
            errors.append(f"{rel(DECISIONS)}:{lineno}: expected 4 "
                          "tab-separated fields")
            continue
        qid, status, decision, evidence = (f.strip() for f in fields)
        if qid not in DECISION_IDS or qid in seen:
            errors.append(f"{rel(DECISIONS)}:{lineno}: unknown or duplicate "
                          f"id '{qid}'")
            continue
        if status not in DECISION_STATUSES:
            errors.append(f"{rel(DECISIONS)}:{lineno}: status '{status}' is "
                          f"not one of {', '.join(DECISION_STATUSES)}")
            continue
        seen.add(qid)
        rows.append((qid, status, decision, evidence))
    for qid in DECISION_IDS:
        if qid not in seen:
            errors.append(f"{rel(DECISIONS)}: {qid} is missing")
    rows.sort(key=lambda row: int(row[0][1:]))
    return rows, errors


def render_decisions(rows) -> str:
    lines = [DOC_BEGIN,
             "| Q | Status | Decision | Evidence |",
             "|---|--------|----------|----------|"]
    lines += [f"| {q} | {s} | {d} | {e} |" for q, s, d, e in rows]
    lines.append(DOC_END)
    return "\n".join(lines)


def doc_block(text: str):
    begin = text.find(DOC_BEGIN)
    end = text.find(DOC_END)
    if begin < 0 or end < begin:
        return None
    return begin, end + len(DOC_END)


def check_decisions():
    rows, errors = load_decisions()
    try:
        text = DOC.read_text(encoding="utf-8")
    except OSError:
        text = ""
    span = doc_block(text)
    if span is None:
        errors.append(f"{rel(DOC)}: no decisions block ({DOC_BEGIN})")
    elif text[span[0]:span[1]] != render_decisions(rows):
        errors.append(f"{rel(DOC)}: decisions table differs from "
                      f"{rel(DECISIONS)}; run "
                      "'python3 scripts/smp_entry_gate.py --sync-doc'")
    counts = {s: sum(1 for row in rows if row[1] == s)
              for s in DECISION_STATUSES}
    details = errors + [f"{q} {s}: {e}" for q, s, _, e in rows if s != "DONE"]
    status = PASS if not errors and counts["DONE"] == len(DECISION_IDS) else FAIL
    summary = ", ".join(f"{counts[s]} {s}" for s in DECISION_STATUSES)
    return status, summary, details


def sync_doc() -> int:
    rows, errors = load_decisions()
    if errors:
        for err in errors:
            print(err)
        return 1
    text = DOC.read_text(encoding="utf-8")
    span = doc_block(text)
    if span is None:
        print(f"{rel(DOC)}: no decisions block ({DOC_BEGIN})")
        return 1
    new = text[:span[0]] + render_decisions(rows) + text[span[1]:]
    if new != text:
        DOC.write_text(new, encoding="utf-8")
        print(f"updated {rel(DOC)}")
    else:
        print(f"{rel(DOC)} already in sync")
    return 0


# --- 4. LTP syscalls subset on both ISAs -----------------------------------

def check_ltp(path: Path):
    if not path.exists():
        return UNKNOWN, f"no LTP results yet ({rel(path)})", []
    details = []
    arches = set()
    counts: dict[str, int] = {}
    for lineno, line in data_lines(path):
        fields = line.split("\t")
        if len(fields) < 3:
            details.append(f"{rel(path)}:{lineno}: expected "
                           "'<arch>\\t<test>\\t<result>[\\t<triage>]'")
            continue
        arch, test, result = (f.strip() for f in fields[:3])
        triage = fields[3].strip() if len(fields) > 3 else ""
        if arch not in LTP_ARCHES:
            details.append(f"{rel(path)}:{lineno}: unknown arch '{arch}'")
            continue
        arches.add(arch)
        counts[result] = counts.get(result, 0) + 1
        where = f"{arch} {test} {result}"
        if result in LTP_RESULTS_OK:
            continue
        if result in LTP_RESULTS_FATAL:
            details.append(f"{where}: a panic or hang blocks the gate")
        elif result not in LTP_RESULTS_TRIAGE:
            details.append(f"{where}: unknown result")
        elif triage.startswith("approved:") and len(triage) > 9:
            continue
        elif result == "TCONF":
            details.append(f"{where}: needs a maintainer approval "
                           "('approved:<who, date, why>')")
        elif triage.startswith("red:") and (ROOT / triage[4:]).exists():
            continue
        else:
            details.append(f"{where}: needs a filed red case "
                           "('red:<path>') or 'approved:<...>'")
    for arch in LTP_ARCHES:
        if arch not in arches:
            details.append(f"no results for {arch}")
    summary = ", ".join(f"{n} {r}" for r, n in sorted(counts.items())) \
        or "empty"
    return (PASS if not details else FAIL), summary, details


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--verbose", action="store_true",
                        help="print every finding, not the first ten")
    parser.add_argument("--ltp-results", type=Path, default=LTP_RESULTS,
                        help="LTP results file (default: %(default)s)")
    parser.add_argument("--sync-doc", action="store_true",
                        help="render decisions.tsv into the gate document")
    args = parser.parse_args()
    if args.sync_doc:
        return sync_doc()

    criteria = (
        ("1 ledgers", check_ledgers()),
        ("2 uprog-1", check_uprog1()),
        ("3 decisions", check_decisions()),
        ("4 ltp", check_ltp(args.ltp_results.resolve())),
    )
    limit = None if args.verbose else 10
    for name, (status, summary, details) in criteria:
        print(f"[smp-entry-gate] {name:<12} {status:<8} {summary}")
        shown = details if limit is None else details[:limit]
        for line in shown:
            print(f"    {line}")
        if len(shown) < len(details):
            print(f"    ... {len(details) - len(shown)} more (--verbose)")
    ok = all(status == PASS for _, (status, _, _) in criteria)
    print("[smp-entry-gate] " + ("OPEN: SMP work may start" if ok else
                                 "CLOSED: SMP work waits (SMP-0 and the "
                                 "SMP-3 invariants doc may proceed)"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
