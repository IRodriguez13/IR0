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
from dataclasses import dataclass
from pathlib import Path


ID_RE = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._+-]*$")
BUILD_ID_RE = re.compile(r"-build([0-9]+)$")
RELEASE_ID_RE = re.compile(r"^(.+)-build[0-9]+$")
RELEASE_MARKER = b"IR0VER:"
IR0_LOGIN_SESSION_TERMINAL = "terminal"
IR0_LOGIN_SESSION_X = "x"
IR0_SESSION_FILE = "etc/ir0-session"
# Backward-compatible aliases for host tests and mandoc cross-refs.
KMANG_SESSION_TERMINAL = IR0_LOGIN_SESSION_TERMINAL
KMANG_SESSION_X = IR0_LOGIN_SESSION_X
KMANG_SESSION_FILE = IR0_SESSION_FILE
TUI_EXIT_BOOT = 10
DESKTOP_BOOT_PROFILES = frozenset({"desktop", "desktop-console"})
TRUTHY_PROFILE_FLAGS = frozenset({"1", "yes", "true", "on"})
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
        isd_root: Path | None = None,
        isd_disk: Path | None = None,
        machine_disk: Path | None = None,
    ) -> None:
        self.machine_dir = machine_dir.resolve()
        self.arch = arch
        self.profile = profile
        self.machine = machine
        self.kernel_root = kernel_root.resolve() if kernel_root else None
        self.isd_root = isd_root.resolve() if isd_root else None
        self.isd_disk = isd_disk.resolve() if isd_disk else None
        self.machine_disk = machine_disk.resolve() if machine_disk else None
        self.kernels = self.machine_dir / "kernels"
        self.current = self.machine_dir / "kernel-current.iso"
        self.fallback = self.machine_dir / "kernel-fallback.iso"
        self.lock_path = self.machine_dir / ".kernel-manager.lock"

    @property
    def persistent_disk(self) -> Path:
        if self.machine_disk is not None:
            return self.machine_disk
        return self.machine_dir / "disk.img"

    def profile_conf_value(self, key: str) -> str | None:
        if self.isd_root is None:
            return None
        conf = self.isd_root / "profiles" / self.profile / "profile.conf"
        if not conf.is_file():
            return None
        prefix = key + "="
        for raw in conf.read_text(encoding="utf-8", errors="replace").splitlines():
            line = raw.strip()
            if not line or line.startswith("#"):
                continue
            if line.startswith(prefix):
                return line.split("=", 1)[1].strip().strip('"')
        return None

    def boot_session_prompt_capable(self) -> bool:
        """True when kmang TUI may ask terminal vs X before poweron."""
        flag = self.profile_conf_value("KMANG_BOOT_PROMPT")
        if flag is not None:
            return flag.lower() in TRUTHY_PROFILE_FLAGS
        return self.profile in DESKTOP_BOOT_PROFILES

    def desktop_boot_capable(self) -> bool:
        return self.boot_session_prompt_capable()

    def write_login_session(self, session: str) -> None:
        if session not in (IR0_LOGIN_SESSION_TERMINAL, IR0_LOGIN_SESSION_X):
            raise ValueError(f"unsupported login session: {session}")
        disk = self.persistent_disk
        if not disk.is_file():
            raise ValueError(
                f"machine disk missing: {disk} (provision with make first-boot/poweron)"
            )
        if self.kernel_root is None:
            raise ValueError("kernel root is not configured for login-session inject")
        inject = self.kernel_root / "scripts" / "inject_init_minix.py"
        if not inject.is_file():
            raise ValueError(f"missing inject tool: {inject}")
        with tempfile.NamedTemporaryFile("w", encoding="utf-8", delete=False) as stream:
            stream.write(session + "\n")
            payload = Path(stream.name)
        try:
            result = subprocess.run(
                [sys.executable, str(inject), str(disk), str(payload), IR0_SESSION_FILE],
                text=True,
                capture_output=True,
                check=False,
            )
            if result.returncode != 0:
                detail = result.stderr.strip() or result.stdout.strip() or "inject failed"
                raise ValueError(detail)
        finally:
            payload.unlink(missing_ok=True)

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
    def embedded_release(image: Path) -> str | None:
        """Release string compiled into the kernel (IR0_VERSION_STRING at link time)."""
        with tempfile.TemporaryDirectory(prefix="ir0-kernel-release.") as directory:
            kernel = Path(directory) / "kernel.bin"
            if not KernelStore._extract_kernel(image, kernel):
                return None
            data = kernel.read_bytes()
        marker = data.find(RELEASE_MARKER)
        if marker < 0:
            return None
        start = marker + len(RELEASE_MARKER)
        end = data.find(b"\0", start)
        if end < 0 or end <= start:
            return None
        release = data[start:end].decode("ascii", "replace")
        return release if release else None

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

    @staticmethod
    def identifier_release(kernel_id: str) -> str | None:
        match = RELEASE_ID_RE.match(kernel_id)
        return match.group(1) if match else None

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

    def _iso_has_bootable_kernel(self, image: Path) -> bool:
        """True when xorriso can extract a supported /boot/kernel*.bin payload."""
        with tempfile.TemporaryDirectory(prefix="ir0-kernel-probe.") as directory:
            kernel = Path(directory) / "kernel.bin"
            return self._extract_kernel(image, kernel)

    def verify(self, kernel_id: str, enroll: bool = False) -> tuple[bool, str]:
        image = self.kernels / f"{kernel_id}.iso"
        if not image.is_file():
            return False, "missing"
        if not self._iso_has_bootable_kernel(image):
            return False, "invalid ISO"
        digest = self.checksum(image)
        embedded = self.embedded_build(image)
        embedded_release = self.embedded_release(image)
        embedded_arch = self.embedded_arch(image)
        expected = self.identifier_build(kernel_id)
        expected_release = self.identifier_release(kernel_id)
        if embedded_arch is None and self.arch != "unknown":
            return False, "architecture inspection failed"
        if self.arch != "unknown" and embedded_arch != self.arch:
            return False, f"architecture mismatch: image is {embedded_arch or 'unknown'}"
        if embedded is not None and expected is not None and embedded != expected:
            return False, f"identity mismatch: image build{embedded}"
        if (
            embedded_release is not None
            and expected_release is not None
            and embedded_release != expected_release
        ):
            return False, f"release mismatch: image has {embedded_release}"
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
            recorded_release = metadata.get("embedded_release")
            if (
                recorded_release is not None
                and embedded_release is not None
                and recorded_release != embedded_release
            ):
                return False, "embedded release changed"
            if enroll and (
                metadata.get("arch") in (None, "unknown")
                or metadata.get("format", 0) < 3
            ):
                self._write_metadata(kernel_id, image, digest, embedded, embedded_release)
            if (
                embedded is not None
                and expected == embedded
                and (
                    expected_release is None
                    or embedded_release is None
                    or expected_release == embedded_release
                )
            ):
                return True, "verified"
            return True, "checksum-only"
        if enroll:
            self._write_metadata(kernel_id, image, digest, embedded, embedded_release)
            if (
                embedded is not None
                and expected == embedded
                and (
                    expected_release is None
                    or embedded_release is None
                    or expected_release == embedded_release
                )
            ):
                return True, "verified"
            return True, "checksum-only"
        return True, "legacy-unverified"

    def _write_metadata(self, kernel_id: str, image: Path, digest: str,
                        embedded_build: int | None,
                        embedded_release: str | None = None) -> None:
        target = self.metadata_path(kernel_id)
        provenance = self.embedded_provenance(image)
        host_git = self.workspace_git()
        if embedded_release is None:
            embedded_release = self.embedded_release(image)
        payload = {
            "format": 3,
            "id": kernel_id,
            "sha256": digest,
            "size": image.stat().st_size,
            "installed_at": int(time.time()),
            "embedded_build": embedded_build,
            "embedded_release": embedded_release,
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
        embedded_release = self.embedded_release(source)
        embedded_arch = self.embedded_arch(source)
        expected = self.identifier_build(kernel_id)
        expected_release = self.identifier_release(kernel_id)
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
        if expected_release is not None:
            if embedded_release is None:
                raise ValueError("kernel ISO has no embedded release identity")
            if embedded_release != expected_release:
                raise ValueError(
                    f"kernel id says {expected_release}, image contains {embedded_release}"
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
        self._write_metadata(kernel_id, target, source_digest, embedded, embedded_release)
        self.select(kernel_id)

    def source_identity(self, source: Path, version: str) -> tuple[str, str]:
        """Return a verified catalog id and ISA for a workspace ISO."""
        if not source.is_file():
            raise ValueError(f"missing workspace kernel ISO: {source}")
        embedded = self.embedded_build(source)
        embedded_release = self.embedded_release(source)
        embedded_arch = self.embedded_arch(source)
        if embedded is None:
            raise ValueError("workspace kernel has no embedded build identity")
        if embedded_release is None:
            raise ValueError("workspace kernel has no embedded release identity")
        if embedded_arch is None:
            raise ValueError("workspace kernel architecture could not be inspected")
        if self.arch != "unknown" and embedded_arch != self.arch:
            raise ValueError(
                f"workspace catalog is {self.arch}, image is {embedded_arch}"
            )
        if version != embedded_release:
            raise ValueError(
                f"workspace Makefile says {version}, kernel image contains {embedded_release}"
            )
        return f"{embedded_release}-build{embedded}", embedded_arch

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


@dataclass(frozen=True)
class HelpEntry:
    keys: str
    description: str


@dataclass(frozen=True)
class HelpSection:
    title: str
    entries: tuple[HelpEntry, ...]


KMANG_HELP_SECTIONS: tuple[HelpSection, ...] = (
    HelpSection(
        "Concepts",
        (
            HelpEntry(
                "",
                "Build #N is local to this host's .build_number — not a global release id.",
            ),
            HelpEntry(
                "",
                "poweron boots Default (catalog), not Workspace, until you press i.",
            ),
            HelpEntry(
                "",
                "* marks Default; [fallback] is the previous Default after re-select.",
            ),
        ),
    ),
    HelpSection(
        "Workspace and catalog",
        (
            HelpEntry("i", "Install workspace ISO into catalog and select as Default"),
            HelpEntry("r", "Rebuild kernel-x64-userspace.iso, install, and select"),
            HelpEntry("Enter", "Select highlighted kernel; desktop profiles ask terminal vs X"),
            HelpEntry("b", "Verify Default/Fallback, ask terminal vs X, then poweron"),
            HelpEntry("c", "Compare workspace ISO vs Default (relation + SHA-256)"),
        ),
    ),
    HelpSection(
        "Inspection",
        (
            HelpEntry("s", "Show metadata detail for highlighted kernel"),
            HelpEntry("v", "Verify checksum and embedded build identity"),
        ),
    ),
    HelpSection(
        "Navigation",
        (
            HelpEntry("↑ / ↓", "Move selection in the kernel list"),
            HelpEntry("PgUp / PgDn", "Page the kernel list"),
        ),
    ),
    HelpSection(
        "Cleanup (confirm twice)",
        (
            HelpEntry("d", "Delete highlighted kernel (not Default or Fallback)"),
            HelpEntry("p", "Prune all kernels except Default and Fallback"),
        ),
    ),
    HelpSection(
        "General",
        (
            HelpEntry("h / ? / F1", "Show this key legend"),
            HelpEntry("q / Esc", "Quit without booting"),
        ),
    ),
)

# Single-letter keys handled by the TUI (excluding Enter and navigation).
KMANG_TUI_ACTION_KEYS = frozenset("ircsvdpbh?")


def format_help_plain_lines(section: HelpSection) -> list[str]:
    lines: list[str] = []
    lines.append(section.title + ":")
    for entry in section.entries:
        if entry.keys:
            lines.append(f"  {entry.keys:<14} {entry.description}")
        else:
            lines.append(f"  {entry.description}")
    lines.append("")
    return lines


def format_help_text() -> str:
    chunks: list[str] = ["IR0 Kernel Manager (kmang) — key legend", ""]
    for section in KMANG_HELP_SECTIONS:
        chunks.extend(format_help_plain_lines(section))
    while chunks and chunks[-1] == "":
        chunks.pop()
    return "\n".join(chunks)


def format_help_compact(max_width: int = 76) -> str:
    legend = "Keys: h/? · q · ↑↓ · i · r · Enter · b · c · s/v · d/p  (h/? = full legend)"
    return _clip(legend, max_width)


def help_overlay_lines() -> list[tuple[bool, str]]:
    rows: list[tuple[bool, str]] = [(True, "kmang — key legend")]
    rows.append((False, ""))
    for section in KMANG_HELP_SECTIONS:
        rows.append((True, section.title))
        for entry in section.entries:
            if entry.keys:
                rows.append((False, f"  {entry.keys:<14} {entry.description}"))
            else:
                rows.append((False, f"  {entry.description}"))
        rows.append((False, ""))
    while rows and rows[-1] == (False, ""):
        rows.pop()
    rows.append((False, ""))
    rows.append((False, "↑/↓ scroll legend · any other key closes"))
    return rows


def _render_help_overlay(
    screen: curses.window,
    lines: list[tuple[bool, str]],
    scroll: int,
) -> int:
    height, width = screen.getmaxyx()
    footer = 2
    body_rows = max(1, height - footer)
    max_scroll = max(0, len(lines) - body_rows)
    scroll = min(scroll, max_scroll)
    screen.erase()
    for row in range(body_rows):
        index = scroll + row
        if index >= len(lines):
            break
        is_title, text = lines[index]
        attr = curses.A_BOLD if is_title else 0
        screen.addstr(row, 2, _clip(text, width - 4), attr)
    if max_scroll > 0:
        hint = f"scroll {scroll + 1}/{max_scroll + 1}  ↑↓  close: any key"
    else:
        hint = "close: any key"
    screen.addstr(height - 1, 2, _clip(hint, width - 4))
    screen.refresh()
    return scroll


def _read_help_scroll_key(screen: curses.window, scroll: int, max_scroll: int) -> tuple[int, bool]:
    key = screen.getch()
    if key in (curses.KEY_UP, ord("k")):
        return max(0, scroll - 1), False
    if key in (curses.KEY_DOWN, ord("j")):
        return min(max_scroll, scroll + 1), False
    if key in (curses.KEY_PPAGE,):
        return max(0, scroll - 5), False
    if key in (curses.KEY_NPAGE,):
        return min(max_scroll, scroll + 5), False
    return scroll, True


HELP_LINES = format_help_text().splitlines()


def _prompt_boot_session(screen: curses.window, kernel_id: str) -> str | None:
    """Return terminal/x session choice, or None to stay in kmang."""
    height, width = screen.getmaxyx()
    rows: list[tuple[bool, str]] = [
        (True, "Boot session"),
        (False, ""),
        (False, f"Kernel: {kernel_id}"),
        (False, ""),
        (False, "  t   Terminal only"),
        (False, "  x   X direct"),
        (False, "  q   Select only (stay in kmang)"),
    ]
    screen.erase()
    for index, (is_title, text) in enumerate(rows):
        if index >= height - 2:
            break
        attr = curses.A_BOLD if is_title else 0
        screen.addstr(index, 2, _clip(text, width - 4), attr)
    screen.addstr(height - 1, 2, _clip("choose: t / x / q", width - 4))
    screen.refresh()
    key = screen.getch()
    if key in (ord("x"), ord("X")):
        return IR0_LOGIN_SESSION_X
    if key in (ord("t"), ord("T")):
        return IR0_LOGIN_SESSION_TERMINAL
    return None


@dataclass(frozen=True)
class TuiConfirmState:
    pending_delete: str | None = None
    pending_prune: bool = False


def tui_confirm_reset() -> TuiConfirmState:
    return TuiConfirmState()


def tui_arm_delete(state: TuiConfirmState, kernel_id: str) -> tuple[TuiConfirmState, str, bool]:
    """Arm or confirm delete. Returns (state, status message, confirmed)."""
    if state.pending_delete != kernel_id:
        return (
            TuiConfirmState(pending_delete=kernel_id, pending_prune=False),
            f"Press d again to remove {kernel_id}",
            False,
        )
    return TuiConfirmState(), "Kernel removed", True


def tui_arm_prune(state: TuiConfirmState) -> tuple[TuiConfirmState, str, bool]:
    """Arm or confirm prune. Returns (state, status message, confirmed)."""
    if not state.pending_prune:
        return (
            TuiConfirmState(pending_prune=True),
            "Press p again to prune all except Default+Fallback",
            False,
        )
    return TuiConfirmState(), "", True


def _maybe_boot_after_select(store: KernelStore, screen: curses.window,
                            kernel_id: str) -> int | None:
    if not store.boot_session_prompt_capable():
        return None
    choice = _prompt_boot_session(screen, kernel_id)
    if choice is None:
        return None
    store.write_login_session(choice)
    return TUI_EXIT_BOOT


def tui(screen: curses.window, store: KernelStore, make_args: list[str],
        source: Path | None, version: str | None) -> int:
    try:
        curses.curs_set(0)
    except curses.error:
        pass
    selected = 0
    scroll = 0
    confirm = tui_confirm_reset()
    statuses: dict[str, tuple[bool, str]] = {}
    refresh_status = True
    show_help = False
    help_scroll = 0
    detail: str | None = None
    message = "Press h/? for full key legend"
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
        list_bottom = max(list_top, height - 4)
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
            overlay = help_overlay_lines()
            height, width = screen.getmaxyx()
            body_rows = max(1, height - 2)
            max_scroll = max(0, len(overlay) - body_rows)
            help_scroll = min(help_scroll, max_scroll)
            while True:
                help_scroll = _render_help_overlay(screen, overlay, help_scroll)
                help_scroll, close = _read_help_scroll_key(
                    screen, help_scroll, max_scroll
                )
                if close:
                    break
            show_help = False
            help_scroll = 0
            confirm = tui_confirm_reset()
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
        if workspace_id is None:
            sync_line = (
                "Workspace ISO unavailable — build kernel-x64-userspace.iso, then i"
            )
            sync_attr = 0
        elif drift:
            sync_line = (
                "! poweron boots Default, not Workspace — press i to enroll this ISO"
            )
            sync_attr = curses.A_BOLD
        else:
            sync_line = "Default matches Workspace (same id and SHA-256)"
            sync_attr = 0
        screen.addstr(7, 2, _clip(sync_line, width - 4), sync_attr)
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

        screen.addstr(height - 3, 2, _clip(format_help_compact(width - 4), width - 4))
        screen.addstr(height - 2, 2, _clip(message, width - 4))
        screen.refresh()
        key = screen.getch()
        try:
            if key in (ord("q"), 27):
                return 0
            if key in (ord("h"), ord("?"), curses.KEY_F1):
                show_help = True
                help_scroll = 0
                confirm = tui_confirm_reset()
            elif key == curses.KEY_UP and entries:
                selected = (selected - 1) % len(entries)
            elif key == curses.KEY_DOWN and entries:
                selected = (selected + 1) % len(entries)
            elif key == curses.KEY_PPAGE and entries:
                selected = max(0, selected - visible)
            elif key == curses.KEY_NPAGE and entries:
                selected = min(len(entries) - 1, selected + visible)
            elif key in (10, 13) and entries:
                kernel_id = entries[selected]
                store.select(kernel_id)
                message = f"Selected {kernel_id}; previous retained as fallback"
                confirm = tui_confirm_reset()
                boot_rc = _maybe_boot_after_select(store, screen, kernel_id)
                if boot_rc is not None:
                    return boot_rc
            elif key == ord("i"):
                if source is None or version is None:
                    raise ValueError("workspace install source/version is not configured")
                workspace_id, _ = store.source_identity(source, version)
                store.install(source.resolve(), workspace_id)
                message = f"Installed and selected {workspace_id}"
                refresh_status = True
                confirm = tui_confirm_reset()
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
                confirm = tui_confirm_reset()
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
                confirm = tui_confirm_reset()
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
                confirm = tui_confirm_reset()
            elif key == ord("d") and entries:
                confirm, message, confirmed = tui_arm_delete(confirm, entries[selected])
                if confirmed:
                    store.delete(entries[selected])
                    refresh_status = True
            elif key == ord("p"):
                confirm, prune_message, confirmed = tui_arm_prune(confirm)
                if confirmed:
                    removed = store.prune()
                    message = f"Pruned {len(removed)} kernel(s)"
                    refresh_status = True
                else:
                    message = prune_message
            elif key == ord("v") and entries:
                valid, state = store.verify(entries[selected], enroll=True)
                message = f"{entries[selected]}: {state if valid else 'FAILED ' + state}"
                refresh_status = True
                confirm = tui_confirm_reset()
            elif key == ord("b"):
                store.resolve()
                kernel_id = store.current_id() or "Default"
                boot_rc = _maybe_boot_after_select(store, screen, kernel_id)
                if boot_rc is not None:
                    return boot_rc
                return TUI_EXIT_BOOT
        except (OSError, ValueError) as error:
            message = f"Error: {error}"
            confirm = tui_confirm_reset()


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
    parser.add_argument("--isd-root", type=Path)
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
    subparsers.add_parser("help", help="Print TUI/CLI key legend")
    subparsers.add_parser("tui")
    args = parser.parse_args()
    store = KernelStore(
        args.machine_dir,
        args.arch,
        args.profile,
        args.machine,
        kernel_root=args.kernel_root,
        isd_root=args.isd_root,
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
        elif args.command == "help":
            print(format_help_text())
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
