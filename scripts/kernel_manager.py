#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Manage versioned boot ISOs for one persistent IR0 machine."""

from __future__ import annotations

import argparse
import curses
import fcntl
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path


ID_RE = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._+-]*$")
BUILD_ID_RE = re.compile(r"-build([0-9]+)$")


class KernelStore:
    def __init__(self, machine_dir: Path, arch: str = "unknown",
                 profile: str = "unknown", machine: str = "unknown") -> None:
        self.machine_dir = machine_dir.resolve()
        self.arch = arch
        self.profile = profile
        self.machine = machine
        self.kernels = self.machine_dir / "kernels"
        self.current = self.machine_dir / "kernel-current.iso"
        self.fallback = self.machine_dir / "kernel-fallback.iso"
        self.lock_path = self.machine_dir / ".kernel-manager.lock"

    def metadata_path(self, kernel_id: str) -> Path:
        return self.kernels / f"{kernel_id}.json"

    @staticmethod
    def _inspection_env() -> dict[str, str]:
        """Keep binutils/xorriso output machine-readable under any user locale."""
        environment = os.environ.copy()
        environment["LC_ALL"] = "C"
        return environment

    @staticmethod
    def checksum(path: Path) -> str:
        digest = hashlib.sha256()
        with path.open("rb") as stream:
            for chunk in iter(lambda: stream.read(1024 * 1024), b""):
                digest.update(chunk)
        return digest.hexdigest()

    @staticmethod
    def _fsync_dir(path: Path) -> None:
        """Persist directory-entry changes across an abrupt host shutdown."""
        descriptor = os.open(path, os.O_RDONLY | os.O_DIRECTORY)
        try:
            os.fsync(descriptor)
        finally:
            os.close(descriptor)

    @staticmethod
    def _extract_kernel(image: Path, destination: Path) -> bool:
        """Extract the first supported kernel payload without trusting its name."""
        for member in ("/boot/kernel-x64.bin", "/boot/kernel-arm64.bin",
                       "/boot/kernel.bin"):
            extract = subprocess.run(
                ["xorriso", "-osirrox", "on", "-indev", str(image),
                 "-extract", member, str(destination)],
                text=True, capture_output=True, check=False,
                env=KernelStore._inspection_env(),
            )
            if extract.returncode == 0 and destination.is_file():
                return True
            destination.unlink(missing_ok=True)
        return False

    @staticmethod
    def embedded_build(image: Path) -> int | None:
        """Read the link-time build identity from the kernel inside an ISO."""
        with tempfile.TemporaryDirectory(prefix="ir0-kernel-verify.") as directory:
            kernel = Path(directory) / "kernel.bin"
            if not KernelStore._extract_kernel(image, kernel):
                return None
            symbols = subprocess.run(
                ["nm", "-n", str(kernel)], text=True, capture_output=True,
                check=False, env=KernelStore._inspection_env(),
            )
            if symbols.returncode != 0:
                return None
            for line in symbols.stdout.splitlines():
                fields = line.split()
                if len(fields) >= 3 and fields[-1] == "ir0_build_number":
                    try:
                        return int(fields[0], 16)
                    except ValueError:
                        return None
        return None

    @staticmethod
    def embedded_arch(image: Path) -> str | None:
        """Derive ISA from ELF e_machine inside the ISO, never from labels."""
        with tempfile.TemporaryDirectory(prefix="ir0-kernel-arch.") as directory:
            kernel = Path(directory) / "kernel.bin"
            if not KernelStore._extract_kernel(image, kernel):
                return None
            header = kernel.read_bytes()[:20]
            if len(header) < 20 or header[:4] != b"\x7fELF":
                return None
            byteorder = "little" if header[5] == 1 else "big" if header[5] == 2 else None
            if byteorder is None:
                return None
            machine = int.from_bytes(header[18:20], byteorder=byteorder)
            if machine == 62:  # EM_X86_64
                return "x86_64"
            if machine == 183:  # EM_AARCH64
                return "arm64"
        return None

    @staticmethod
    def identifier_build(kernel_id: str) -> int | None:
        match = BUILD_ID_RE.search(kernel_id)
        return int(match.group(1)) if match else None

    def verify(self, kernel_id: str, enroll: bool = False) -> tuple[bool, str]:
        image = self.kernels / f"{kernel_id}.iso"
        if not image.is_file():
            return False, "missing"
        probe = subprocess.run(
            ["xorriso", "-indev", str(image), "-ls", "/boot/kernel-x64.bin"],
            text=True, capture_output=True, check=False,
            env=self._inspection_env(),
        )
        if probe.returncode != 0 or "/boot/kernel-x64.bin" not in probe.stdout:
            return False, "invalid ISO"
        digest = self.checksum(image)
        embedded = self.embedded_build(image)
        embedded_arch = self.embedded_arch(image)
        expected = self.identifier_build(kernel_id)
        if embedded_arch is None and self.arch != "unknown":
            return False, "architecture inspection failed"
        if self.arch != "unknown" and embedded_arch != self.arch:
            return False, f"architecture mismatch: image is {embedded_arch or 'unknown'}"
        if embedded is not None and expected is not None and embedded != expected:
            return False, f"identity mismatch: image build{embedded}"
        meta_path = self.metadata_path(kernel_id)
        if meta_path.is_file():
            try:
                metadata = json.loads(meta_path.read_text())
            except (OSError, json.JSONDecodeError):
                return False, "bad metadata"
            if metadata.get("format") not in (1, 2) or metadata.get("id") != kernel_id:
                return False, "bad metadata identity"
            if metadata.get("arch") not in (None, "unknown", self.arch):
                return False, "architecture mismatch"
            if metadata.get("embedded_arch") not in (None, embedded_arch):
                return False, "embedded architecture changed"
            if metadata.get("sha256") != digest or metadata.get("size") != image.stat().st_size:
                return False, "checksum mismatch"
            recorded = metadata.get("embedded_build")
            if recorded is not None and recorded != embedded:
                return False, "embedded identity changed"
            if enroll and metadata.get("arch") in (None, "unknown"):
                self._write_metadata(kernel_id, image, digest, embedded)
            if embedded is not None and expected == embedded:
                return True, "verified"
            return True, "checksum-only"
        if enroll:
            self._write_metadata(kernel_id, image, digest, embedded)
            if embedded is not None and expected == embedded:
                return True, "verified"
            return True, "checksum-only"
        return True, "legacy-unverified"

    def _write_metadata(self, kernel_id: str, image: Path, digest: str,
                        embedded_build: int | None) -> None:
        target = self.metadata_path(kernel_id)
        payload = {
            "format": 2, "id": kernel_id, "sha256": digest,
            "size": image.stat().st_size, "installed_at": int(time.time()),
            "embedded_build": embedded_build,
            "arch": self.arch,
            "embedded_arch": self.embedded_arch(image),
        }
        temporary = target.with_name(f".{target.name}.new.{os.getpid()}")
        try:
            with temporary.open("w") as stream:
                stream.write(json.dumps(payload, sort_keys=True, indent=2) + "\n")
                stream.flush()
                os.fsync(stream.fileno())
            os.replace(temporary, target)
            self._fsync_dir(target.parent)
        finally:
            temporary.unlink(missing_ok=True)

    def installed(self) -> list[str]:
        if not self.kernels.is_dir():
            return []
        entries = [p.stem for p in self.kernels.glob("*.iso") if p.is_file()]
        return sorted(
            entries,
            key=lambda kernel_id: (
                self.identifier_build(kernel_id) is not None,
                self.identifier_build(kernel_id) or -1,
                kernel_id,
            ),
            reverse=True,
        )

    @staticmethod
    def link_id(link: Path) -> str | None:
        if not link.is_symlink():
            return None
        target = (link.parent / os.readlink(link)).resolve()
        return target.stem if target.is_file() else None

    def current_id(self) -> str | None:
        return self.link_id(self.current)

    def fallback_id(self) -> str | None:
        return self.link_id(self.fallback)

    def _atomic_link(self, link: Path, target: Path) -> None:
        link.parent.mkdir(parents=True, exist_ok=True)
        relative = os.path.relpath(target, link.parent)
        temporary = link.with_name(f".{link.name}.new.{os.getpid()}")
        try:
            temporary.symlink_to(relative)
            os.replace(temporary, link)
            self._fsync_dir(link.parent)
        finally:
            temporary.unlink(missing_ok=True)

    def select(self, kernel_id: str) -> None:
        target = self.kernels / f"{kernel_id}.iso"
        if not target.is_file():
            raise ValueError(f"kernel is not installed: {kernel_id}")
        valid, reason = self.verify(kernel_id, enroll=True)
        if not valid:
            raise ValueError(f"kernel {kernel_id} is not bootable: {reason}")
        old = self.current_id()
        if old and old != kernel_id:
            self._atomic_link(self.fallback, self.kernels / f"{old}.iso")
        self._atomic_link(self.current, target)

    def install(self, source: Path, kernel_id: str) -> None:
        if not ID_RE.fullmatch(kernel_id):
            raise ValueError(f"invalid kernel identifier: {kernel_id}")
        if not source.is_file():
            raise ValueError(f"missing kernel ISO: {source}")
        self.kernels.mkdir(parents=True, exist_ok=True)
        target = self.kernels / f"{kernel_id}.iso"
        source_digest = self.checksum(source)
        embedded = self.embedded_build(source)
        embedded_arch = self.embedded_arch(source)
        expected = self.identifier_build(kernel_id)
        if embedded_arch is None and self.arch != "unknown":
            raise ValueError("kernel ISO architecture could not be inspected")
        if self.arch != "unknown" and embedded_arch != self.arch:
            raise ValueError(
                f"kernel catalog is {self.arch}, image is {embedded_arch or 'unknown'}"
            )
        if expected is not None:
            if embedded is None:
                raise ValueError("kernel ISO has no embedded build identity")
            if embedded != expected:
                raise ValueError(
                    f"kernel id says build{expected}, image contains build{embedded}"
                )
        if target.exists():
            if self.checksum(target) != source_digest:
                raise ValueError(f"kernel id collision: {kernel_id}")
            self.verify(kernel_id, enroll=True)
            self.select(kernel_id)
            return
        fd, name = tempfile.mkstemp(prefix=f".{kernel_id}.", suffix=".iso", dir=self.kernels)
        os.close(fd)
        temporary = Path(name)
        try:
            shutil.copyfile(source, temporary)
            with temporary.open("rb") as stream:
                os.fsync(stream.fileno())
            os.replace(temporary, target)
            self._fsync_dir(target.parent)
        finally:
            temporary.unlink(missing_ok=True)
        self._write_metadata(kernel_id, target, source_digest, embedded)
        self.select(kernel_id)

    def source_identity(self, source: Path, version: str) -> tuple[str, str]:
        """Return a verified catalog id and ISA for a workspace ISO."""
        if not source.is_file():
            raise ValueError(f"missing workspace kernel ISO: {source}")
        embedded = self.embedded_build(source)
        embedded_arch = self.embedded_arch(source)
        if embedded is None:
            raise ValueError("workspace kernel has no embedded build identity")
        if embedded_arch is None:
            raise ValueError("workspace kernel architecture could not be inspected")
        if self.arch != "unknown" and embedded_arch != self.arch:
            raise ValueError(
                f"workspace catalog is {self.arch}, image is {embedded_arch}"
            )
        return f"{version}-build{embedded}", embedded_arch

    def delete(self, kernel_id: str) -> None:
        if kernel_id in {self.current_id(), self.fallback_id()}:
            raise ValueError("current and fallback kernels cannot be removed")
        target = self.kernels / f"{kernel_id}.iso"
        if not target.is_file():
            raise ValueError(f"kernel is not installed: {kernel_id}")
        target.unlink()
        self.metadata_path(kernel_id).unlink(missing_ok=True)
        self._fsync_dir(self.kernels)

    def resolve(self) -> Path:
        for link in (self.current, self.fallback):
            kernel_id = self.link_id(link)
            if kernel_id:
                valid, _ = self.verify(kernel_id, enroll=True)
                if valid:
                    return self.kernels / f"{kernel_id}.iso"
        raise ValueError("no verified current or fallback kernel")


