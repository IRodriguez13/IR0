#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Host portability and safety guard for KTM tooling.

The KTM test plane runs on the developer host, so its scripts must stay
portable across Linux, BSD, and macOS and must never risk host RAM, disk, or
processes. See
Documentation/ai_driven_dev/rules/ir0-ktm-host-portability-safety.md.

The guard has two halves:

  * a static scan for banned GNU-only flags, insecure temp files, hardcoded
    host paths, and unbounded QEMU invocations;
  * a self-test of the shared host-safety helpers (memory caps, streaming log
    scan, secure temp disks).

Existing violations are frozen in scripts/host_portability_baseline.txt. The
ledger is a ratchet in both directions: a new violation fails, and a fixed
violation must shrink the ledger.
"""

from __future__ import annotations

import argparse
import contextlib
import io
import os
import re
import stat
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BASELINE = ROOT / "scripts" / "host_portability_baseline.txt"
MAKE_DIR = ROOT / "scripts" / "make"
MAKE_FILES = (ROOT / "Makefile", *sorted(MAKE_DIR.glob("*.mk")))

sys.path.insert(0, str(Path(__file__).resolve().parent))

import ktm_host_safety as safety  # noqa: E402
import smoke_autokill as smoke  # noqa: E402
import config_set_line as config_set  # noqa: E402

SCOPE_GLOBS = (
    "Makefile",
    "scripts/config_set_line.py",
    "scripts/ktm_*.py",
    "scripts/ktm_*.sh",
    "scripts/make/*.mk",
    "scripts/smoke_*.py",
    "scripts/smoke_*.sh",
    "scripts/inject_init_minix.py",
    "tests/ktm/**/*.py",
    "tests/ktm/**/*.sh",
)

# (id, regex, explanation). Keep the regexes narrow: false positives make the
# ledger noisy and hide real regressions.
PATTERNS: tuple[tuple[str, str, str], ...] = (
    (
        "py_tempfile_mktemp",
        r"tempfile\.mktemp\(",
        "tempfile.mktemp is insecure; use mkstemp/mkdtemp",
    ),
    (
        "py_shell_true",
        r"shell\s*=\s*True",
        "subprocess shell=True with data; pass a list argv",
    ),
    (
        "py_hardcoded_python3",
        r"""["']python3["']""",
        "hardcoded python3; use sys.executable",
    ),
    (
        "hardcoded_tmp",
        r"/tmp/",
        "hardcoded /tmp; use ${TMPDIR:-/tmp} or tempfile.gettempdir()",
    ),
    (
        "unbounded_log_read",
        r"\blog\.read_text\(",
        "read logs through read_log_text (64 MiB cap) or iter_log_lines",
    ),
    (
        "gnu_sed_i",
        r"\bsed\s+-i",
        "sed -i is GNU-only; write a temp file and mv, or use Python",
    ),
    (
        "gnu_readlink_f",
        r"\breadlink\s+-f",
        "readlink -f is GNU-only; use cd + pwd -P or Path.resolve()",
    ),
    (
        "gnu_stat_c",
        r"\bstat\s+-c",
        "stat -c is GNU-only; use Python stat() or a stat -f fallback",
    ),
    (
        "gnu_nproc",
        r"\bnproc\b",
        "nproc is GNU-only; use getconf _NPROCESSORS_ONLN or sysctl",
    ),
    (
        "gnu_free",
        r"\bfree\s+-",
        "free is Linux-only; read /proc/meminfo or os.sysconf",
    ),
    (
        "gnu_checksum",
        r"\b(?:sha256sum|md5sum)\b",
        "sha256sum/md5sum are GNU-only; use shasum -a 256 or openssl",
    ),
    (
        "gnu_find_printf",
        r"-printf\b",
        "find -printf is GNU-only; use find -print and parse in Python",
    ),
    (
        "gnu_grep_p",
        r"\bgrep\s+-P\b",
        "grep -P is GNU-only; use grep -E",
    ),
    (
        "gnu_reflink",
        r"--reflink\b",
        "cp --reflink is GNU-only; fall back to plain cp",
    ),
    (
        "mktemp_suffix",
        r"mktemp[^\n]*XXXXXX\.",
        "mktemp template must end in X's; BSD mktemp rejects suffixes",
    ),
    (
        "bash4_arrays",
        r"\b(?:declare\s+-A|mapfile|readarray)\b",
        "bash 4 only; macOS ships bash 3.2 — use POSIX sh or Python",
    ),
)

