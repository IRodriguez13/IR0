#!/usr/bin/env python3
"""
IR0 Kernel menuconfig — curses-based configuration UI.

Parses setup/Kconfig, shows an interactive terminal menu, and writes:
  .config                         — Makefile-includable CONFIG_*=y/n/value
  include/generated/autoconf.h    — #define CONFIG_* for C source
"""

import curses
import os
import sys
import re

KERNEL_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
KCONFIG_PATH = os.path.join(KERNEL_ROOT, "setup", "Kconfig")
DOT_CONFIG   = os.path.join(KERNEL_ROOT, ".config")
AUTOCONF_DIR = os.path.join(KERNEL_ROOT, "include", "generated")
AUTOCONF_H   = os.path.join(AUTOCONF_DIR, "autoconf.h")

# ---------------------------------------------------------------------------
# Kconfig parser (minimal subset)
# ---------------------------------------------------------------------------

class ConfigSymbol:
    """One config SYMBOL entry."""
    def __init__(self, name):
        self.name   = name
        self.prompt = name
        self.type   = "bool"      # bool | int | string
        self.default = None
        self.value   = None       # current value (set later)
        self.depends = None       # symbol name or None
        self.range_lo = None
        self.range_hi = None
        self.help_text = ""
        self.menu   = ""          # parent menu title

    def effective_default(self):
        if self.type == "bool":
            return self.default if self.default in ("y", "n") else "y"
        if self.type == "int":
            try:
                return str(int(self.default))
            except (TypeError, ValueError):
                return "0"
        return self.default or ""


def parse_kconfig(path):
    """Return a list of ConfigSymbol from a Kconfig file."""
    symbols = []
    current = None
    menu_stack = []
    in_help = False

    with open(path) as f:
        lines = f.readlines()

    for raw in lines:
        line = raw.rstrip("\n")
        stripped = line.strip()

        if in_help:
            if stripped == "" and current and current.help_text:
                in_help = False
                continue
            if line.startswith("        ") or line.startswith("\t\t"):
                if current:
                    current.help_text += stripped + "\n"
                continue
            elif stripped and not line[0].isspace():
                in_help = False
            elif stripped:
                if current:
                    current.help_text += stripped + "\n"
                continue
            else:
                continue

        if stripped.startswith("menu "):
            title = stripped[5:].strip().strip('"')
            menu_stack.append(title)
            continue
        if stripped == "endmenu":
            if menu_stack:
                menu_stack.pop()
            continue
        if stripped.startswith("if ") or stripped == "endif":
            continue

        m = re.match(r"config\s+(\w+)", stripped)
        if m:
            current = ConfigSymbol(m.group(1))
            current.menu = menu_stack[-1] if menu_stack else ""
            symbols.append(current)
            continue

        if current is None:
            continue

        if stripped.startswith("bool"):
            current.type = "bool"
            m2 = re.search(r'"([^"]*)"', stripped)
            if m2:
                current.prompt = m2.group(1)
        elif stripped.startswith("int"):
            current.type = "int"
            m2 = re.search(r'"([^"]*)"', stripped)
            if m2:
                current.prompt = m2.group(1)
        elif stripped.startswith("string"):
            current.type = "string"
            m2 = re.search(r'"([^"]*)"', stripped)
            if m2:
                current.prompt = m2.group(1)
        elif stripped.startswith("default"):
            val = stripped.split(None, 1)[1] if len(stripped.split(None, 1)) > 1 else ""
            current.default = val.strip()
        elif stripped.startswith("depends on"):
            current.depends = stripped.replace("depends on", "").strip()
        elif stripped.startswith("range"):
            parts = stripped.split()
            if len(parts) >= 3:
                current.range_lo = int(parts[1])
                current.range_hi = int(parts[2])
        elif stripped == "help":
            in_help = True

    return symbols


def load_dotconfig(symbols):
    """Load existing .config into symbol values."""
    vals = {}
    if os.path.isfile(DOT_CONFIG):
        with open(DOT_CONFIG) as f:
            for line in f:
                line = line.strip()
                if line.startswith("#") or "=" not in line:
                    continue
                k, v = line.split("=", 1)
                vals[k.strip()] = v.strip()
    for s in symbols:
        key = "CONFIG_" + s.name
        if key in vals:
            s.value = vals[key]
        else:
            s.value = s.effective_default()


