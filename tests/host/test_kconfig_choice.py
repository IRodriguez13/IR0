#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Host contract: Kconfig `choice` blocks stay one-hot through menuconfig.py.

Linux Kconfig semantics: exactly one member of a bool choice is y, the
choice's `default` picks it when .config names none, and choice attributes
(prompt/default/depends/help) never leak onto the symbol declared before it.
"""

from __future__ import annotations

import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
KCONFIG = ROOT / "setup" / "Kconfig"


def kconfig_choices() -> list[tuple[str, list[str]]]:
    """Return (default, members) for every choice block in setup/Kconfig."""
    choices = []
    members: list[str] | None = None
    default = ""
    for line in KCONFIG.read_text().splitlines():
        s = line.strip()
        if s == "choice":
            members, default = [], ""
        elif s == "endchoice" and members is not None:
            choices.append((default, members))
            members = None
        elif members is not None:
            m = re.match(r"config\s+(\w+)", s)
            if m:
                members.append(m.group(1))
            elif s.startswith("default") and not members:
                default = s.split(None, 1)[1]
    return choices


def read_vals(path: Path) -> dict[str, str]:
    vals = {}
    for line in path.read_text().splitlines():
        m = re.match(r"(CONFIG_\w+)=(.*)", line.strip())
        if m:
            vals[m.group(1)] = m.group(2)
        m = re.match(r"#define (CONFIG_\w+) (.*)", line.strip())
        if m:
            vals[m.group(1)] = m.group(2)
    return vals


def run_tool(tree: Path, *args: str) -> None:
    subprocess.run(
        [sys.executable, str(tree / "scripts/kconfig/menuconfig.py"), *args],
        check=True,
        stdout=subprocess.DEVNULL,
    )


def check_one_hot(tree: Path, label: str, want: dict[str, str]) -> list[str]:
    errors = []
    dot = read_vals(tree / ".config")
    hdr = read_vals(tree / "includes/generated/autoconf.h")
    for default, members in kconfig_choices():
        on = [m for m in members if dot.get("CONFIG_" + m) == "y"]
        on_h = [m for m in members if hdr.get("CONFIG_" + m) == "1"]
        expected = want.get(members[0], default)
        if on != [expected] or on_h != [expected]:
            errors.append(
                f"{label}: choice {members} .config={on} autoconf={on_h} "
                f"want [{expected}]"
            )
    return errors


def main() -> int:
    errors: list[str] = []
    choices = kconfig_choices()
    if len(choices) < 2:
        print("expected the board and log-profile choices in Kconfig", file=sys.stderr)
        return 1

    with tempfile.TemporaryDirectory(prefix="ir0-kconfig-") as tmp:
        tree = Path(tmp)
        shutil.copytree(ROOT / "setup", tree / "setup")
        (tree / "scripts/kconfig").mkdir(parents=True)
        shutil.copy2(ROOT / "scripts/kconfig/menuconfig.py", tree / "scripts/kconfig")

        # 1. defconfig names one profile: --sync must not switch the others on.
        shutil.copy2(tree / "setup/defconfig", tree / ".config")
        run_tool(tree, "--sync")
        errors += check_one_hot(tree, "defconfig --sync", {})
        dot = read_vals(tree / ".config")
        if dot.get("CONFIG_TOOL_MENUCONFIG_LANG") != "en":
            errors.append(
                "choice default leaked onto TOOL_MENUCONFIG_LANG: "
                f"{dot.get('CONFIG_TOOL_MENUCONFIG_LANG')!r}"
            )

        # 2. A .config that omits a choice gets the choice default; a symbol
        #    declared before a choice keeps its own default.
        lines = [
            l for l in (tree / "setup/defconfig").read_text().splitlines()
            if not l.startswith(
                ("CONFIG_LOG_PROFILE_", "CONFIG_ARCH_ARM64=", "CONFIG_TOOL_MENUCONFIG_LANG=")
            )
        ]
        (tree / ".config").write_text("\n".join(lines) + "\n")
        run_tool(tree, "--sync")
        errors += check_one_hot(tree, "missing choice --sync", {})
        dot = read_vals(tree / ".config")
        if dot.get("CONFIG_ARCH_ARM64") != "n":
            errors.append("choice default leaked onto ARCH_ARM64 (became y)")
        if dot.get("CONFIG_TOOL_MENUCONFIG_LANG") != "en":
            errors.append(
                "choice default leaked onto TOOL_MENUCONFIG_LANG: "
                f"{dot.get('CONFIG_TOOL_MENUCONFIG_LANG')!r}"
            )

        # 3. A corrupted .config with every member on recovers to one member.
        text = (tree / ".config").read_text()
        for _, members in choices:
            for m in members:
                text = re.sub(rf"^CONFIG_{m}=.*$", f"CONFIG_{m}=y", text, flags=re.M)
                if f"CONFIG_{m}=" not in text:
                    text += f"CONFIG_{m}=y\n"
        (tree / ".config").write_text(text)
        run_tool(tree, "--sync")
        errors += check_one_hot(tree, "all-on --sync", {})

        # 4. --set selects one member and deselects its siblings.
        run_tool(tree, "--set", "LOG_PROFILE_DEBUG=y")
        errors += check_one_hot(
            tree, "--set LOG_PROFILE_DEBUG=y", {"LOG_PROFILE_QUIET": "LOG_PROFILE_DEBUG"}
        )

    for e in errors:
        print(e, file=sys.stderr)
    if errors:
        return 1
    print("✓ test_kconfig_choice")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
