#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Atomically set lines in Kconfig-style configuration files.

`sed -i` is GNU-only: BSD/macOS sed needs `sed -i ''` and silently differs in
other flags. This helper replaces every line matching PATTERN with
REPLACEMENT and appends REPLACEMENT when nothing matched. The rewrite goes
through a sibling temporary file and os.replace(), so an interrupted build
cannot leave a truncated .config behind.

Usage:
    config_set_line.py FILE PATTERN REPLACEMENT
    config_set_line.py --self-test
"""

from __future__ import annotations

import argparse
import contextlib
import os
import re
import stat
import sys
import tempfile
from pathlib import Path


def rewrite(text: str, pattern: str, replacement: str) -> str:
    """Return text with every PATTERN match replaced, or appended if absent."""
    if not pattern:
        raise ValueError("empty pattern")
    if "\n" in replacement:
        raise ValueError("replacement must be a single line")
    regex = re.compile(pattern)
    out: list[str] = []
    matched = False
    for line in text.splitlines():
        if regex.search(line):
            out.append(replacement)
            matched = True
        else:
            out.append(line)
    if not matched:
        out.append(replacement)
    return "\n".join(out) + "\n"


def set_line(path: Path, pattern: str, replacement: str) -> bool:
    """Atomically rewrite path. Return True when the file changed."""
    original = path.read_text(encoding="utf-8")
    updated = rewrite(original, pattern, replacement)
    if updated == original:
        return False

    mode = stat.S_IMODE(path.stat().st_mode)
    fd, tmp_name = tempfile.mkstemp(
        dir=path.parent, prefix=path.name + ".", suffix=".tmp"
    )
    try:
        with os.fdopen(fd, "w", encoding="utf-8", newline="") as handle:
            handle.write(updated)
        os.chmod(tmp_name, mode)
        os.replace(tmp_name, path)
    except BaseException:
        with contextlib.suppress(FileNotFoundError):
            os.unlink(tmp_name)
        raise
    return True


def self_test() -> int:
    errors: list[str] = []

    def expect(condition: bool, message: str) -> None:
        if not condition:
            errors.append(message)

    try:
        text = "A=1\nCONFIG_EXTRA_CFLAGS=\n# CONFIG_ECHO is not set\n"
        expect(
            rewrite(text, r"^CONFIG_EXTRA_CFLAGS=.*", "CONFIG_EXTRA_CFLAGS=x")
            == "A=1\nCONFIG_EXTRA_CFLAGS=x\n# CONFIG_ECHO is not set\n",
            "replace an existing assignment",
        )
        expect(
            rewrite(text, r"^# CONFIG_ECHO is not set", "CONFIG_ECHO=y")
            == "A=1\nCONFIG_EXTRA_CFLAGS=\nCONFIG_ECHO=y\n",
            "replace a comment placeholder",
        )
        expect(
            rewrite(text, r"^CONFIG_MISSING=", "CONFIG_MISSING=y")
            == text + "CONFIG_MISSING=y\n",
            "append a missing assignment",
        )
        try:
            rewrite(text, "", "X=y")
            errors.append("empty pattern was accepted")
        except ValueError:
            pass

        with tempfile.TemporaryDirectory(prefix="ir0-config-set-") as tmp:
            path = Path(tmp) / ".config"
            path.write_text("CONFIG_A=1\n", encoding="utf-8")
            path.chmod(0o640)
            expect(
                set_line(path, r"^CONFIG_A=.*", "CONFIG_A=2"),
                "set_line reports a change",
            )
            expect(
                path.read_text(encoding="utf-8") == "CONFIG_A=2\n",
                "set_line wrote the replacement",
            )
            expect(
                stat.S_IMODE(path.stat().st_mode) == 0o640,
                "set_line preserves the file mode",
            )
            expect(
                not set_line(path, r"^CONFIG_A=.*", "CONFIG_A=2"),
                "set_line is idempotent",
            )
            expect(
                not list(Path(tmp).glob("*.tmp")),
                "set_line leaves no temporary file behind",
            )
    except Exception as exc:  # noqa: BLE001 - a broken helper is a hard failure
        errors.append(f"self-test crashed: {exc!r}")

    for message in errors:
        print(f"[config-set-line] FAILED: {message}", file=sys.stderr)
    if errors:
        return 1
    print("[config-set-line] OK")
    return 0


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Atomically set a config line")
    parser.add_argument("--self-test", action="store_true", help="run self-tests")
    parser.add_argument("file", nargs="?", type=Path)
    parser.add_argument("pattern", nargs="?")
    parser.add_argument("replacement", nargs="?")
    args = parser.parse_args(argv)

    if args.self_test:
        return self_test()
    if args.file is None or args.pattern is None or args.replacement is None:
        parser.error("FILE, PATTERN and REPLACEMENT are required")
    if not args.file.is_file():
        print(f"config-set-line: no such file: {args.file}", file=sys.stderr)
        return 2
    try:
        set_line(args.file, args.pattern, args.replacement)
    except (OSError, ValueError, re.error) as exc:
        print(f"config-set-line: {exc}", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