def save_config(symbols):
    """Write .config and include/generated/autoconf.h."""
    with open(DOT_CONFIG, "w") as f:
        f.write("#\n# IR0 Kernel Configuration\n# Generated by menuconfig\n#\n\n")
        for s in symbols:
            key = "CONFIG_" + s.name
            if s.type == "bool":
                if s.value == "y":
                    f.write(f"{key}=y\n")
                else:
                    f.write(f"# {key} is not set\n")
            else:
                f.write(f"{key}={s.value}\n")

    os.makedirs(AUTOCONF_DIR, exist_ok=True)
    with open(AUTOCONF_H, "w") as f:
        f.write("/*\n * Automatically generated by menuconfig — do not edit.\n */\n")
        f.write("#ifndef _IR0_AUTOCONF_H\n#define _IR0_AUTOCONF_H\n\n")
        for s in symbols:
            macro = "CONFIG_" + s.name
            if s.type == "bool":
                if s.value == "y":
                    f.write(f"#define {macro} 1\n")
                else:
                    f.write(f"/* {macro} is not set */\n")
            elif s.type == "int":
                f.write(f"#define {macro} {s.value}\n")
            else:
                f.write(f'#define {macro} "{s.value}"\n')
        f.write("\n#endif /* _IR0_AUTOCONF_H */\n")


# ---------------------------------------------------------------------------
# Dependency helpers
# ---------------------------------------------------------------------------

def sym_lookup(symbols, name):
    for s in symbols:
        if s.name == name:
            return s
    return None

def dep_satisfied(symbols, sym):
    if sym.depends is None:
        return True
    dep = sym_lookup(symbols, sym.depends)
    if dep is None:
        return True
    return dep.value == "y"

# ---------------------------------------------------------------------------
# Curses TUI
# ---------------------------------------------------------------------------

