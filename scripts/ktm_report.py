#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""
KTM — unified post-mortem report for a QEMU serial log.

Runs syscall manifest (tier-1 gaps) and log classification.
"""

from __future__ import annotations

import argparse
import subprocess
import sys
import tempfile
from collections import deque
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(Path(__file__).resolve().parent))

from ktm_host_safety import iter_log_lines  # noqa: E402

DEFAULT_LOG = Path(tempfile.gettempdir()) / "runit-ash-smoke.log"


def main() -> int:
    parser = argparse.ArgumentParser(description="KTM unified smoke/QEMU log report")
    parser.add_argument("log", nargs="?", type=Path,
                          help=f"serial log (default: {DEFAULT_LOG.name})")
    parser.add_argument("--no-manifest", action="store_true")
    args = parser.parse_args()

    log = args.log or DEFAULT_LOG

    print("KTM_REPORT")
    print(f"  log={log}")

    if not args.no_manifest:
        print("\n--- syscall manifest (tier-1) ---")
        manifest = ROOT / "scripts" / "ktm_syscall_manifest.py"
        rc = subprocess.run(
            [sys.executable, str(manifest), "--tier1"],
            cwd=ROOT,
            check=False,
            timeout=120,
        )
        if rc.returncode != 0:
            print("  (tier-1 gaps listed above — informational for T1 roadmap)")

    print("\n--- log classification ---")
    classify = ROOT / "scripts" / "ktm_log_classify.py"
    if log.is_file():
        subprocess.run(
            [sys.executable, str(classify), str(log)],
            check=False,
            timeout=120,
        )
    else:
        print(f"  log missing: {log}")
        return 1

    # KTM event summary (streamed; a huge log must not exhaust host RAM).
    if log.is_file():
        ev: deque[str] = deque(maxlen=8)
        panic: deque[str] = deque(maxlen=4)
        ev_count = panic_count = ctx_count = 0
        for raw in iter_log_lines(log):
            if "[KTM][EV]" in raw:
                ev_count += 1
                ev.append(raw.strip())
            elif "[KTM][PANIC_CLASS]" in raw:
                panic_count += 1
                panic.append(raw.strip())
            elif "[KTM][CTX]" in raw:
                ctx_count += 1
        print("\n--- KTM serial anchors ---")
        print(
            f"  events={ev_count} panic_class={panic_count} "
            f"ctx_snapshots={ctx_count}"
        )
        for ln in ev:
            print(f"    {ln}")
        for ln in panic:
            print(f"    {ln}")

    return 0


if __name__ == "__main__":
    sys.exit(main())
