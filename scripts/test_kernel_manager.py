#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Host behavior tests for the persistent-machine kernel store."""

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
import curses
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
MANAGER = ROOT / "scripts/kernel_manager.py"


def load_kernel_manager():
    spec = importlib.util.spec_from_file_location("kernel_manager", MANAGER)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"cannot load {MANAGER}")
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


KM = load_kernel_manager()


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

    def test_prune_keeps_default_and_fallback(self) -> None:
        self.install("0.0.1-rc5-build41", b"kernel-41")
        self.install("0.0.1-rc5-build42", b"kernel-42")
        self.install("0.0.1-rc5-build43", b"kernel-43")
        self.run_manager("prune")
        listing = self.run_manager("list").stdout
        self.assertIn("0.0.1-rc5-build43\t", listing)
        self.assertIn("0.0.1-rc5-build42\t", listing)
        self.assertNotIn("0.0.1-rc5-build41\t", listing)

    def test_compare_reports_diverged_workspace(self) -> None:
        self.install("0.0.1-rc5-build41", b"kernel-41")
        newer = self.root / "workspace.iso"
        kernel = self.root / "workspace.bin"
        obj = self.root / "workspace.o"
        subprocess.run(
            ["cc", "-c", "-x", "c", "-o", str(obj), "-"],
            input='const char payload[] = "kernel-99";\n',
            text=True, capture_output=True, check=True,
        )
        subprocess.run(
            ["ld", "-r", "--defsym=ir0_build_number=99", "-o", str(kernel), str(obj)],
            capture_output=True, check=True,
        )
        subprocess.run(
            ["xorriso", "-outdev", str(newer), "-map", str(kernel),
             "/boot/kernel-x64.bin"],
            capture_output=True, check=True,
        )
        result = self.run_manager(
            "--source", str(newer), "--version", "0.0.1-rc5",
            "compare", success=False,
        )
        self.assertEqual(result.returncode, 1)
        report = json.loads(result.stdout)
        self.assertEqual(report["relation"], "diverged")
        self.assertTrue(report["needs_install"])
        self.assertEqual(report["workspace_id"], "0.0.1-rc5-build99")
        self.assertEqual(report["default_id"], "0.0.1-rc5-build41")

    def test_info_marks_build_scope_machine_local(self) -> None:
        self.install("0.0.1-rc5-build41", b"kernel-41")
        payload = json.loads(
            self.run_manager("info", "0.0.1-rc5-build41", "--json").stdout
        )
        self.assertEqual(payload["build_scope"], "machine-local")
        self.assertEqual(payload["embedded_build"], 41)

    def test_help_command_lists_every_tui_action_key(self) -> None:
        result = self.run_manager("help")
        text = result.stdout
        self.assertIn("key legend", text)
        for key in sorted(KM.KMANG_TUI_ACTION_KEYS):
            self.assertIn(key, text, msg=f"help text missing TUI key {key!r}")
        self.assertIn("Enter", text)
        self.assertIn("PgUp", text)
        self.assertIn(".build_number", text)

    def test_help_compact_mentions_primary_bindings(self) -> None:
        full = KM.format_help_compact(200)
        for fragment in ("h/?", "q", "i", "Enter", "b", "d/p"):
            self.assertIn(fragment, full)
        narrow = KM.format_help_compact(76)
        self.assertIn("h/?", narrow)
        self.assertTrue(narrow.startswith("Keys:"))

    def test_help_sections_have_unique_titles(self) -> None:
        titles = [section.title for section in KM.KMANG_HELP_SECTIONS]
        self.assertEqual(len(titles), len(set(titles)))

    def test_resolve_fails_without_enrolled_kernels(self) -> None:
        result = self.run_manager("resolve", success=False)
        self.assertEqual(result.returncode, 2)
        self.assertIn("no verified", result.stderr)

    def test_install_workspace_selects_current(self) -> None:
        source = self.root / "workspace.iso"
        obj = self.root / "ws.o"
        subprocess.run(
            ["cc", "-c", "-x", "c", "-o", str(obj), "-"],
            input='const char payload[] = "kernel-55";\n',
            text=True, capture_output=True, check=True,
        )
        subprocess.run(
            ["ld", "-r", "--defsym=ir0_build_number=55", "-o", str(self.root / "ws.bin"), str(obj)],
            capture_output=True, check=True,
        )
        subprocess.run(
            ["xorriso", "-outdev", str(source), "-map", str(self.root / "ws.bin"),
             "/boot/kernel-x64.bin"],
            capture_output=True, check=True,
        )
        self.run_manager(
            "--source", str(source), "--version", "0.0.1-rc5", "install-workspace"
        )
        listing = self.run_manager("list").stdout
        self.assertIn("0.0.1-rc5-build55\tcurrent", listing)
        resolved = Path(self.run_manager("resolve").stdout.strip())
        self.assertEqual(resolved, self.machine / "kernels/0.0.1-rc5-build55.iso")

    def test_compare_identical_when_default_matches_workspace(self) -> None:
        self.install("0.0.1-rc5-build41", b"kernel-41")
        source = self.root / "workspace.iso"
        shutil.copy(
            self.machine / "kernels/0.0.1-rc5-build41.iso",
            source,
        )
        result = self.run_manager(
            "--source", str(source), "--version", "0.0.1-rc5", "compare"
        )
        report = json.loads(result.stdout)
        self.assertEqual(report["relation"], "identical")
        self.assertFalse(report["needs_install"])

    def test_verify_rejects_invalid_iso_payload(self) -> None:
        source = self.root / "bad.iso"
        junk = self.root / "junk.bin"
        junk.write_bytes(b"not-a-kernel")
        subprocess.run(
            ["xorriso", "-outdev", str(source), "-map", str(junk), "/boot/kernel-x64.bin"],
            capture_output=True, check=True,
        )
        result = self.run_manager(
            "install", "--source", str(source), "--id", "0.0.1-rc5-build77",
            success=False,
        )
        self.assertIn("no embedded build identity", result.stderr)

    def test_select_rejects_missing_kernel(self) -> None:
        result = self.run_manager("select", "missing-build1", success=False)
        self.assertEqual(result.returncode, 2)
        self.assertIn("not installed", result.stderr)

    def test_metadata_written_on_install(self) -> None:
        self.install("0.0.1-rc5-build41", b"kernel-41")
        meta = json.loads(
            (self.machine / "kernels/0.0.1-rc5-build41.json").read_text()
        )
        self.assertEqual(meta["format"], 3)
        self.assertEqual(meta["id"], "0.0.1-rc5-build41")
        self.assertEqual(meta["embedded_build"], 41)
        self.assertEqual(meta["build_scope"], "machine-local")
        self.assertTrue(meta["sha256"])

    def test_concurrent_manager_is_rejected(self) -> None:
        import fcntl

        self.machine.mkdir(parents=True, exist_ok=True)
        lock_path = self.machine / ".kernel-manager.lock"
        lock_path.touch()
        lock_stream = lock_path.open("a+")
        fcntl.flock(lock_stream.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
        try:
            result = subprocess.run(
                ["python3", str(MANAGER), "--machine-dir", str(self.machine), "list"],
                text=True, capture_output=True, check=False,
            )
            self.assertEqual(result.returncode, 2)
            self.assertIn("another kernel manager is active", result.stderr)
        finally:
            fcntl.flock(lock_stream.fileno(), fcntl.LOCK_UN)
            lock_stream.close()

    def test_tui_requires_interactive_terminal(self) -> None:
        result = subprocess.run(
            ["python3", str(MANAGER), "--machine-dir", str(self.machine), "tui"],
            text=True, capture_output=True, check=False,
            stdin=subprocess.DEVNULL,
        )
        self.assertEqual(result.returncode, 2)
        self.assertIn("requires an interactive terminal", result.stderr)

    def test_iso_probe_accepts_extractable_payload_path(self) -> None:
        store = KM.KernelStore(self.machine)
        self.install("0.0.1-rc5-build41", b"kernel-41")
        image = self.machine / "kernels/0.0.1-rc5-build41.iso"
        self.assertTrue(store._iso_has_bootable_kernel(image))
        junk = self.root / "empty.iso"
        junk.write_bytes(b"not an iso")
        self.assertFalse(store._iso_has_bootable_kernel(junk))

    def test_desktop_boot_capable_profiles(self) -> None:
        desktop = KM.KernelStore(self.machine, profile="desktop")
        console = KM.KernelStore(self.machine, profile="desktop-console")
        minimal = KM.KernelStore(self.machine, profile="minimal")
        self.assertTrue(desktop.desktop_boot_capable())
        self.assertTrue(console.desktop_boot_capable())
        self.assertFalse(minimal.desktop_boot_capable())

    def test_write_login_session_injects_one_shot_file(self) -> None:
        disk = self.machine / "disk.img"
        disk.parent.mkdir(parents=True, exist_ok=True)
        disk.touch()
        inject = ROOT / "scripts/inject_init_minix.py"
        subprocess.run(
            [sys.executable, str(inject), "--format", str(disk)],
            capture_output=True, check=True,
        )
        store = KM.KernelStore(
            self.machine,
            profile="desktop",
            kernel_root=ROOT,
            machine_disk=disk,
        )
        store.write_login_session(KM.KMANG_SESSION_X)
        verify = subprocess.run(
            [sys.executable, str(ROOT / "scripts/verify_minix_rootfs.py"),
             str(disk), "/etc/ir0-session"],
            text=True,
            capture_output=True,
            check=False,
        )
        self.assertEqual(verify.returncode, 0, verify.stdout + verify.stderr)

    def test_prompt_boot_session_maps_keys(self) -> None:
        class FakeScreen:
            def __init__(self, key: int) -> None:
                self._key = key

            def getmaxyx(self) -> tuple[int, int]:
                return (12, 80)

            def erase(self) -> None:
                return None

            def addstr(self, *_args, **_kwargs) -> None:
                return None

            def refresh(self) -> None:
                return None

            def getch(self) -> int:
                return self._key

        self.assertEqual(
            KM._prompt_boot_session(FakeScreen(ord("x")), "0.0.1-rc5-build1"),
            KM.KMANG_SESSION_X,
        )
        self.assertEqual(
            KM._prompt_boot_session(FakeScreen(ord("t")), "0.0.1-rc5-build1"),
            KM.KMANG_SESSION_TERMINAL,
        )
        self.assertIsNone(
            KM._prompt_boot_session(FakeScreen(ord("q")), "0.0.1-rc5-build1"),
        )

    def test_tui_arm_delete_requires_second_press(self) -> None:
        state = KM.tui_confirm_reset()
        state, message, confirmed = KM.tui_arm_delete(state, "build-1")
        self.assertFalse(confirmed)
        self.assertEqual(state.pending_delete, "build-1")
        self.assertIn("Press d again", message)
        state, message, confirmed = KM.tui_arm_delete(state, "build-1")
        self.assertTrue(confirmed)
        self.assertIsNone(state.pending_delete)
        self.assertEqual(message, "Kernel removed")

    def test_tui_arm_delete_switches_target_without_confirming(self) -> None:
        state = KM.tui_confirm_reset()
        state, _, _ = KM.tui_arm_delete(state, "build-1")
        state, message, confirmed = KM.tui_arm_delete(state, "build-2")
        self.assertFalse(confirmed)
        self.assertEqual(state.pending_delete, "build-2")
        self.assertIn("build-2", message)

    def test_tui_arm_prune_requires_second_press(self) -> None:
        state = KM.tui_confirm_reset()
        state, message, confirmed = KM.tui_arm_prune(state)
        self.assertFalse(confirmed)
        self.assertTrue(state.pending_prune)
        self.assertIn("Press p again", message)
        state, _, confirmed = KM.tui_arm_prune(state)
        self.assertTrue(confirmed)
        self.assertFalse(state.pending_prune)

    def test_tui_arm_delete_clears_pending_prune(self) -> None:
        state = KM.TuiConfirmState(pending_prune=True)
        state, _, confirmed = KM.tui_arm_delete(state, "build-9")
        self.assertFalse(confirmed)
        self.assertEqual(state.pending_delete, "build-9")
        self.assertFalse(state.pending_prune)

    def test_read_help_scroll_key_maps_navigation(self) -> None:
        class FakeScreen:
            def __init__(self, key: int) -> None:
                self._key = key

            def getch(self) -> int:
                return self._key

        scroll, close = KM._read_help_scroll_key(FakeScreen(curses.KEY_UP), 3, 10)
        self.assertEqual(scroll, 2)
        self.assertFalse(close)
        scroll, close = KM._read_help_scroll_key(FakeScreen(ord("j")), 0, 10)
        self.assertEqual(scroll, 1)
        self.assertFalse(close)
        scroll, close = KM._read_help_scroll_key(FakeScreen(ord("q")), 1, 10)
        self.assertEqual(scroll, 1)
        self.assertTrue(close)

    def test_boot_session_prompt_reads_isd_profile_flag(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            isd = Path(directory)
            profile_dir = isd / "profiles" / "desktop"
            profile_dir.mkdir(parents=True)
            (profile_dir / "profile.conf").write_text(
                "PROFILE_NAME=desktop\nKMANG_BOOT_PROMPT=0\n"
            )
            store = KM.KernelStore(self.machine, profile="desktop", isd_root=isd)
            self.assertFalse(store.boot_session_prompt_capable())
            (profile_dir / "profile.conf").write_text(
                "PROFILE_NAME=desktop\nKMANG_BOOT_PROMPT=1\n"
            )
            self.assertTrue(store.boot_session_prompt_capable())

    def test_boot_session_prompt_fallback_without_isd(self) -> None:
        minimal = KM.KernelStore(self.machine, profile="minimal")
        self.assertFalse(minimal.boot_session_prompt_capable())
        desktop = KM.KernelStore(self.machine, profile="desktop")
        self.assertTrue(desktop.boot_session_prompt_capable())


if __name__ == "__main__":
    unittest.main()
