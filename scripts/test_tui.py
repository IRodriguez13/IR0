#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Interactive test suite picker (curses TUI) — make test."""

from __future__ import annotations

import curses
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import test_runner  # noqa: E402  (same directory; catalog parser)

ROOT = Path(__file__).resolve().parents[1]
RUNNER = ROOT / "scripts" / "test_runner.py"


def load_rows() -> list[dict]:
    suites = test_runner.load_catalog(test_runner.CATALOG)
    rows: list[dict] = []
    for suite in suites:
        sid = suite.get("id")
        if not sid:
            continue
        default = bool(suite.get("default"))
        rows.append(
            {
                "id": sid,
                "group": suite.get("group", ""),
                "label": suite.get("make", suite.get("label", "")),
                "default": default,
                "on": default,
            }
        )
    return rows


def display_lines(rows: list[dict]) -> list[tuple[str, int | None]]:
    lines: list[tuple[str, int | None]] = []
    last_grp = None
    for i, row in enumerate(rows):
        group = row["group"] or "other"
        if group != last_grp:
            lines.append((f"[{group}]", None))
            last_grp = group
        mark = "[x]" if row["on"] else "[ ]"
        lines.append((f"{mark} {row['id']:<8} make {row['label']}", i))
    return lines


def draw(stdscr, rows: list[dict], idx: int, scroll: int, persist: bool, status: str) -> int:
    stdscr.erase()
    h, w = stdscr.getmaxyx()
    title = " IR0 tests "
    stdscr.addstr(0, max(0, (w - len(title)) // 2), title, curses.A_BOLD)
    stdscr.addstr(1, 0, "Space toggle   A all   D defaults   R run   P disk   Q quit"[: max(0, w - 1)])
    selected = sum(1 for row in rows if row["on"])
    stdscr.addstr(
        2, 0,
        f"disk {'on' if persist else 'off'}   {selected} selected   {status}"[: max(0, w - 1)],
    )

    lines = display_lines(rows)
    cursor_line = next((n for n, (_text, row_i) in enumerate(lines) if row_i == idx), 0)
    list_top = 4
    room = max(1, h - list_top)
    if cursor_line < scroll:
        scroll = cursor_line
    if cursor_line >= scroll + room:
        scroll = cursor_line - room + 1
    scroll = max(0, min(scroll, max(0, len(lines) - room)))

    for offset, (text, row_i) in enumerate(lines[scroll:scroll + room]):
        attr = curses.A_REVERSE if row_i == idx else curses.A_NORMAL
        if row_i is None:
            attr = curses.A_UNDERLINE
        stdscr.addstr(list_top + offset, 0, text[: max(0, w - 1)], attr)

    stdscr.refresh()
    return scroll


def run_selected(rows: list[dict], persist: bool) -> tuple[int, str]:
    ids = [r["id"] for r in rows if r["on"]]
    if not ids:
        return 2, "nothing selected"
    cmd = [sys.executable, str(RUNNER), "--suites", ",".join(ids)]
    if persist:
        cmd.append("--persist")
    proc = subprocess.run(cmd, cwd=ROOT)
    if proc.returncode == 0:
        return 0, "OK"
    return proc.returncode, f"exit {proc.returncode}"


def main_curses(stdscr) -> int:
    curses.curs_set(0)
    stdscr.keypad(True)
    rows = load_rows()
    if not rows:
        stdscr.addstr(0, 0, "empty catalog")
        stdscr.refresh()
        stdscr.getch()
        return 2

    idx = 0
    scroll = 0
    persist = False
    status = ""

    while True:
        scroll = draw(stdscr, rows, idx, scroll, persist, status)
        ch = stdscr.getch()
        if ch in (ord("q"), ord("Q"), 27):
            return 0
        if ch in (curses.KEY_UP, ord("k")):
            idx = (idx - 1) % len(rows)
        elif ch in (curses.KEY_DOWN, ord("j")):
            idx = (idx + 1) % len(rows)
        elif ch == ord(" "):
            rows[idx]["on"] = not rows[idx]["on"]
        elif ch in (ord("a"), ord("A")):
            val = not all(r["on"] for r in rows)
            for r in rows:
                r["on"] = val
        elif ch in (ord("d"), ord("D")):
            for r in rows:
                r["on"] = r["default"]
        elif ch in (ord("p"), ord("P")):
            persist = not persist
        elif ch in (ord("r"), ord("R")):
            status = "running..."
            scroll = draw(stdscr, rows, idx, scroll, persist, status)
            curses.endwin()
            rc, msg = run_selected(rows, persist)
            stdscr.refresh()
            curses.doupdate()
            stdscr.keypad(True)
            status = msg
            if rc != 0:
                stdscr.addstr(curses.LINES - 1, 0, f"Failed ({msg}). Press any key.")
                stdscr.refresh()
                stdscr.getch()


def main() -> int:
    if not sys.stdin.isatty():
        sys.stderr.write("test_tui requires a terminal; use: make test SUITE=host,ktest\n")
        return 2
    try:
        return curses.wrapper(main_curses)
    except RuntimeError as exc:
        sys.stderr.write(f"{exc}\n")
        return 2


if __name__ == "__main__":
    sys.exit(main())