COMPILED = tuple(
    (name, re.compile(pattern), why) for name, pattern, why in PATTERNS
)
WHY = {name: why for name, _pattern, why in PATTERNS}

# Helpers that implement the portable replacement necessarily name the
# GNU-only construct they abstract. Keep this list tiny and explicit; every
# other occurrence must be fixed or frozen in the ledger.
HELPER_ALLOW = {
    ("gnu_reflink", "scripts/make/host-portable.mk"),
    ("gnu_sed_i", "scripts/config_set_line.py"),
}

# A real launch passes a device/disk/serial option right after the binary.
# `qemu -display help` is a capability probe: it never starts a guest.
QEMU_DIRECT_RE = re.compile(
    r"(?:\$\(QEMU\)|qemu-system-[a-z0-9_]+)\s+-(?!display\s+help)"
)
WRAPPER_NAMES = (
    "SMOKE_QEMU_RUN",
    "smoke_qemu_run.sh",
    "smoke_autokill.py",
    "ktm_userdev_runner.py",
    "ktm_runner.py",
)


def rel(path: Path) -> str:
    return path.relative_to(ROOT).as_posix()


def scoped_files() -> list[Path]:
    seen: set[Path] = set()
    for pattern in SCOPE_GLOBS:
        for path in ROOT.glob(pattern):
            if path.is_file():
                seen.add(path)
    return sorted(seen)


def code_lines(text: str):
    """Yield (lineno, line) for lines that are not pure comments."""
    for lineno, line in enumerate(text.splitlines(), 1):
        if line.lstrip().startswith("#"):
            continue
        yield lineno, line


def scan_file(path: Path) -> dict[str, list[int]]:
    found: dict[str, list[int]] = {}
    text = path.read_text(encoding="utf-8", errors="replace")
    for lineno, line in code_lines(text):
        for name, regex, _why in COMPILED:
            if (name, rel(path)) in HELPER_ALLOW:
                continue
            if regex.search(line):
                found.setdefault(name, []).append(lineno)
    return found


def current_violations() -> dict[tuple[str, str], list[int]]:
    violations: dict[tuple[str, str], list[int]] = {}
    for path in scoped_files():
        for name, lines in scan_file(path).items():
            violations[(name, rel(path))] = lines
    return violations


def load_baseline() -> dict[tuple[str, str], int]:
    entries: dict[tuple[str, str], int] = {}
    if not BASELINE.exists():
        return entries
    for raw in BASELINE.read_text(encoding="utf-8").splitlines():
        line = raw.split("#", 1)[0].strip()
        if not line:
            continue
        parts = line.split()
        if len(parts) != 3 or not parts[2].isdigit():
            raise ValueError(f"bad baseline line: {raw!r}")
        entries[(parts[0], parts[1])] = int(parts[2])
    return entries


def qemu_policy_errors() -> list[str]:
    """Every QEMU invocation must be memory-capped or go through a wrapper."""
    errors: list[str] = []
    for path in MAKE_FILES:
        if not path.exists():
            errors.append(f"{rel(path)}: missing")
            continue
        lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
        block: list[tuple[int, str]] = []

        def check(items: list[tuple[int, str]]) -> None:
            body = "\n".join(line for _lineno, line in items)
            direct = [
                lineno
                for lineno, line in items
                if QEMU_DIRECT_RE.search(line)
            ]
            if not direct:
                return
            wrapped = any(name in body for name in WRAPPER_NAMES)
            capped = re.search(r"(?:^|\s)-m\s+\S+", body) is not None
            if not wrapped and not capped:
                errors.append(
                    f"{rel(path)}:{direct[0]}: QEMU runs without a wrapper "
                    "or an explicit -m; use $(SMOKE_QEMU_RUN) or add -m"
                )

        for lineno, line in enumerate(lines, 1):
            if line.startswith("\t"):
                block.append((lineno, line))
                continue
            if block:
                check(block)
                block = []
        if block:
            check(block)
    return errors


