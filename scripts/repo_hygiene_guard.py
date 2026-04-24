#!/usr/bin/env python3
"""
IR0 repository hygiene guardrails.

Checks:
1) No obviously unnecessary artifacts are tracked.
2) Spanish markdown naming/location is constrained to /esp directories.
"""

from pathlib import Path
import fnmatch
import subprocess
import sys


ROOT = Path(__file__).resolve().parent.parent

FORBIDDEN_TRACKED_PATTERNS = [
    "*.log",
    "*.tmp",
    "*.swp",
    "*.swo",
    ".DS_Store",
    "qemu_debug.log",
]


def git_ls_files():
    proc = subprocess.run(
        ["git", "ls-files"],
        cwd=ROOT,
        text=True,
        capture_output=True,
        check=False,
    )
    if proc.returncode != 0:
        raise RuntimeError(proc.stderr.strip() or "git ls-files failed")
    return [line.strip() for line in proc.stdout.splitlines() if line.strip()]


def is_forbidden_tracked(path: str) -> bool:
    name = Path(path).name
    for pattern in FORBIDDEN_TRACKED_PATTERNS:
        if fnmatch.fnmatch(name, pattern) or fnmatch.fnmatch(path, pattern):
            return True
    return False


def is_spanish_named_markdown(path: str) -> bool:
    p = Path(path)
    if p.suffix.lower() != ".md":
        return False
    stem = p.stem.lower()
    return stem.endswith("_es") or stem.endswith("-es") or stem.startswith("esp_")


def main():
    errors = []

    try:
        tracked = git_ls_files()
    except Exception as exc:
        print(f"[repo-hygiene-guard] FAILED: {exc}")
        return 1

    for rel in tracked:
        if is_forbidden_tracked(rel):
            errors.append(f"[tracked-artifact] {rel}")

        if is_spanish_named_markdown(rel):
            parts = Path(rel).parts
            if "esp" not in parts:
                errors.append(f"[spanish-doc-location] {rel} (must live under an /esp directory)")

    if errors:
        print("[repo-hygiene-guard] FAILED")
        for err in errors:
            print(" -", err)
        return 1

    print("[repo-hygiene-guard] OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
