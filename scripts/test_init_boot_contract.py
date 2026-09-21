#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Host tests for init boot contract and capture harness."""

from __future__ import annotations

import importlib.util
import json
import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
CAPTURE = ROOT / "scripts" / "init_boot_capture.py"
CONTRACT = ROOT / "scripts" / "init_boot_contract.json"


def load_capture():
    spec = importlib.util.spec_from_file_location("init_boot_capture", CAPTURE)
    if spec is None or spec.loader is None:
        raise RuntimeError("cannot load init_boot_capture.py")
    module = importlib.util.module_from_spec(spec)
    sys.modules["init_boot_capture"] = module
    spec.loader.exec_module(module)
    return module


IB = load_capture()


class InitBootContractTest(unittest.TestCase):
    def test_contract_profiles_cover_alt_inits(self) -> None:
        data = json.loads(CONTRACT.read_text(encoding="utf-8"))
        profiles = data["profiles"]
        self.assertIn("minimal", profiles)
        self.assertIn("minimal-sysvinit", profiles)
        self.assertIn("minimal-openrc", profiles)
        self.assertEqual(profiles["minimal"]["init_system"], "runit")
        self.assertEqual(profiles["minimal-sysvinit"]["init_system"], "sysvinit")
        self.assertEqual(profiles["minimal-openrc"]["init_system"], "openrc")

    def test_arm64_scaffold_documents_iso(self) -> None:
        data = json.loads(CONTRACT.read_text(encoding="utf-8"))
        arm = data["architectures"]["arm64"]
        self.assertEqual(arm["status"], "scaffold")
        self.assertIn("kernel-arm64", arm["kernel_iso"])

    def test_extract_boot_window(self) -> None:
        text = "noise\nSYSVINIT_BOOT_OK\nmiddle\nGETTY_READY\nafter"
        window = IB.extract_boot_window(text, "SYSVINIT_BOOT_OK", "GETTY_READY")
        self.assertIn("SYSVINIT_BOOT_OK", window)
        self.assertIn("GETTY_READY", window)
        self.assertNotIn("after", window)

    def test_tag_report_missing(self) -> None:
        report = IB.tag_report("SYSVINIT_BOOT_OK", ["SYSVINIT_BOOT_OK", "GETTY_READY"], [])
        self.assertEqual(report["missing_required"], ["GETTY_READY"])


if __name__ == "__main__":
    unittest.main()