def self_test_errors() -> list[str]:
    errors: list[str] = []

    def expect(condition: bool, message: str) -> None:
        if not condition:
            errors.append(f"self-test: {message}")

    try:
        expect(safety.parse_mem_mib("128M") == 128, "parse 128M")
        expect(safety.parse_mem_mib("1G") == 1024, "parse 1G")
        expect(safety.parse_mem_mib("64") == 64, "parse bare MiB")
        try:
            safety.parse_mem_mib("8")
            errors.append("self-test: 8 MiB should be below the floor")
        except ValueError:
            pass

        injected = safety.ensure_qemu_mem(["qemu"], requested_mib=128)
        expect(injected[-2:] == ["-m", "128M"], "inject default -m")
        try:
            safety.ensure_qemu_mem(["qemu", "-m", "8G"])
            errors.append("self-test: 8G guest was not rejected")
        except ValueError:
            pass

        original_available = safety.host_available_mib
        safety.host_available_mib = lambda: 256
        try:
            try:
                safety.ensure_qemu_mem(["qemu"], requested_mib=128)
                errors.append("self-test: low-memory headroom was not rejected")
            except RuntimeError:
                pass
            expect(
                safety.ensure_qemu_mem(
                    ["qemu"], requested_mib=128, allow_low_mem=True
                )[-2:]
                == ["-m", "128M"],
                "allow-low-mem escape hatch",
            )
        finally:
            safety.host_available_mib = original_available

        with tempfile.TemporaryDirectory(prefix="ir0-guard-config-") as tmp:
            config = Path(tmp) / ".config"
            config.write_text("CONFIG_A=1\n", encoding="utf-8")
            expect(
                config_set.set_line(config, r"^CONFIG_A=.*", "CONFIG_A=2"),
                "config_set_line rewrites an existing assignment",
            )
            expect(
                config.read_text(encoding="utf-8") == "CONFIG_A=2\n",
                "config_set_line wrote the replacement",
            )
            expect(
                config_set.rewrite("X=1\n", r"^Y=", "Y=2") == "X=1\nY=2\n",
                "config_set_line appends a missing assignment",
            )
            expect(
                config_set.rewrite(
                    "CONFIG_ECHO=y\n",
                    r"^CONFIG_ECHO=|^# CONFIG_ECHO is not set",
                    "CONFIG_ECHO=y",
                )
                == "CONFIG_ECHO=y\n",
                "config_set_line does not duplicate an existing assignment",
            )

        disk = safety.make_temp_disk(prefix="ir0-guard-selftest-")
        try:
            mode = stat.S_IMODE(disk.stat().st_mode)
            expect(disk.is_file(), "temp disk exists")
            expect(mode == 0o600, f"temp disk mode is {oct(mode)}, want 0o600")
        finally:
            disk.unlink(missing_ok=True)

        with tempfile.TemporaryDirectory(prefix="ir0-guard-log-") as tmp:
            log = Path(tmp) / "serial.log"
            tag = "TEST_END|guard|PASS"
            # Split the tag across the 1 MiB chunk boundary.
            log.write_bytes(b"a" * (safety.LOG_CHUNK - 3) + b"\n" + tag.encode() + b"\n")
            found, _matches = safety.scan_log(log, [tag])
            expect(tag in found, "scan_log finds a tag split across chunks")
            lines = list(safety.iter_log_lines(log))
            expect(lines[-1] == tag, "iter_log_lines keeps a split line intact")
            found_limited, _ = safety.scan_log(log, [tag], max_bytes=1024)
            expect(tag not in found_limited, "scan_log honors max_bytes")

        with tempfile.TemporaryDirectory(prefix="ir0-guard-drain-") as tmp:
            # A guest that floods serial and exits must not be drained past
            # the cap; a tag beyond the cap must not turn the run into PASS.
            log = Path(tmp) / "serial.log"
            tag = "GUARD_DRAIN_OK"
            log.write_bytes(
                b"x" * (2 * smoke.READ_CHUNK) + b"\n" + tag.encode() + b"\n"
            )
            state = smoke.MonitorState(
                log_path=log,
                success_tags=[tag],
                success_mode="all",
                fail_res=[],
                timeout_sec=1,
                stale_sec=0,
                max_log_bytes=smoke.READ_CHUNK,
            )
            smoke.drain_log(state)
            expect(
                state.offset <= state.max_log_bytes + smoke.READ_CHUNK,
                "drain_log stops at max_log_bytes",
            )
            expect(
                not smoke.success_satisfied(state),
                "drain_log does not read a success tag past the cap",
            )

            with contextlib.redirect_stdout(io.StringIO()):
                rc = smoke.finish_after_exit(state, 0.0)
            expect(rc == 1, "finish_after_exit fails an oversized log")
            expect(
                state.reason == "log_too_large",
                f"oversized exit reason is {state.reason!r}",
            )

        with tempfile.TemporaryDirectory(prefix="ir0-guard-exit-") as tmp:
            tag = "GUARD_EXIT_OK"
            for payload, want_rc, want_reason in (
                (b"boot\n" + tag.encode() + b"\n", 0, "qemu_exit_success"),
                (b"boot\n", 1, "qemu_exit"),
            ):
                log = Path(tmp) / "serial.log"
                log.write_bytes(payload)
                state = smoke.MonitorState(
                    log_path=log,
                    success_tags=[tag],
                    success_mode="all",
                    fail_res=[],
                    timeout_sec=1,
                    stale_sec=0,
                    max_log_bytes=1024,
                )
                with contextlib.redirect_stdout(io.StringIO()):
                    rc = smoke.finish_after_exit(state, 0.0)
                expect(
                    rc == want_rc,
                    f"finish_after_exit rc={rc}, want {want_rc}",
                )
                expect(
                    state.reason == want_reason,
                    f"finish_after_exit reason={state.reason!r}, "
                    f"want {want_reason!r}",
                )
    except Exception as exc:  # noqa: BLE001 - a broken helper is a hard failure
        errors.append(f"self-test crashed: {exc!r}")
    return errors


