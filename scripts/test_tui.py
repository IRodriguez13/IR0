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


def filter_rows(rows: list[dict], query: str) -> list[int]:
    """Return row indices matching query (id, group or label)."""
    needle = query.strip().lower()
    if not needle:
        return list(range(len(rows)))
    return [
        i
        for i, row in enumerate(rows)
        if needle in row["id"].lower()
        or needle in row["group"].lower()
        or needle in row["label"].lower()
    ]


def display_lines(rows: list[dict], view: list[int]) -> list[tuple[str, int | None]]:
    """Build the display lines once per filter/toggle change."""
    lines: list[tuple[str, int | None]] = []
    last_grp = None
    for i in view:
        row = rows[i]
        group = row["group"] or "other"
        if group != last_grp:
            lines.append((f"[{group}]", None))
            last_grp = group
        mark = "[x]" if row["on"] else "[ ]"
        lines.append((f"{mark} {row['id']:<8} make {row['label']}", i))
    return lines


def selected_ids(rows: list[dict], view: list[int]) -> list[str]:
    return [rows[i]["id"] for i in view if rows[i]["on"]]


def draw(
    stdscr,
    rows: list[dict],
    view: list[int],
    lines: list[tuple[str, int | None]],
    cursor: int,
    scroll: int,
    persist: bool,
    status: str,
    query: str,
) -> tuple[int, int]:
    stdscr.erase()
    h, w = stdscr.getmaxyx()
    if h < 3 or w < 2:
        stdscr.refresh()
        return scroll, 1

    title = " IR0 tests "
    _safe_addstr(stdscr, 0, max(0, (w - len(title)) // 2), title, curses.A_BOLD)
    _safe_addstr(
        stdscr,
        1,
        0,
        "Space toggle   A all   D defaults   R run   P disk   / search   Q quit",
    )

    chosen = len(selected_ids(rows, view))
    scope = f"{len(view)}/{len(rows)} suites"
    if query:
        scope += f"  filter:{query}"
    _safe_addstr(
        stdscr,
        2,
        0,
        f"disk {'on' if persist else 'off'}   {chosen} selected   {scope}   {status}",
    )

    list_top = 4
    room = max(1, h - list_top)
    cursor_line = 0
    current_row = view[cursor] if view else None
    for n, (_text, row_i) in enumerate(lines):
        if current_row is not None and row_i == current_row:
            cursor_line = n
            break

    if cursor_line < scroll:
        scroll = cursor_line
    if cursor_line >= scroll + room:
        scroll = cursor_line - room + 1
    scroll = max(0, min(scroll, max(0, len(lines) - room)))

    if not view:
        _safe_addstr(stdscr, list_top, 0, "  no suites match the current filter")
    else:
        for offset, (text, row_i) in enumerate(lines[scroll : scroll + room]):
            attr = curses.A_REVERSE if row_i == current_row else curses.A_NORMAL
            if row_i is None:
                attr = curses.A_UNDERLINE
            _safe_addstr(stdscr, list_top + offset, 0, text, attr)

    stdscr.refresh()
    return scroll, room


def _safe_addstr(stdscr, y: int, x: int, text: str, attr: int = curses.A_NORMAL) -> None:
    try:
        h, w = stdscr.getmaxyx()
        if y < 0 or y >= h or x >= w:
            return
        stdscr.addstr(y, x, text[: max(0, w - x - 1)], attr)
    except curses.error:
        pass


def prompt_search(stdscr, current: str) -> str:
    h, w = stdscr.getmaxyx()
    if h < 2 or w < 4:
        return current
    curses.curs_set(1)
    curses.echo()
    try:
        stdscr.addstr(h - 1, 0, " " * max(0, w - 1))
        stdscr.addstr(h - 1, 0, "/ ")
        stdscr.refresh()
        raw = stdscr.getstr(h - 1, 2, max(1, w - 3))
        return raw.decode("utf-8", "replace").strip()
    except curses.error:
        return current
    finally:
        curses.noecho()
        curses.curs_set(0)


def run_selected(ids: list[str], persist: bool) -> tuple[int, str]:
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

    query = ""
    view = filter_rows(rows, query)
    lines = display_lines(rows, view)
    cursor = 0
    scroll = 0
    persist = False
    status = ""

    while True:
        scroll, page = draw(
            stdscr, rows, view, lines, cursor, scroll, persist, status, query
        )
        ch = stdscr.getch()

        if ch == curses.KEY_RESIZE:
            scroll = min(scroll, max(0, len(lines) - 1))
            continue

        if ch in (ord("q"), ord("Q")):
            return 0
        if ch == 27:
            if query:
                query = ""
                view = filter_rows(rows, query)
                lines = display_lines(rows, view)
                cursor = 0
                scroll = 0
                status = ""
                continue
            return 0

        if ch == ord("/"):
            query = prompt_search(stdscr, query)
            view = filter_rows(rows, query)
            lines = display_lines(rows, view)
            cursor = 0
            scroll = 0
            status = f"{len(view)} match(es)" if query else ""
        elif ch in (curses.KEY_UP, ord("k")):
            if view:
                cursor = (cursor - 1) % len(view)
            status = ""
        elif ch in (curses.KEY_DOWN, ord("j")):
            if view:
                cursor = (cursor + 1) % len(view)
            status = ""
        elif ch == curses.KEY_PPAGE:
            if view:
                cursor = max(0, cursor - page)
            status = ""
        elif ch == curses.KEY_NPAGE:
            if view:
                cursor = min(len(view) - 1, cursor + page)
            status = ""
        elif ch == curses.KEY_HOME:
            cursor = 0
            status = ""
        elif ch == curses.KEY_END:
            cursor = max(0, len(view) - 1)
            status = ""
        elif ch == ord(" "):
            if view:
                row = rows[view[cursor]]
                row["on"] = not row["on"]
                lines = display_lines(rows, view)
            status = ""
        elif ch in (ord("a"), ord("A")):
            targets = [rows[i] for i in view]
            val = not all(row["on"] for row in targets)
            for row in targets:
                row["on"] = val
            lines = display_lines(rows, view)
            status = ""
        elif ch in (ord("d"), ord("D")):
            for row in rows:
                row["on"] = row["default"]
            lines = display_lines(rows, view)
            status = ""
        elif ch in (ord("p"), ord("P")):
            persist = not persist
            status = ""
        elif ch in (ord("r"), ord("R")):
            ids = selected_ids(rows, view)
            if not ids:
                status = "nothing selected"
                continue
            status = "running..."
            draw(stdscr, rows, view, lines, cursor, scroll, persist, status, query)
            curses.endwin()
            rc, msg = run_selected(ids, persist)
            stdscr.refresh()
            curses.doupdate()
            stdscr.keypad(True)
            curses.curs_set(0)
            status = msg
            if rc != 0:
                _safe_addstr(
                    stdscr,
                    curses.LINES - 1,
                    0,
                    f"Failed ({msg}). Press any key.",
                )
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
