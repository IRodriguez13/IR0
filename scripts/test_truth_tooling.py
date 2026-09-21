#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Truth tests for IR0 tooling (kmang, usmang, ISD path resolver)."""

from __future__ import annotations

import importlib.util
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
MANAGER = ROOT / "scripts/kernel_manager.py"
USMANG = ROOT / "scripts/userspace_manager.py"
RESOLVER = ROOT / "scripts/resolve_isd_root.sh"
ISD_CHECK = ROOT.parent / "ISD" / "scripts" / "check-ir0-interface.sh"


def load_module(path: Path, name: str):
    spec = importlib.util.spec_from_file_location(name, path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"cannot load {path}")
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


KM = load_module(MANAGER, "kernel_manager_truth")
UM = load_module(USMANG, "userspace_manager_truth")


def fake_kernel_c_source(release: str, payload: bytes) -> str:
    body = (
        f'const char ir0_version_release[] = "IR0VER:{release}";\n'
        f"const char payload[] = {payload!r};\n"
    )
    return body.replace("b'", '"').replace("';", '";')


class TruthToolingTest(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def test_resolve_isd_root_fails_without_checkout(self) -> None:
        empty = self.root / "empty-ir0"
        empty.mkdir()
        env = os.environ.copy()
        env.pop("IR0_ISD_ROOT", None)
        result = subprocess.run(
            ["bash", str(RESOLVER), str(empty)],
            text=True,
            capture_output=True,
            check=False,
            env=env,
        )
        self.assertEqual(result.returncode, 1)
        self.assertIn("Set IR0_ISD_ROOT", result.stderr)

    def test_resolve_isd_root_ignores_home_isd(self) -> None:
        ir0 = self.root / "IR0"
        ir0.mkdir()
        home_isd = Path.home() / "ISD"
        if home_isd.is_dir() and (home_isd / "Makefile").is_file():
            sibling = self.root / "ISD"
            sibling.mkdir()
            (sibling / "Makefile").write_text("# stub\n")
            result = subprocess.run(
                ["bash", str(RESOLVER), str(ir0)],
                text=True,
                capture_output=True,
                check=False,
            )
            self.assertEqual(result.returncode, 0)
            self.assertEqual(result.stdout.strip(), str(sibling.resolve()))

    def test_kmang_rejects_release_mismatch(self) -> None:
        machine = self.root / "machine"
        source = self.root / "mismatch.iso"
        obj = self.root / "mismatch.o"
        subprocess.run(
            ["cc", "-c", "-x", "c", "-o", str(obj), "-"],
            input=fake_kernel_c_source("0.0.1-rc5", b"kernel-mismatch"),
            text=True,
            capture_output=True,
            check=True,
        )
        subprocess.run(
            ["ld", "-r", "--defsym=ir0_build_number=7", "-o", str(self.root / "k.bin"), str(obj)],
            capture_output=True,
            check=True,
        )
        subprocess.run(
            ["xorriso", "-outdev", str(source), "-map", str(self.root / "k.bin"),
             "/boot/kernel-x64.bin"],
            capture_output=True,
            check=True,
        )
        result = subprocess.run(
            ["python3", str(MANAGER), "--machine-dir", str(machine),
             "install", "--source", str(source), "--id", "0.0.1-rc6-build7"],
            text=True,
            capture_output=True,
            check=False,
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("0.0.1-rc5", result.stderr)

    def test_usmang_verify_unknown_without_staged_rootfs(self) -> None:
        isd = self.root / "ISD"
        isd.mkdir()
        (isd / "Makefile").write_text("all:\n")
        (isd / "scripts").mkdir()
        shutil.copy(
            ROOT.parent / "ISD" / "scripts" / "verify-profile-rootfs.sh",
            isd / "scripts" / "verify-profile-rootfs.sh",
        )
        (isd / "profiles" / "minimal").mkdir(parents=True)
        (isd / "profiles" / "minimal" / "verify-paths.txt").write_text(
            "busybox bin/busybox\n"
        )
        result = subprocess.run(
            ["python3", str(USMANG), "--isd-root", str(isd),
             "--profile", "minimal", "verify"],
            text=True,
            capture_output=True,
            check=False,
        )
        self.assertEqual(result.returncode, 3)

    def test_usmang_verify_error_on_missing_binary(self) -> None:
        isd = self.root / "ISD"
        staged = isd / "out" / "x86_64" / "rootfs" / "minimal"
        (staged / "etc").mkdir(parents=True)
        (staged / "etc" / "ir0-profile").write_text("minimal\n")
        (isd / "Makefile").write_text("all:\n")
        (isd / "scripts").mkdir()
        shutil.copy(
            ROOT.parent / "ISD" / "scripts" / "verify-profile-rootfs.sh",
            isd / "scripts" / "verify-profile-rootfs.sh",
        )
        (isd / "profiles" / "minimal").mkdir(parents=True, exist_ok=True)
        (isd / "profiles" / "minimal" / "verify-paths.txt").write_text(
            "busybox bin/busybox\n"
        )
        result = subprocess.run(
            ["python3", str(USMANG), "--isd-root", str(isd),
             "--profile", "minimal", "verify"],
            text=True,
            capture_output=True,
            check=False,
        )
        self.assertEqual(result.returncode, 2)

    def test_isd_check_ir0_interface_rejects_missing_file(self) -> None:
        if not ISD_CHECK.is_file():
            self.skipTest("sibling ISD checkout not present")
        fake_ir0 = self.root / "IR0"
        fake_ir0.mkdir()
        result = subprocess.run(
            ["bash", str(ISD_CHECK), str(fake_ir0)],
            text=True,
            capture_output=True,
            check=False,
            cwd=str(ISD_CHECK.parent.parent),
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("IR0_ISD_INTERFACE", result.stderr + result.stdout)

    def test_isd_check_ir0_interface_ok_with_contract(self) -> None:
        if not ISD_CHECK.is_file():
            self.skipTest("sibling ISD checkout not present")
        fake_ir0 = self.root / "IR0"
        fake_ir0.mkdir()
        shutil.copy(ROOT / "IR0_ISD_INTERFACE", fake_ir0 / "IR0_ISD_INTERFACE")
        (fake_ir0 / "scripts").mkdir()
        shutil.copy(ROOT / "scripts/inject_init_minix.py", fake_ir0 / "scripts/inject_init_minix.py")
        shutil.copy(ROOT / "scripts/verify_minix_rootfs.py", fake_ir0 / "scripts/verify_minix_rootfs.py")
        shutil.copy(ROOT / "scripts/verify_ext2_rootfs.sh", fake_ir0 / "scripts/verify_ext2_rootfs.sh")
        # Minimal Makefile stubs for PUBLIC_TARGET= checks in ISD.
        (fake_ir0 / "Makefile").write_text(
            "KERNEL_ROOT:=$(CURDIR)\n"
            "ir0-minix-inject-path:\n"
            "\t@echo $(KERNEL_ROOT)/scripts/inject_init_minix.py\n"
            "ir0-verify-minix-path:\n"
            "\t@echo $(KERNEL_ROOT)/scripts/verify_minix_rootfs.py\n"
            "verify-ext2-rootfs:\n"
            "\t@true\n"
            "verify-rootfs:\n"
            "\t@true\n"
            "headers_install:\n"
            "\t@true\n"
            "kernel-x64-userspace.iso:\n"
            "\t@true\n"
        )
        result = subprocess.run(
            ["bash", str(ISD_CHECK), str(fake_ir0)],
            text=True,
            capture_output=True,
            check=False,
            cwd=str(ISD_CHECK.parent.parent),
        )
        self.assertEqual(result.returncode, 0, result.stderr)


if __name__ == "__main__":
    unittest.main()
