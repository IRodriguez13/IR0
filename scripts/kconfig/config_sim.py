#!/usr/bin/env python3
"""
IR0 config simulation CLI.

Builds a matrix of CONFIG_* on/off combinations for a selected symbol set and
executes a build command for each configuration.
"""

import argparse
import itertools
import os
import subprocess
import sys


KERNEL_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
MENUCONFIG = os.path.join(KERNEL_ROOT, "scripts", "kconfig", "menuconfig.py")

DEFAULT_SYMBOLS = [
    "ENABLE_NETWORKING",
    "ENABLE_SOUND",
    "ENABLE_BLUETOOTH",
    "ENABLE_MOUSE",
    "ENABLE_PC_SPEAKER",
    "ENABLE_STORAGE_ATA",
    "ENABLE_STORAGE_ATA_BLOCK",
    "ENABLE_FS_MINIX",
    "ENABLE_FS_TMPFS",
]


def run(cmd):
    proc = subprocess.run(cmd, cwd=KERNEL_ROOT, text=True)
    return proc.returncode


def parse_args():
    parser = argparse.ArgumentParser(description="Simulate kernel configs and build each variant")
    parser.add_argument(
        "--symbols",
        default=",".join(DEFAULT_SYMBOLS),
        help="Comma-separated boolean symbols without CONFIG_ prefix",
    )
    parser.add_argument(
        "--max-cases",
        type=int,
        default=64,
        help="Maximum number of generated combinations",
    )
    parser.add_argument(
        "--build-cmd",
        default="make -s kernel-x64.bin",
        help="Shell build command executed per configuration",
    )
    parser.add_argument(
        "--runtime-cmd",
        default="",
        help="Optional shell runtime check executed after a successful build",
    )
    return parser.parse_args()


def main():
    args = parse_args()
    symbols = [s.strip() for s in args.symbols.split(",") if s.strip()]
    if not symbols:
        print("No symbols provided", file=sys.stderr)
        return 2

    total = 1 << len(symbols)
    max_cases = min(total, args.max_cases)
    if max_cases <= 0:
        print("max-cases must be > 0", file=sys.stderr)
        return 2

    print(f"[SIM] symbols={symbols}")
    print(f"[SIM] total combinations={total}, executing={max_cases}")

    # Start from defconfig before every run set.
    if run(["make", "defconfig"]) != 0:
        return 1

    build_cmd = ["bash", "-lc", args.build_cmd]
    runtime_cmd = ["bash", "-lc", args.runtime_cmd] if args.runtime_cmd else None
    failed = 0
    executed = 0

    all_combos = list(itertools.product(["n", "y"], repeat=len(symbols)))
    if max_cases == total:
        selected = all_combos
    else:
        selected = []
        seen = set()
        for i in range(max_cases):
            idx = int(round(i * (total - 1) / (max_cases - 1))) if max_cases > 1 else 0
            if idx not in seen:
                selected.append(all_combos[idx])
                seen.add(idx)
        # Fill gaps deterministically if rounding produced duplicates.
        if len(selected) < max_cases:
            for combo in all_combos:
                if combo not in selected:
                    selected.append(combo)
                if len(selected) >= max_cases:
                    break

    for combo in selected:
        set_args = [f"{sym}={val}" for sym, val in zip(symbols, combo)]
        print(f"[SIM] case {executed + 1}/{max_cases}: {' '.join(set_args)}")

        rc = run(["python3", MENUCONFIG, "--set", *set_args])
        if rc != 0:
            failed += 1
            executed += 1
            print("[SIM]   -> failed while applying config")
            continue

        rc = run(build_cmd)
        if rc != 0:
            failed += 1
            print("[SIM]   -> build FAILED")
        elif runtime_cmd:
            rc = run(runtime_cmd)
            if rc != 0:
                failed += 1
                print("[SIM]   -> runtime FAILED")
            else:
                print("[SIM]   -> build OK, runtime OK")
        else:
            print("[SIM]   -> build OK")
        executed += 1

    # Restore default baseline at the end.
    run(["make", "defconfig"])

    print(f"[SIM] completed={executed}, failed={failed}")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
