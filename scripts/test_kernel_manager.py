#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Host behavior tests for the persistent-machine kernel store."""

from __future__ import annotations

import subprocess
import tempfile
import unittest
import re
import json
import os
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
MANAGER = ROOT / "scripts/kernel_manager.py"


class KernelManagerTest(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        self.machine = self.root / "machine"

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def run_manager(self, *arguments: str, success: bool = True) -> subprocess.CompletedProcess[str]:
        result = subprocess.run(
            ["python3", str(MANAGER), "--machine-dir", str(self.machine), *arguments],
            text=True,
            capture_output=True,
            check=False,
        )
        if success:
            self.assertEqual(result.returncode, 0, result.stderr)
        return result

    def install(self, kernel_id: str, contents: bytes) -> None:
        source = self.root / f"source-{kernel_id}.iso"
        kernel = self.root / f"kernel-{kernel_id}.bin"
        match = re.search(r"-build([0-9]+)$", kernel_id)
        if match:
            obj = self.root / f"kernel-{kernel_id}.o"
            subprocess.run(
                ["cc", "-c", "-x", "c", "-o", str(obj), "-"],
                input=f"const char payload[] = {contents!r};\n".replace("b'", "\"").replace("';", "\";"),
                text=True, capture_output=True, check=True,
            )
            subprocess.run(
                ["ld", "-r", f"--defsym=ir0_build_number={match.group(1)}",
                 "-o", str(kernel), str(obj)], capture_output=True, check=True,
            )
        else:
            kernel.write_bytes(contents)
        subprocess.run(
            ["xorriso", "-outdev", str(source), "-map", str(kernel),
             "/boot/kernel-x64.bin"],
            capture_output=True, check=True,
        )
        self.run_manager("install", "--source", str(source), "--id", kernel_id)

    def test_install_select_and_fallback_are_observable(self) -> None:
        self.install("0.0.1-rc5-build41", b"kernel-41")
        self.install("0.0.1-rc5-build42", b"kernel-42")
        listing = self.run_manager("list").stdout
        self.assertIn("0.0.1-rc5-build42\tcurrent", listing)
        self.assertIn("0.0.1-rc5-build41\tfallback", listing)
        resolved = Path(self.run_manager("resolve").stdout.strip())
        self.assertEqual(resolved, self.machine / "kernels/0.0.1-rc5-build42.iso")

        self.run_manager("select", "0.0.1-rc5-build41")
        listing = self.run_manager("list").stdout
        self.assertIn("0.0.1-rc5-build41\tcurrent", listing)
        self.assertIn("0.0.1-rc5-build42\tfallback", listing)

    def test_current_and_fallback_cannot_be_deleted(self) -> None:
        self.install("build1", b"one")
        self.install("build2", b"two")
        for kernel_id in ("build1", "build2"):
            result = self.run_manager("delete", kernel_id, success=False)
            self.assertEqual(result.returncode, 2)
            self.assertIn("cannot be removed", result.stderr)

    def test_unselected_kernel_can_be_deleted(self) -> None:
        self.install("build1", b"one")
        self.install("build2", b"two")
        self.install("build3", b"three")
        self.run_manager("delete", "build1")
        self.assertNotIn("build1", self.run_manager("list").stdout)

    def test_rejects_unsafe_identifier(self) -> None:
        source = self.root / "kernel.iso"
        source.write_bytes(b"not reached because the identifier is rejected")
        result = self.run_manager(
            "install", "--source", str(source), "--id", "../escape", success=False
        )
        self.assertEqual(result.returncode, 2)
        self.assertFalse((self.root / "escape.iso").exists())

    def test_corrupt_current_resolves_to_verified_fallback(self) -> None:
        self.install("build1", b"one")
        self.install("build2", b"two")
        current = self.machine / "kernels/build2.iso"
        with current.open("ab") as stream:
            stream.write(b"tampered")
        resolved = Path(self.run_manager("resolve").stdout.strip())
        self.assertEqual(resolved, self.machine / "kernels/build1.iso")
        listing = self.run_manager("list").stdout
        self.assertIn("build2\tcurrent,invalid:checksum mismatch", listing)

    def test_same_identifier_cannot_replace_different_image(self) -> None:
        self.install("build1", b"one")
        source = self.root / "replacement.iso"
        kernel = self.root / "replacement.bin"
        kernel.write_bytes(b"different")
        subprocess.run(
            ["xorriso", "-outdev", str(source), "-map", str(kernel),
             "/boot/kernel-x64.bin"], capture_output=True, check=True,
        )
        result = self.run_manager(
            "install", "--source", str(source), "--id", "build1", success=False
        )
        self.assertEqual(result.returncode, 2)
        self.assertIn("collision", result.stderr)

    def test_rejects_label_that_disagrees_with_embedded_build(self) -> None:
        self.install("0.0.1-rc5-build41", b"kernel-41")
        source = self.root / "mismatch.iso"
        installed = self.machine / "kernels/0.0.1-rc5-build41.iso"
        subprocess.run(
            ["xorriso", "-osirrox", "on", "-indev", str(installed),
             "-extract", "/boot/kernel-x64.bin", str(self.root / "mismatch.bin")],
            capture_output=True, check=True,
        )
        subprocess.run(
            ["xorriso", "-outdev", str(source), "-map", str(self.root / "mismatch.bin"),
             "/boot/kernel-x64.bin"], capture_output=True, check=True,
        )
        result = self.run_manager(
            "install", "--source", str(source),
            "--id", "0.0.1-rc5-build42", success=False,
        )
        self.assertIn("image contains build41", result.stderr)

    def test_metadata_cannot_claim_a_different_kernel(self) -> None:
        self.install("0.0.1-rc5-build41", b"kernel-41")
        metadata = self.machine / "kernels/0.0.1-rc5-build41.json"
        contents = json.loads(metadata.read_text())
        contents["id"] = "0.0.1-rc5-build99"
        metadata.write_text(json.dumps(contents))
        result = self.run_manager("verify", "0.0.1-rc5-build41", success=False)
        self.assertEqual(result.returncode, 2)
        self.assertIn("bad metadata identity", result.stdout)

    def test_builds_are_sorted_numerically_newest_first(self) -> None:
        for build in (850, 1074, 862):
            self.install(f"0.0.1-rc5-build{build}", f"kernel-{build}".encode())
        identifiers = [line.split("\t", 1)[0]
                       for line in self.run_manager("list").stdout.splitlines()]
        self.assertEqual(identifiers, [
            "0.0.1-rc5-build1074",
            "0.0.1-rc5-build862",
            "0.0.1-rc5-build850",
        ])

    def test_catalog_architecture_cannot_override_elf(self) -> None:
        self.install("0.0.1-rc5-build41", b"kernel-41")
        source = self.machine / "kernels/0.0.1-rc5-build41.iso"
        other_machine = self.root / "arm-machine"
        result = subprocess.run([
            "python3", str(MANAGER), "--machine-dir", str(other_machine),
            "--arch", "arm64", "install", "--source", str(source),
            "--id", "0.0.1-rc5-build41",
        ], text=True, capture_output=True, check=False)
        self.assertEqual(result.returncode, 2)
        self.assertIn("catalog is arm64, image is x86_64", result.stderr)

    def test_architecture_detection_is_independent_of_user_locale(self) -> None:
        self.install("0.0.1-rc5-build41", b"kernel-41")
        environment = os.environ.copy()
        environment["LANG"] = "es_AR.UTF-8"
        environment["LC_ALL"] = "es_AR.UTF-8"
        result = subprocess.run([
            "python3", str(MANAGER), "--machine-dir", str(self.machine),
            "--arch", "x86_64", "verify", "0.0.1-rc5-build41",
        ], text=True, capture_output=True, check=False, env=environment)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("verified", result.stdout)


if __name__ == "__main__":
    unittest.main()