def main() -> int:
    parser = argparse.ArgumentParser(description="Host portability guard")
    parser.add_argument("--list", action="store_true", help="print violations")
    parser.add_argument(
        "--print-baseline",
        action="store_true",
        help="print the ledger body for the current tree",
    )
    args = parser.parse_args()

    violations = current_violations()
    baseline = load_baseline()

    if args.print_baseline:
        for (name, path), lines in sorted(violations.items()):
            print(f"{name:24} {path:48} {len(lines)}")
        return 0

    if args.list:
        for (name, path), lines in sorted(violations.items()):
            print(f"{name:24} {path:48} {len(lines):3}  lines={lines[:8]}")
        return 0

    errors: list[str] = []
    actual = {(name, path): len(lines) for (name, path), lines in violations.items()}

    for (name, path), lines in sorted(violations.items()):
        key = (name, path)
        if key not in baseline:
            errors.append(
                f"{path}: new {name} violation at lines {lines[:8]} — {WHY[name]}"
            )
        elif len(lines) > baseline[key]:
            errors.append(
                f"{path}: {name} grew from {baseline[key]} to {len(lines)} "
                f"(lines {lines[:8]})"
            )
        elif len(lines) < baseline[key]:
            errors.append(
                f"{path}: {name} shrank from {baseline[key]} to {len(lines)} — "
                "shrink scripts/host_portability_baseline.txt"
            )

    for key in sorted(set(baseline) - set(actual)):
        errors.append(
            f"{key[1]}: stale baseline entry for {key[0]} — remove it"
        )

    errors.extend(qemu_policy_errors())
    errors.extend(self_test_errors())

    if errors:
        print("[host-portability-guard] FAILED")
        for err in errors:
            print(" -", err)
        return 1

    print(
        "[host-portability-guard] OK "
        f"(files={len(scoped_files())}, baselined={len(baseline)}, "
        "qemu_policy=ok, self_test=ok)"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
