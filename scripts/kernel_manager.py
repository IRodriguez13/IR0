#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Manage versioned boot ISOs for one persistent IR0 machine.

Build numbers (#N / -buildN) are machine-local counters: the same tree built on
two hosts can legitimately produce different N. Catalog identity therefore pairs
the local counter with SHA-256 and optional provenance (host, user, git, date).
poweron always boots Default (kernel-current.iso), never the bare workspace ISO
once any verified kernel is enrolled.
"""

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
# Fragments emitted into kernel .rodata via IR0_BUILD_* macros.
PROVENANCE_RE = re.compile(
    rb"built ([A-Za-z]{3} +\d{1,2} +\d{4}) (\d{2}:\d{2}:\d{2}) by "
    rb"([A-Za-z0-9._-]+)@([A-Za-z0-9._-]+) with "
)


class KernelStore:
    def __init__(
        self,
        machine_dir: Path,
        arch: str = "unknown",
        profile: str = "unknown",
        machine: str = "unknown",
        kernel_root: Path | None = None,
        isd_disk: Path | None = None,
        machine_disk: Path | None = None,
    ) -> None:
        self.machine_dir = machine_dir.resolve()
        self.arch = arch
        self.profile = profile
        self.machine = machine
        self.kernel_root = kernel_root.resolve() if kernel_root else None
        self.isd_disk = isd_disk.resolve() if isd_disk else None
        self.machine_disk = machine_disk.resolve() if machine_disk else None
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
    def embedded_provenance(image: Path) -> dict[str, str]:
        """Best-effort host/date strings from kernel .rodata (may be absent)."""
        with tempfile.TemporaryDirectory(prefix="ir0-kernel-prov.") as directory:
            kernel = Path(directory) / "kernel.bin"
            if not KernelStore._extract_kernel(image, kernel):
                return {}
            data = kernel.read_bytes()
        match = PROVENANCE_RE.search(data)
        if not match:
            return {}
        return {
            "build_date": match.group(1).decode("ascii", "replace"),
            "build_time": match.group(2).decode("ascii", "replace"),
            "builder_user": match.group(3).decode("ascii", "replace"),
            "builder_host": match.group(4).decode("ascii", "replace"),
        }

    @staticmethod
    def identifier_build(kernel_id: str) -> int | None:
        match = BUILD_ID_RE.search(kernel_id)
        return int(match.group(1)) if match else None

    def next_local_build(self) -> int | None:
        """Counter that the next link on this tree will stamp (machine-local)."""
        if self.kernel_root is None:
            return None
        path = self.kernel_root / ".build_number"
        if not path.is_file():
            return None
        try:
            return int(path.read_text().strip())
        except (OSError, ValueError):
            return None

    def workspace_git(self) -> dict[str, object]:
        """Capture tree identity at install time; never invent a commit."""
        if self.kernel_root is None or not (self.kernel_root / ".git").exists():
            return {}
        commit = subprocess.run(
            ["git", "-C", str(self.kernel_root), "rev-parse", "--short=12", "HEAD"],
            text=True, capture_output=True, check=False,
        )
        dirty = subprocess.run(
            ["git", "-C", str(self.kernel_root), "status", "--porcelain"],
            text=True, capture_output=True, check=False,
        )
        payload: dict[str, object] = {}
        if commit.returncode == 0 and commit.stdout.strip():
            payload["git_commit"] = commit.stdout.strip()
        if dirty.returncode == 0:
            payload["git_dirty"] = bool(dirty.stdout.strip())
        return payload

    def disk_status(self) -> str:
        bits: list[str] = []
        for label, path in (("machine", self.machine_disk), ("isd-base", self.isd_disk)):
            if path is None:
                continue
            if path.is_file():
                size_m = path.stat().st_size // (1024 * 1024)
                bits.append(f"{label}={size_m}M")
            else:
                bits.append(f"{label}=missing")
        return ", ".join(bits) if bits else "disks not configured"

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
            if metadata.get("format") not in (1, 2, 3) or metadata.get("id") != kernel_id:
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
            if enroll and (
                metadata.get("arch") in (None, "unknown")
                or metadata.get("format", 0) < 3
            ):
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
        provenance = self.embedded_provenance(image)
        host_git = self.workspace_git()
        payload = {
            "format": 3,
            "id": kernel_id,
            "sha256": digest,
            "size": image.stat().st_size,
            "installed_at": int(time.time()),
            "embedded_build": embedded_build,
            "arch": self.arch,
            "embedded_arch": self.embedded_arch(image),
            "build_scope": "machine-local",
            **provenance,
            **host_git,
        }
        if self.kernel_root is not None:
            payload["kernel_root"] = str(self.kernel_root)
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

    def read_metadata(self, kernel_id: str) -> dict:
        path = self.metadata_path(kernel_id)
        if not path.is_file():
            return {}
        try:
            data = json.loads(path.read_text())
            return data if isinstance(data, dict) else {}
        except (OSError, json.JSONDecodeError):
            return {}

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

    def prune(self) -> list[str]:
        """Remove every installed kernel that is neither Default nor Fallback."""
        keep = {self.current_id(), self.fallback_id()} - {None}
        removed: list[str] = []
        for kernel_id in list(self.installed()):
            if kernel_id in keep:
                continue
            self.delete(kernel_id)
            removed.append(kernel_id)
        return removed

    def resolve(self) -> Path:
        for link in (self.current, self.fallback):
            kernel_id = self.link_id(link)
            if kernel_id:
                valid, _ = self.verify(kernel_id, enroll=True)
                if valid:
                    return self.kernels / f"{kernel_id}.iso"
        raise ValueError("no verified current or fallback kernel")

    def describe(self, kernel_id: str) -> dict:
        """Structured detail for CLI/TUI; safe for missing metadata."""
        image = self.kernels / f"{kernel_id}.iso"
        valid, state = self.verify(kernel_id)
        meta = self.read_metadata(kernel_id)
        digest = meta.get("sha256")
        if not digest and image.is_file():
            digest = self.checksum(image)
        flags = []
        if kernel_id == self.current_id():
            # CLI keeps "current" for compatibility; TUI labels it Default.
            flags.append("current")
        if kernel_id == self.fallback_id():
            flags.append("fallback")
        return {
            "id": kernel_id,
            "arch": self.arch,
            "profile": self.profile,
            "machine": self.machine,
            "current": kernel_id == self.current_id(),
            "fallback": kernel_id == self.fallback_id(),
            "valid": valid,
            "health": state,
            "flags": flags,
            "sha256": digest,
            "sha256_short": (digest or "")[:12] or None,
            "size": meta.get("size") or (image.stat().st_size if image.is_file() else None),
            "embedded_build": meta.get("embedded_build"),
            "builder_host": meta.get("builder_host"),
            "builder_user": meta.get("builder_user"),
            "build_date": meta.get("build_date"),
            "build_time": meta.get("build_time"),
            "git_commit": meta.get("git_commit"),
            "git_dirty": meta.get("git_dirty"),
            "build_scope": meta.get("build_scope", "machine-local"),
            "installed_at": meta.get("installed_at"),
            "path": str(image) if image.is_file() else None,
        }

    def compare_workspace(self, source: Path, version: str) -> dict:
        """Explain whether poweron Default matches the workspace ISO."""
        workspace_id, workspace_arch = self.source_identity(source, version)
        workspace_digest = self.checksum(source.resolve())
        default_id = self.current_id()
        default_digest = None
        if default_id:
            meta = self.read_metadata(default_id)
            default_digest = meta.get("sha256")
            image = self.kernels / f"{default_id}.iso"
            if not default_digest and image.is_file():
                default_digest = self.checksum(image)
        same_id = default_id == workspace_id
        same_bytes = (
            default_digest is not None and default_digest == workspace_digest
        )
        if same_bytes:
            relation = "identical"
        elif same_id:
            relation = "same-id-different-bytes"
        elif default_id is None:
            relation = "no-default"
        else:
            relation = "diverged"
        return {
            "relation": relation,
            "workspace_id": workspace_id,
            "workspace_arch": workspace_arch,
            "workspace_sha256": workspace_digest,
            "default_id": default_id,
            "default_sha256": default_digest,
            "poweron_uses": default_id or "workspace-iso-fallback",
            "needs_install": relation != "identical",
            "note": (
                "Build numbers are machine-local; compare SHA-256 across hosts, "
                "not #N alone."
            ),
        }


def _clip(text: str, width: int) -> str:
    if width <= 0:
        return ""
    return text if len(text) <= width else text[: max(0, width - 1)]


def _provenance_label(meta: dict) -> str:
    host = meta.get("builder_host") or "?"
    user = meta.get("builder_user")
    date = meta.get("build_date")
    commit = meta.get("git_commit")
    bits = []
    if user:
        bits.append(f"{user}@{host}")
    else:
        bits.append(f"@{host}" if host != "?" else "provenance?")
    if date:
        bits.append(str(date))
    if commit:
        dirty = "+" if meta.get("git_dirty") else ""
        bits.append(f"git:{commit}{dirty}")
    sha = meta.get("sha256_short") or (meta.get("sha256") or "")[:12]
    if sha:
        bits.append(f"sha:{sha}")
    return " · ".join(bits)


HELP_LINES = [
    "Build #N is local to this host's .build_number — not a global release id.",
    "poweron boots Default (catalog), not Workspace, until you press i.",
    "",
    "i  install workspace ISO into catalog + select it",
    "r  rebuild kernel ISO, install, select",
    "Enter  select highlighted as Default (old becomes Fallback)",
    "b  boot Default via make poweron",
    "v  verify checksum + embedded identity",
    "c  compare Workspace vs Default",
    "s  show detail for highlighted kernel",
    "p  prune (delete all except Default+Fallback); press twice",
    "d  delete highlighted (not Default/Fallback); press twice",
    "h/?  this help   q  quit",
]


def tui(screen: curses.window, store: KernelStore, make_args: list[str],
        source: Path | None, version: str | None) -> int:
    try:
        curses.curs_set(0)
    except curses.error:
        pass
    selected = 0
    scroll = 0
    pending_delete = None
    pending_prune = False
    statuses: dict[str, tuple[bool, str]] = {}
    refresh_status = True
    show_help = False
    detail: str | None = None
    message = (
        "i install  r rebuild  enter select  c compare  s detail  "
        "v verify  p prune  b boot  h help  q quit"
    )
    while True:
        entries = store.installed()
        workspace_id = None
        workspace_health = "not configured"
        workspace_digest = None
        if source is not None and version is not None:
            try:
                workspace_id, workspace_arch = store.source_identity(source, version)
                workspace_digest = store.checksum(source.resolve())
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
        height, width = screen.getmaxyx()
        list_top = 9
        list_bottom = max(list_top, height - 3)
        visible = max(1, list_bottom - list_top)
        if selected < scroll:
            scroll = selected
        if selected >= scroll + visible:
            scroll = selected - visible + 1

        default_id = store.current_id()
        drift = False
        if workspace_id and default_id:
            default_sha = store.read_metadata(default_id).get("sha256")
            if workspace_digest and default_sha and workspace_digest != default_sha:
                drift = True
            elif workspace_id != default_id:
                drift = True
        elif workspace_id and default_id is None:
            drift = True

        screen.erase()
        if show_help:
            screen.addstr(0, 2, "kmang help", curses.A_BOLD)
            for index, line in enumerate(HELP_LINES):
                if 2 + index >= height - 1:
                    break
                screen.addstr(2 + index, 2, _clip(line, width - 4))
            screen.addstr(height - 1, 2, _clip("press any key", width - 4))
            screen.refresh()
            screen.getch()
            show_help = False
            continue

        if detail is not None:
            screen.addstr(0, 2, "kernel detail", curses.A_BOLD)
            for index, line in enumerate(detail.splitlines()):
                if 2 + index >= height - 1:
                    break
                screen.addstr(2 + index, 2, _clip(line, width - 4))
            screen.addstr(height - 1, 2, _clip("press any key", width - 4))
            screen.refresh()
            screen.getch()
            detail = None
            continue

        screen.addstr(0, 2, "IR0 Kernel Manager", curses.A_BOLD)
        screen.addstr(
            1, 2,
            _clip(
                f"Store: {store.arch}/{store.profile}/{store.machine}  "
                f"({store.machine_dir})",
                width - 4,
            ),
        )
        next_build = store.next_local_build()
        next_label = f"next local #{next_build}" if next_build is not None else "next local #?"
        screen.addstr(
            2, 2,
            _clip(
                f"Build scope: machine-local counters  |  {next_label}  |  "
                f"{store.disk_status()}",
                width - 4,
            ),
        )
        screen.addstr(4, 2, _clip(f"Default:   {default_id or '(none — poweron uses workspace ISO)'}", width - 4))
        screen.addstr(5, 2, _clip(f"Fallback:  {store.fallback_id() or '-'}", width - 4))
        screen.addstr(6, 2, _clip(f"Workspace: {workspace_id or '-'} [{workspace_health}]", width - 4))
        if drift:
            screen.addstr(
                7, 2,
                _clip(
                    "! poweron boots Default, not Workspace — press i to enroll this ISO",
                    width - 4,
                ),
                curses.A_BOLD,
            )
        else:
            screen.addstr(7, 2, _clip("Default matches Workspace (or Workspace unavailable)", width - 4))
        screen.addstr(8, 2, "Installed:", curses.A_BOLD)

        if not entries:
            screen.addstr(list_top, 2, _clip("(empty — press i or r)", width - 4))
        for row, kernel_id in enumerate(entries[scroll: scroll + visible]):
            index = scroll + row
            marker = "*" if kernel_id == default_id else " "
            tags = []
            if kernel_id == store.fallback_id():
                tags.append("fallback")
            if workspace_id and kernel_id == workspace_id:
                tags.append("workspace")
            valid, state = statuses.get(kernel_id, (False, "unknown"))
            health = "ok" if valid and state == "verified" else state
            meta = store.describe(kernel_id)
            prov = _provenance_label(meta)
            suffix = f" [{', '.join(tags)}]" if tags else ""
            line = f"{marker} [{store.arch}] {kernel_id} [{health}]{suffix}  {prov}"
            attr = curses.A_REVERSE if index == selected else 0
            screen.addstr(list_top + row, 2, _clip(line, width - 4), attr)

        screen.addstr(height - 2, 2, _clip(message, width - 4))
        screen.refresh()
        key = screen.getch()
        try:
            if key in (ord("q"), 27):
                return 0
            if key in (ord("h"), ord("?")):
                show_help = True
                pending_delete = None
                pending_prune = False
            elif key == curses.KEY_UP and entries:
                selected = (selected - 1) % len(entries)
            elif key == curses.KEY_DOWN and entries:
                selected = (selected + 1) % len(entries)
            elif key == curses.KEY_PPAGE and entries:
                selected = max(0, selected - visible)
            elif key == curses.KEY_NPAGE and entries:
                selected = min(len(entries) - 1, selected + visible)
            elif key in (10, 13) and entries:
                store.select(entries[selected])
                message = f"Selected {entries[selected]}; previous retained as fallback"
                pending_delete = None
                pending_prune = False
            elif key == ord("i"):
                if source is None or version is None:
                    raise ValueError("workspace install source/version is not configured")
                workspace_id, _ = store.source_identity(source, version)
                store.install(source.resolve(), workspace_id)
                message = f"Installed and selected {workspace_id}"
                refresh_status = True
                pending_delete = None
                pending_prune = False
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
                pending_delete = None
                pending_prune = False
            elif key == ord("c"):
                if source is None or version is None:
                    raise ValueError("workspace source/version is not configured")
                report = store.compare_workspace(source.resolve(), version)
                message = (
                    f"compare: {report['relation']}  "
                    f"workspace={report['workspace_id']}  "
                    f"default={report['default_id'] or '-'}  "
                    f"needs_install={report['needs_install']}"
                )
                pending_delete = None
                pending_prune = False
            elif key == ord("s") and entries:
                info = store.describe(entries[selected])
                lines = [
                    f"id:            {info['id']}",
                    f"health:        {info['health']}  flags={','.join(info['flags']) or '-'}",
                    f"build scope:   {info['build_scope']} (local to builder host)",
                    f"embedded #:    {info.get('embedded_build')}",
                    f"sha256:        {info.get('sha256')}",
                    f"size:          {info.get('size')}",
                    f"builder:       {info.get('builder_user') or '?'}@{info.get('builder_host') or '?'}",
                    f"built:         {info.get('build_date') or '?'} {info.get('build_time') or ''}",
                    f"git:           {info.get('git_commit') or '?'} dirty={info.get('git_dirty')}",
                    f"installed_at:  {info.get('installed_at')}",
                    f"path:          {info.get('path')}",
                ]
                detail = "\n".join(lines)
                pending_delete = None
                pending_prune = False
            elif key == ord("d") and entries:
                pending_prune = False
                if pending_delete != entries[selected]:
                    pending_delete = entries[selected]
                    message = f"Press d again to remove {entries[selected]}"
                else:
                    store.delete(entries[selected])
                    pending_delete = None
                    message = "Kernel removed"
                    refresh_status = True
            elif key == ord("p"):
                pending_delete = None
                if not pending_prune:
                    pending_prune = True
                    message = "Press p again to prune all except Default+Fallback"
                else:
                    removed = store.prune()
                    pending_prune = False
                    message = f"Pruned {len(removed)} kernel(s)"
                    refresh_status = True
            elif key == ord("v") and entries:
                valid, state = store.verify(entries[selected], enroll=True)
                message = f"{entries[selected]}: {state if valid else 'FAILED ' + state}"
                refresh_status = True
                pending_delete = None
                pending_prune = False
            elif key == ord("b"):
                store.resolve()
                return 10
        except (OSError, ValueError) as error:
            message = f"Error: {error}"
            pending_delete = None
            pending_prune = False


def main() -> int:
    parser = argparse.ArgumentParser(
        description=(
            "Persistent IR0 kernel catalog. Build numbers are machine-local; "
            "use SHA-256 / provenance to compare across hosts."
        )
    )
    parser.add_argument("--machine-dir", required=True, type=Path)
    parser.add_argument("--arch", default="unknown")
    parser.add_argument("--profile", default="unknown")
    parser.add_argument("--machine", default="unknown")
    parser.add_argument("--kernel-root", type=Path)
    parser.add_argument("--isd-disk", type=Path)
    parser.add_argument("--machine-disk", type=Path)
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
    info = subparsers.add_parser("info")
    info.add_argument("id", nargs="?")
    info.add_argument("--json", action="store_true")
    subparsers.add_parser("compare")
    subparsers.add_parser("prune")
    subparsers.add_parser("tui")
    args = parser.parse_args()
    store = KernelStore(
        args.machine_dir,
        args.arch,
        args.profile,
        args.machine,
        kernel_root=args.kernel_root,
        isd_disk=args.isd_disk,
        machine_disk=args.machine_disk,
    )
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
        elif args.command == "prune":
            removed = store.prune()
            if removed:
                print("✓ pruned: " + ", ".join(removed))
            else:
                print("✓ nothing to prune")
        elif args.command == "list":
            records = []
            for kernel_id in store.installed():
                info_rec = store.describe(kernel_id)
                flags = list(info_rec["flags"])
                flags.append(info_rec["health"] if info_rec["valid"] else f"invalid:{info_rec['health']}")
                flags.append(f"arch={store.arch}")
                if info_rec.get("builder_host"):
                    flags.append(f"host={info_rec['builder_host']}")
                if info_rec.get("sha256_short"):
                    flags.append(f"sha={info_rec['sha256_short']}")
                records.append(info_rec)
                if not args.json:
                    print(f"{kernel_id}\t{','.join(flags)}")
            if args.json:
                print(json.dumps(records, sort_keys=True, indent=2))
        elif args.command == "info":
            targets = [args.id] if args.id else store.installed()
            if not targets:
                raise ValueError("no kernels installed")
            records = [store.describe(kernel_id) for kernel_id in targets]
            if args.json:
                print(json.dumps(records if args.id is None else records[0],
                                 sort_keys=True, indent=2))
            else:
                for info_rec in records:
                    print(f"{info_rec['id']}")
                    for key in (
                        "health", "flags", "build_scope", "embedded_build",
                        "sha256", "builder_user", "builder_host", "build_date",
                        "git_commit", "git_dirty", "path",
                    ):
                        print(f"  {key}: {info_rec.get(key)}")
        elif args.command == "compare":
            if args.source is None or args.version is None:
                raise ValueError("workspace source/version is not configured")
            report = store.compare_workspace(args.source.resolve(), args.version)
            print(json.dumps(report, sort_keys=True, indent=2))
            return 1 if report["needs_install"] else 0
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