def run_menu(stdscr, symbols):
    curses.curs_set(0)
    curses.start_color()
    curses.use_default_colors()
    curses.init_pair(1, curses.COLOR_WHITE, curses.COLOR_BLUE)
    curses.init_pair(2, curses.COLOR_YELLOW, -1)
    curses.init_pair(3, curses.COLOR_GREEN, -1)
    curses.init_pair(4, curses.COLOR_RED, -1)
    curses.init_pair(5, curses.COLOR_CYAN, -1)

    visible = [s for s in symbols if dep_satisfied(symbols, s)]
    cursor = 0
    scroll = 0
    show_help = False
    dirty = False

    while True:
        visible = [s for s in symbols if dep_satisfied(symbols, s)]
        if cursor >= len(visible):
            cursor = max(0, len(visible) - 1)

        stdscr.erase()
        h, w = stdscr.getmaxyx()

        title = " IR0 Kernel Configuration "
        stdscr.attron(curses.color_pair(1) | curses.A_BOLD)
        stdscr.addstr(0, 0, " " * w)
        stdscr.addstr(0, max(0, (w - len(title)) // 2), title)
        stdscr.attroff(curses.color_pair(1) | curses.A_BOLD)

        footer = " [Space] Toggle  [Enter] Edit  [S] Save  [D] Defaults  [?] Help  [Q] Quit "
        stdscr.attron(curses.color_pair(1))
        stdscr.addstr(h - 1, 0, footer[:w - 1].ljust(w - 1))
        stdscr.attroff(curses.color_pair(1))

        if dirty:
            mod = " [MODIFIED] "
            stdscr.attron(curses.color_pair(4) | curses.A_BOLD)
            stdscr.addstr(0, w - len(mod) - 1, mod)
            stdscr.attroff(curses.color_pair(4) | curses.A_BOLD)

        list_top = 2
        list_h = h - 4

        if cursor < scroll:
            scroll = cursor
        if cursor >= scroll + list_h:
            scroll = cursor - list_h + 1

        current_menu = None
        row = list_top
        for idx in range(scroll, min(scroll + list_h, len(visible))):
            sym = visible[idx]
            if sym.menu and sym.menu != current_menu:
                current_menu = sym.menu
                header = f"--- {current_menu} ---"
                if row < h - 2:
                    stdscr.attron(curses.color_pair(5) | curses.A_BOLD)
                    stdscr.addstr(row, 2, header[:w - 4])
                    stdscr.attroff(curses.color_pair(5) | curses.A_BOLD)
                    row += 1
                    if row >= h - 2:
                        break

            if row >= h - 2:
                break

            is_sel = (idx == cursor)

            if sym.type == "bool":
                marker = "[*]" if sym.value == "y" else "[ ]"
                color = curses.color_pair(3) if sym.value == "y" else curses.color_pair(0)
            elif sym.type == "int":
                marker = f"({sym.value})"
                color = curses.color_pair(2)
            else:
                marker = f'"{sym.value}"'
                color = curses.color_pair(2)

            line = f"  {marker} {sym.prompt}"
            if len(line) > w - 2:
                line = line[:w - 5] + "..."

            if is_sel:
                stdscr.attron(curses.A_REVERSE)
            stdscr.attron(color)
            stdscr.addstr(row, 0, line.ljust(w - 1)[:w - 1])
            stdscr.attroff(color)
            if is_sel:
                stdscr.attroff(curses.A_REVERSE)

            row += 1

        if show_help and cursor < len(visible):
            sym = visible[cursor]
            help_lines = [
                f"Symbol: CONFIG_{sym.name}",
                f"Type: {sym.type}",
                f"Default: {sym.effective_default()}",
            ]
            if sym.depends:
                help_lines.append(f"Depends on: CONFIG_{sym.depends}")
            if sym.range_lo is not None:
                help_lines.append(f"Range: {sym.range_lo}..{sym.range_hi}")
            if sym.help_text.strip():
                help_lines.append("")
                help_lines.extend(sym.help_text.strip().split("\n"))

            box_w = min(60, w - 4)
            box_h = min(len(help_lines) + 2, h - 4)
            box_y = max(1, (h - box_h) // 2)
            box_x = max(1, (w - box_w) // 2)

            try:
                win = curses.newwin(box_h, box_w, box_y, box_x)
                win.box()
                win.addstr(0, 2, " Help ")
                for i, hl in enumerate(help_lines[:box_h - 2]):
                    win.addstr(i + 1, 1, hl[:box_w - 3])
                win.refresh()
            except curses.error:
                pass

        stdscr.refresh()

        key = stdscr.getch()

        if show_help:
            show_help = False
            continue

        if key == curses.KEY_UP or key == ord('k'):
            cursor = max(0, cursor - 1)
        elif key == curses.KEY_DOWN or key == ord('j'):
            cursor = min(len(visible) - 1, cursor + 1)
        elif key == curses.KEY_HOME:
            cursor = 0
        elif key == curses.KEY_END:
            cursor = len(visible) - 1
        elif key == curses.KEY_PPAGE:
            cursor = max(0, cursor - list_h)
        elif key == curses.KEY_NPAGE:
            cursor = min(len(visible) - 1, cursor + list_h)
        elif key in (ord(' '), ord('\n'), curses.KEY_ENTER, 10, 13):
            if cursor < len(visible):
                sym = visible[cursor]
                if sym.type == "bool":
                    sym.value = "n" if sym.value == "y" else "y"
                    dirty = True
                elif sym.type == "int":
                    val = curses_input(stdscr, f"CONFIG_{sym.name}", sym.value, h, w)
                    if val is not None:
                        try:
                            v = int(val)
                            if sym.range_lo is not None:
                                v = max(sym.range_lo, min(sym.range_hi, v))
                            sym.value = str(v)
                            dirty = True
                        except ValueError:
                            pass
                else:
                    val = curses_input(stdscr, f"CONFIG_{sym.name}", sym.value, h, w)
                    if val is not None:
                        sym.value = val
                        dirty = True
        elif key == ord('?') or key == ord('h'):
            show_help = True
        elif key == ord('s') or key == ord('S'):
            save_config(symbols)
            dirty = False
            flash_msg(stdscr, "Configuration saved to .config", h, w)
        elif key == ord('d') or key == ord('D'):
            for s in symbols:
                s.value = s.effective_default()
            dirty = True
        elif key == ord('q') or key == ord('Q'):
            if dirty:
                if confirm(stdscr, "Save before exit?", h, w):
                    save_config(symbols)
            break


def curses_input(stdscr, label, current, h, w):
    """Simple single-line input dialog."""
    box_w = min(50, w - 4)
    box_h = 5
    box_y = (h - box_h) // 2
    box_x = (w - box_w) // 2
    win = curses.newwin(box_h, box_w, box_y, box_x)
    win.box()
    win.addstr(0, 2, f" {label} ")
    win.addstr(2, 2, "> ")
    win.refresh()

    curses.curs_set(1)
    curses.echo()
    buf = win.getstr(2, 4, box_w - 6)
    curses.noecho()
    curses.curs_set(0)

    try:
        return buf.decode("utf-8").strip()
    except Exception:
        return current


def flash_msg(stdscr, msg, h, w):
    box_w = min(len(msg) + 6, w - 2)
    box_y = h // 2
    box_x = (w - box_w) // 2
    try:
        win = curses.newwin(3, box_w, box_y, box_x)
        win.box()
        win.attron(curses.color_pair(3) | curses.A_BOLD)
        win.addstr(1, 2, msg[:box_w - 4])
        win.attroff(curses.color_pair(3) | curses.A_BOLD)
        win.refresh()
        curses.napms(1200)
    except curses.error:
        pass


def confirm(stdscr, msg, h, w):
    box_w = min(len(msg) + 16, w - 2)
    box_y = h // 2
    box_x = (w - box_w) // 2
    try:
        win = curses.newwin(3, box_w, box_y, box_x)
        win.box()
        win.addstr(1, 2, f"{msg} [y/N] ")
        win.refresh()
        key = win.getch()
        return key in (ord('y'), ord('Y'))
    except curses.error:
        return False


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------

def main():
    if not os.path.isfile(KCONFIG_PATH):
        print(f"Error: {KCONFIG_PATH} not found", file=sys.stderr)
        sys.exit(1)

    symbols = parse_kconfig(KCONFIG_PATH)
    load_dotconfig(symbols)
    curses.wrapper(run_menu, symbols)


if __name__ == "__main__":
    main()
