#!/usr/bin/env python3
"""
IR0 repository hygiene guardrails.

Checks:
1) No obviously unnecessary artifacts are tracked.
2) No compiled binaries or build outputs under setup/pid1 or vendored BusyBox.
3) No forbidden agent Co-authored-by trailers in branch history.
4) Spanish markdown naming/location is constrained to /esp directories.
"""

from pathlib import Path
import fnmatch
import re
import subprocess
import sys

FORBIDDEN_AGENT_COAUTHOR_RE = re.compile(
    r"^Co-authored-by:.*<cursoragent@",
    re.IGNORECASE | re.MULTILINE,
)


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


# Paths where compiled artifacts must never be tracked (sources only).
COMPILED_ARTIFACT_PREFIXES = (
    "setup/third-party/busybox-1.36.1/",
    "setup/pid1/fase52_staging/bin/",
    "setup/pid1/fase52_staging/lib/",
)

COMPILED_ARTIFACT_EXACT = {
    "setup/libkconfig_build.a",
    "scripts/kconfig/libkconfig_build.a",
    "setup/pid1/f41true",
    "setup/pid1/fase48_busybox",
    "setup/pid1/fase48_cat",
    "setup/pid1/fase48_echo",
    "setup/pid1/fase50_busybox_real",
    "setup/pid1/fase50_hello",
    "setup/pid1/fase55e_doom_interactive",
    "setup/pid1/fase58c_boot_halt",
    "setup/pid1/fase58c_fbdev",
    "setup/pid1/fase58l_busybox_smoke",
    "setup/pid1/init",
    "setup/pid1/musl_arch_prctl_smoke",
    "setup/pid1/sh_smoke",
    "setup/pid1/userspace_segv",
    "setup/pid1/sbin/irinit",
}

COMPILED_ARTIFACT_SUFFIXES = (
    ".cmd",
    ".a",
)

COMPILED_ARTIFACT_BASENAMES = {
    "busybox",
    "busybox_unstripped",
    "busybox_unstripped.out",
    "applet_tables",
    "usage",
    "usage_pod",
    "docproc",
    "fixdep",
    "split-include",
    "conf",
    "autoconf.h",
    "applet_tables.h",
    "bbconfigopts.h",
    "bbconfigopts_bz2.h",
    "NUM_APPLETS.h",
    "usage_compressed.h",
}


def is_compiled_artifact(path: str) -> bool:
    if path in COMPILED_ARTIFACT_EXACT:
        return True

    name = Path(path).name
    if name in COMPILED_ARTIFACT_BASENAMES:
        if path.startswith(COMPILED_ARTIFACT_PREFIXES[0]):
            return True

    for prefix in COMPILED_ARTIFACT_PREFIXES:
        if not path.startswith(prefix):
            continue
        if prefix in (
            "setup/pid1/fase52_staging/bin/",
            "setup/pid1/fase52_staging/lib/",
        ):
            return True
        if path.endswith(COMPILED_ARTIFACT_SUFFIXES):
            return True
        if name in COMPILED_ARTIFACT_BASENAMES:
            return True
        if "/include/config/" in path and path.startswith(prefix):
            return True

    if path.startswith("setup/pid1/fase52_staging/usr/lib/") and path.endswith(".a"):
        return True

    return False


def is_elf_executable(path: Path) -> bool:
    try:
        data = path.read_bytes()[:4]
    except OSError:
        return False
    return data == b"\x7fELF"


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


def git_head_commits():
    proc = subprocess.run(
        ["git", "log", "HEAD", "--format=%H"],
        cwd=ROOT,
        text=True,
        capture_output=True,
        check=False,
    )
    if proc.returncode != 0:
        raise RuntimeError(proc.stderr.strip() or "git log failed")
    return [line.strip() for line in proc.stdout.splitlines() if line.strip()]


def commits_with_forbidden_agent_coauthor():
    bad = []
    for commit in git_head_commits():
        proc = subprocess.run(
            ["git", "log", "-1", "--format=%B", commit],
            cwd=ROOT,
            text=True,
            capture_output=True,
            check=False,
        )
        if proc.returncode != 0:
            continue
        if FORBIDDEN_AGENT_COAUTHOR_RE.search(proc.stdout):
            oneline = subprocess.run(
                ["git", "log", "-1", "--oneline", commit],
                cwd=ROOT,
                text=True,
                capture_output=True,
                check=False,
            )
            label = oneline.stdout.strip() if oneline.returncode == 0 else commit
            bad.append(label)
    return bad


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

        if is_compiled_artifact(rel):
            errors.append(f"[tracked-compiled] {rel}")

        full = ROOT / rel
        if full.is_file() and is_elf_executable(full):
            if rel.startswith(("setup/pid1/", "setup/third-party/busybox-1.36.1/")):
                errors.append(f"[tracked-elf] {rel}")

        if is_spanish_named_markdown(rel):
            parts = Path(rel).parts
            if "esp" not in parts:
                errors.append(f"[spanish-doc-location] {rel} (must live under an /esp directory)")

    try:
        for label in commits_with_forbidden_agent_coauthor():
            errors.append(f"[agent-coauthor] {label}")
    except Exception as exc:
        errors.append(f"[agent-coauthor-check] {exc}")

    if errors:
        print("[repo-hygiene-guard] FAILED")
        for err in errors:
            print(" -", err)
        return 1

    print("[repo-hygiene-guard] OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