def tui(screen: curses.window, store: KernelStore, make_args: list[str],
        source: Path | None, version: str | None) -> int:
    try:
        curses.curs_set(0)
    except curses.error:
        pass
    selected = 0
    pending_delete = None
    statuses: dict[str, tuple[bool, str]] = {}
    refresh_status = True
    message = "i install workspace  r rebuild+install  enter select  v verify  b boot  d delete  q quit"
    while True:
        entries = store.installed()
        workspace_id = None
        workspace_health = "not configured"
        if source is not None and version is not None:
            try:
                workspace_id, workspace_arch = store.source_identity(source, version)
                workspace_health = (
                    "installed" if workspace_id in entries else "not installed"
                )
                workspace_health += f", {workspace_arch}"
            except (OSError, ValueError) as error:
                workspace_health = f"unavailable: {error}"
        if refresh_status:
            statuses = {kernel_id: store.verify(kernel_id) for kernel_id in entries}
            refresh_status = False
        selected = min(selected, max(0, len(entries) - 1))
        screen.erase()
        screen.addstr(0, 2, "IR0 Kernel Manager", curses.A_BOLD)
        screen.addstr(1, 2, f"Context:  {store.arch} / {store.profile} / {store.machine}")
        screen.addstr(3, 2, f"Default:  {store.current_id() or 'workspace ISO'}")
        screen.addstr(4, 2, f"Fallback: {store.fallback_id() or '-'}")
        screen.addstr(5, 2, f"Workspace: {workspace_id or '-'} [{workspace_health}]")
        screen.addstr(7, 2, "Installed:", curses.A_BOLD)
        for index, kernel_id in enumerate(entries):
            marker = "*" if kernel_id == store.current_id() else " "
            fallback = " [fallback]" if kernel_id == store.fallback_id() else ""
            valid, state = statuses.get(kernel_id, (False, "unknown"))
            health = "ok" if valid and state == "verified" else state
            attr = curses.A_REVERSE if index == selected else 0
            screen.addstr(8 + index, 2,
                          f"{marker} [{store.arch}] {kernel_id} [{health}]{fallback}",
                          attr)
        screen.addstr(screen.getmaxyx()[0] - 2, 2, message[: screen.getmaxyx()[1] - 4])
        screen.refresh()
        key = screen.getch()
        try:
            if key in (ord("q"), 27):
                return 0
            if key == curses.KEY_UP and entries:
                selected = (selected - 1) % len(entries)
            elif key == curses.KEY_DOWN and entries:
                selected = (selected + 1) % len(entries)
            elif key in (10, 13) and entries:
                store.select(entries[selected])
                message = f"Selected {entries[selected]}; previous kernel retained as fallback"
                pending_delete = None
            elif key == ord("i"):
                if source is None or version is None:
                    raise ValueError("workspace install source/version is not configured")
                workspace_id, _ = store.source_identity(source, version)
                store.install(source.resolve(), workspace_id)
                message = f"Installed and selected {workspace_id}"
                refresh_status = True
            elif key == ord("r"):
                curses.endwin()
                result = subprocess.run(
                    ["make", "-s", "kernel-x64-userspace.iso", *make_args],
                    check=False,
                )
                screen.refresh()
                if result.returncode == 0 and source and version:
                    workspace_id, _ = store.source_identity(source, version)
                    store.install(source.resolve(), workspace_id)
                    message = f"Built, installed, and selected {workspace_id}"
                elif result.returncode == 0:
                    raise ValueError("TUI install source/version is not configured")
                else:
                    message = "Kernel build/install failed"
                refresh_status = True
            elif key == ord("d") and entries:
                if pending_delete != entries[selected]:
                    pending_delete = entries[selected]
                    message = f"Press d again to remove {entries[selected]}"
                else:
                    store.delete(entries[selected])
                    pending_delete = None
                    message = "Kernel removed"
                    refresh_status = True
            elif key == ord("v") and entries:
                valid, state = store.verify(entries[selected], enroll=True)
                message = f"{entries[selected]}: {state if valid else 'FAILED ' + state}"
                refresh_status = True
            elif key == ord("b"):
                store.resolve()
                return 10
        except (OSError, ValueError) as error:
            message = f"Error: {error}"


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--machine-dir", required=True, type=Path)
    parser.add_argument("--arch", default="unknown")
    parser.add_argument("--profile", default="unknown")
    parser.add_argument("--machine", default="unknown")
    parser.add_argument("--make-arg", action="append", default=[])
    parser.add_argument("--source", type=Path)
    parser.add_argument("--version")
    subparsers = parser.add_subparsers(dest="command", required=True)
    install = subparsers.add_parser("install")
    install.add_argument("--source", required=True, type=Path)
    install.add_argument("--id", required=True)
    subparsers.add_parser("install-workspace")
    select = subparsers.add_parser("select")
    select.add_argument("id")
    delete = subparsers.add_parser("delete")
    delete.add_argument("id")
    listing = subparsers.add_parser("list")
    listing.add_argument("--json", action="store_true")
    subparsers.add_parser("resolve")
    verify = subparsers.add_parser("verify")
    verify.add_argument("id", nargs="?")
    subparsers.add_parser("workspace")
    subparsers.add_parser("tui")
    args = parser.parse_args()
    store = KernelStore(args.machine_dir, args.arch, args.profile, args.machine)
    store.machine_dir.mkdir(parents=True, exist_ok=True)
    lock_stream = store.lock_path.open("a+")
    try:
        fcntl.flock(lock_stream.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
    except BlockingIOError:
        print("✗ another kernel manager is active for this machine", file=sys.stderr)
        return 2
    try:
        if args.command == "install":
            store.install(args.source.resolve(), args.id)
            print(f"✓ installed and selected kernel {args.id}")
        elif args.command == "install-workspace":
            if args.source is None or args.version is None:
                raise ValueError("workspace source/version is not configured")
            kernel_id, _ = store.source_identity(args.source.resolve(), args.version)
            store.install(args.source.resolve(), kernel_id)
            print(f"✓ installed and selected workspace kernel {kernel_id}")
        elif args.command == "select":
            store.select(args.id)
            print(f"✓ selected kernel {args.id}")
        elif args.command == "delete":
            store.delete(args.id)
            print(f"✓ removed kernel {args.id}")
        elif args.command == "list":
            records = []
            for kernel_id in store.installed():
                flags = []
                if kernel_id == store.current_id():
                    flags.append("current")
                if kernel_id == store.fallback_id():
                    flags.append("fallback")
                valid, state = store.verify(kernel_id)
                flags.append(state if valid else f"invalid:{state}")
                flags.append(f"arch={store.arch}")
                records.append({
                    "id": kernel_id, "arch": store.arch,
                    "profile": store.profile, "machine": store.machine,
                    "current": kernel_id == store.current_id(),
                    "fallback": kernel_id == store.fallback_id(),
                    "valid": valid, "health": state,
                })
                if not args.json:
                    print(f"{kernel_id}\t{','.join(flags)}")
            if args.json:
                print(json.dumps(records, sort_keys=True, indent=2))
        elif args.command == "resolve":
            print(store.resolve())
        elif args.command == "verify":
            targets = [args.id] if args.id else store.installed()
            failed = False
            for kernel_id in targets:
                valid, state = store.verify(kernel_id, enroll=True)
                print(f"{'✓' if valid else '✗'} {kernel_id}: {state}")
                failed |= not valid
            return 2 if failed else 0
        elif args.command == "workspace":
            if args.source is None or args.version is None:
                raise ValueError("workspace source/version is not configured")
            kernel_id, embedded_arch = store.source_identity(
                args.source.resolve(), args.version
            )
            state = "installed" if kernel_id in store.installed() else "not-installed"
            print(f"{kernel_id}\t{state},arch={embedded_arch}")
        else:
            if not sys.stdin.isatty() or not sys.stdout.isatty():
                print("✗ kmang requires an interactive terminal", file=sys.stderr)
                return 2
            result = curses.wrapper(
                tui, store, args.make_arg, args.source, args.version
            )
            if result == 10:
                return 10
        return 0
    except (OSError, ValueError) as error:
        print(f"✗ {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
