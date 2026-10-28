#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""KTM adoption ratchet.

KTM is the canonical test plane (Documentation/KTM.md §8). This guard keeps new
in-guest and in-kernel harnesses from being added as serial-only smokes, and it
keeps the legacy ledger shrinking.

Scope:

  setup/pid1/*_{smoke,probe,stress}.c   userspace harnesses run as PID1/payload
  kernel/test/test_*.c                  in-kernel ktest cases and runner

A harness counts as KTM when it reports through the KTM plane: it includes
libktm_user.h / the KTM uapi, calls the /dev/ktm ioctl helpers, or registers an
in-kernel KTM scenario. Everything else is legacy and must appear in
scripts/ktm_legacy_baseline.txt.

The ledger is a ratchet in both directions:

  * a legacy harness that is not listed  -> fail (new serial-only test)
  * a listed entry that is gone/now KTM  -> fail (stale ledger, shrink it)

That way the baseline can only be reduced, never grown silently. Run with
--list to print the current classification.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BASELINE = ROOT / "scripts" / "ktm_legacy_baseline.txt"

PID1_TEST_SUFFIXES = ("_smoke.c", "_probe.c", "_stress.c")
KERNEL_TEST_GLOB = "test_*.c"

MAIN_RE = re.compile(r"\b(?:int|void)\s+main\s*\(")
KTM_USER_RE = re.compile(
    r'libktm_user\.h|<ir0/ktm/uapi\.h>|\bktm_(?:open|close|case_begin|case_end|'
    r"checkpoint|hostshare_report|run_scenario|assert_|config_fault|reset)\b"
)
KTM_SCENARIO_RE = re.compile(r"\bktm_scenario_t\b|\bktm_scenario_register\w*\s*\(")


def rel(path: Path) -> str:
    return path.relative_to(ROOT).as_posix()


def classify_pid1(path: Path) -> str:
    text = path.read_text(encoding="utf-8", errors="replace")
    if not MAIN_RE.search(text):
        return "helper"
    if KTM_USER_RE.search(text):
        return "ktm"
    return "legacy"


def classify_kernel_test(path: Path) -> str:
    text = path.read_text(encoding="utf-8", errors="replace")
    if KTM_SCENARIO_RE.search(text):
        return "ktm"
    return "legacy"


def current_legacy() -> dict[str, str]:
    """Return {path: reason} for every legacy harness in scope."""
    legacy: dict[str, str] = {}
    for path in sorted((ROOT / "setup" / "pid1").glob("*.c")):
        if not path.name.endswith(PID1_TEST_SUFFIXES):
            continue
        if classify_pid1(path) == "legacy":
            legacy[rel(path)] = "serial-only PID1 smoke"
    for path in sorted((ROOT / "kernel" / "test").glob(KERNEL_TEST_GLOB)):
        if classify_kernel_test(path) == "legacy":
            legacy[rel(path)] = "legacy in-kernel ktest (KTAP)"
    return legacy


def load_baseline() -> dict[str, str]:
    entries: dict[str, str] = {}
    if not BASELINE.exists():
        return entries
    for raw in BASELINE.read_text(encoding="utf-8").splitlines():
        line = raw.split("#", 1)[0].strip()
        if not line:
            continue
        parts = line.split(None, 1)
        entries[parts[0]] = parts[1] if len(parts) > 1 else ""
    return entries


def check_ktm_artifacts(errors: list[str]) -> tuple[int, int]:
    """KTM pilots and scenarios must actually use the KTM plane."""
    pilots = 0
    for path in sorted((ROOT / "tests" / "ktm" / "userdev").glob("*.c")):
        pilots += 1
        text = path.read_text(encoding="utf-8", errors="replace")
        if "libktm_user.h" not in text:
            errors.append(
                f"{rel(path)}: KTM userdev pilot does not include libktm_user.h"
            )
    scenarios = 0
    for path in sorted((ROOT / "tests" / "ktm" / "scenarios").glob("*.c")):
        scenarios += 1
        text = path.read_text(encoding="utf-8", errors="replace")
        if not KTM_SCENARIO_RE.search(text):
            errors.append(
                f"{rel(path)}: scenario file does not register a ktm_scenario_t"
            )
    return pilots, scenarios


def main() -> int:
    parser = argparse.ArgumentParser(description="KTM adoption ratchet")
    parser.add_argument("--list", action="store_true", help="print classification")
    parser.add_argument(
        "--print-baseline",
        action="store_true",
        help="print the ledger body for the current tree",
    )
    args = parser.parse_args()

    legacy = current_legacy()
    baseline = load_baseline()

    if args.print_baseline:
        for path in sorted(legacy):
            print(path)
        return 0

    if args.list:
        for path, reason in sorted(legacy.items()):
            print(f"legacy  {path:48s} {reason}")
        for path in sorted(set(baseline) - set(legacy)):
            print(f"stale   {path}")
        return 0

    errors: list[str] = []
    new = sorted(set(legacy) - set(baseline))
    stale = sorted(set(baseline) - set(legacy))

    for path in new:
        errors.append(
            f"{path}: new serial-only harness — report through libktm_user / a KTM "
            "scenario, or add it to scripts/ktm_legacy_baseline.txt with a reason"
        )
    for path in stale:
        if not (ROOT / path).exists():
            errors.append(f"{path}: ledger entry is gone — remove it from the baseline")
        else:
            errors.append(
                f"{path}: ledger entry is no longer legacy — remove it from the baseline"
            )

    pilots, scenarios = check_ktm_artifacts(errors)

    if errors:
        print("[ktm-adoption-guard] FAILED")
        for err in errors:
            print(" -", err)
        return 1

    print(
        f"[ktm-adoption-guard] OK "
        f"(legacy={len(legacy)} baselined, userdev={pilots}, scenarios={scenarios})"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
