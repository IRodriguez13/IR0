#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Host behavior tests for usmang (scripts/userspace_manager.py)."""

from __future__ import annotations

import json
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
MANAGER = ROOT / "scripts/userspace_manager.py"
ISD = Path(ROOT).parent / "ISD"


class UserspaceManagerTest(unittest.TestCase):
    def run_manager(self, *arguments: str, success: bool = True) -> subprocess.CompletedProcess[str]:
        result = subprocess.run(
            ["python3", str(MANAGER), *arguments],
            text=True,
            capture_output=True,
            check=False,
        )
        if success:
            self.assertEqual(result.returncode, 0, result.stderr)
        return result

    @unittest.skipUnless(ISD.is_dir(), "ISD sibling tree not present")
    def test_minimal_summary_does_not_claim_desktop_clients(self) -> None:
        result = self.run_manager(
            "--isd-root", str(ISD), "--profile", "minimal", "summary",
        )
        self.assertIn("desktop: n/a", result.stdout)
        self.assertNotIn("desktop X clients", result.stdout)

    @unittest.skipUnless(ISD.is_dir(), "ISD sibling tree not present")
    def test_desktop_command_lists_profile_packages_only(self) -> None:
        result = self.run_manager(
            "--isd-root", str(ISD), "--profile", "desktop", "--json", "desktop",
        )
        payload = json.loads(result.stdout)
        self.assertTrue(payload["applies"])
        packages = (ISD / "profiles/desktop/packages.txt").read_text()
        for name in payload["x_session_packages"]:
            self.assertIn(name, packages)
        self.assertIn("twm", payload["x_session_packages"])
        self.assertIn("xterm", payload["x_session_packages"])

    def test_fake_isd_minimal_desktop_not_applicable(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            isd = Path(directory)
            (isd / "Makefile").write_text("all:\n")
            (isd / "VERSION").write_text("9.9.9\n")
            (isd / "profiles").mkdir()
            (isd / "profiles/minimal").mkdir()
            (isd / "profiles/minimal/profile.conf").write_text('USERLAND_BASE=busybox\n')
            (isd / "profiles/minimal/packages.txt").write_text("busybox\nrunit\n")
            (isd / "scripts").mkdir()
            (isd / "scripts/resolve-packages.sh").write_text(
                "#!/bin/bash\nprintf '%s\\n' busybox runit\n"
            )
            (isd / "scripts/package-origins.txt").write_text("")
            (isd / "scripts/resolve-packages.sh").chmod(0o755)
            result = self.run_manager(
                "--isd-root", str(isd), "--profile", "minimal", "--json", "desktop",
            )
            payload = json.loads(result.stdout)
            self.assertFalse(payload["applies"])
            self.assertEqual(payload["x_session_packages"], [])

    def test_help_lists_commands_and_future_scope(self) -> None:
        result = self.run_manager("help")
        text = result.stdout
        self.assertIn("usmang", text.lower())
        for fragment in ("summary", "userland", "help", "BusyBox", "systemd"):
            self.assertIn(fragment, text)

    @unittest.skipUnless(ISD.is_dir(), "ISD sibling tree not present")
    def test_help_mentions_login_session_doc_when_isd_present(self) -> None:
        doc = ISD / "Documentation/LOGIN_SESSION.md"
        if not doc.is_file():
            self.skipTest("ISD LOGIN_SESSION.md not present yet")
        result = self.run_manager("help")
        self.assertIn("LOGIN_SESSION.md", result.stdout)


if __name__ == "__main__":
    unittest.main()
