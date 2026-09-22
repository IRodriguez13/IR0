#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Host behavior tests for usmang (scripts/userspace_manager.py)."""

from __future__ import annotations

import importlib.util
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
    def test_desktop_admin_defaults_to_doas(self) -> None:
        spec = importlib.util.spec_from_file_location("userspace_manager", MANAGER)
        assert spec and spec.loader
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        self.assertEqual(module.profile_admin_elevation(ISD, "desktop"), "doas")

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
        self.assertIn("--packages", text)

    def test_verify_selected_packages_logs_and_continues(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            isd = Path(directory)
            staged = isd / "out" / "x86_64" / "rootfs" / "minimal"
            (staged / "bin").mkdir(parents=True)
            (staged / "bin" / "busybox").write_text("stub\n")
            (staged / "etc").mkdir(parents=True)
            (staged / "etc" / "ir0-profile").write_text("minimal\n")
            (isd / "Makefile").write_text("all:\n")
            (isd / "profiles" / "minimal").mkdir(parents=True)
            (isd / "profiles/minimal/verify-paths.txt").write_text(
                "busybox bin/busybox\nrunit etc/runit\n"
            )
            result = subprocess.run(
                [
                    "python3",
                    str(MANAGER),
                    "--isd-root",
                    str(isd),
                    "--profile",
                    "minimal",
                    "verify",
                    "-p",
                    "busybox,runit,ghost",
                ],
                text=True,
                capture_output=True,
                check=False,
            )
            self.assertEqual(result.returncode, 1)
            self.assertIn("OK  busybox: bin/busybox", result.stdout)
            self.assertIn("verify runit: missing etc/runit", result.stderr)
            self.assertIn("unknown package 'ghost'", result.stderr)

    def test_list_profiles_includes_minimal_sysvinit(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            isd = Path(directory)
            for name in ("minimal", "minimal-sysvinit"):
                prof = isd / "profiles" / name
                prof.mkdir(parents=True)
                (prof / "profile.conf").write_text(
                    f"PROFILE_NAME={name}\nUSERLAND_BASE=busybox\n"
                    f"INIT_SYSTEM={'sysvinit' if name.endswith('sysvinit') else 'runit'}\n"
                )
            spec = importlib.util.spec_from_file_location("userspace_manager", MANAGER)
            assert spec and spec.loader
            module = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(module)
            names = module.list_profiles(isd)
            self.assertIn("minimal-sysvinit", names)
            self.assertEqual(module.profile_init_system(isd, "minimal-sysvinit"), "sysvinit")
            self.assertEqual(module.profile_init_system(isd, "minimal"), "runit")

    def test_profile_init_system_fail_closed(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            isd = Path(directory)
            prof = isd / "profiles" / "broken"
            prof.mkdir(parents=True)
            (prof / "profile.conf").write_text(
                "PROFILE_NAME=broken\nUSERLAND_BASE=busybox\nINIT_SYSTEM=foobar\n"
            )
            spec = importlib.util.spec_from_file_location("userspace_manager", MANAGER)
            assert spec and spec.loader
            module = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(module)
            with self.assertRaises(module.UnsupportedInitSystem):
                module.profile_init_system(isd, "broken")
            empty = isd / "profiles" / "empty"
            empty.mkdir(parents=True)
            (empty / "profile.conf").write_text("PROFILE_NAME=empty\n")
            with self.assertRaises(module.UnsupportedInitSystem):
                module.profile_init_system(isd, "empty")

    def test_tui_help_lines_cover_verify(self) -> None:
        spec = importlib.util.spec_from_file_location("userspace_manager", MANAGER)
        assert spec and spec.loader
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        text = "\n".join(module.tui_help_lines())
        self.assertIn("verify", text)
        self.assertIn("admin", text)
        self.assertIn("quit", text)

    def test_admin_elevation_toggle_persists_to_isdconfig(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            isd = Path(directory)
            prof = isd / "profiles" / "desktop"
            prof.mkdir(parents=True)
            (prof / "profile.conf").write_text(
                "PROFILE_NAME=desktop\nADMIN_ELEVATION=doas\nUSERLAND_BASE=busybox\n"
            )
            (prof / "packages.txt").write_text("busybox\nrunit\nopendoas\n")
            spec = importlib.util.spec_from_file_location("userspace_manager", MANAGER)
            assert spec and spec.loader
            module = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(module)
            self.assertEqual(module.profile_admin_elevation(isd, "desktop"), "doas")
            new_tool, cfg_path = module.toggle_admin_elevation(isd, "desktop")
            self.assertEqual(new_tool, "sudo")
            self.assertTrue(cfg_path.is_file())
            text = cfg_path.read_text()
            self.assertIn("ADMIN_ELEVATION=sudo", text)
            self.assertIn("CONFIG_PKG_SUDO=y", text)
            self.assertIn("CONFIG_PKG_OPENDOAS=n", text)
            self.assertEqual(module.profile_admin_elevation(isd, "desktop"), "sudo")

    def test_tui_requires_tty(self) -> None:
        result = subprocess.run(
            ["python3", str(MANAGER), "tui"],
            text=True,
            capture_output=True,
            check=False,
        )
        self.assertEqual(result.returncode, 2)
        self.assertIn("TTY", result.stderr)

    @unittest.skipUnless(ISD.is_dir(), "ISD sibling tree not present")
    def test_help_mentions_login_session_doc_when_isd_present(self) -> None:
        doc = ISD / "Documentation/LOGIN_SESSION.md"
        if not doc.is_file():
            self.skipTest("ISD LOGIN_SESSION.md not present yet")
        result = self.run_manager("help")
        self.assertIn("LOGIN_SESSION.md", result.stdout)


if __name__ == "__main__":
    unittest.main()
