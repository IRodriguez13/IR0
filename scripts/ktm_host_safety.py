#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Shared host-safety helpers for KTM tooling.

Everything here is standard-library only and avoids Linux-only assumptions so
the KTM runners work on Linux, BSD, and macOS. See
Documentation/ai_driven_dev/rules/ir0-ktm-host-portability-safety.md.
"""

from __future__ import annotations

import os
import re
import tempfile
from pathlib import Path
from typing import Iterator

LOG_CHUNK = 1 << 20
MAX_LOG_BYTES = 64 * 1024 * 1024
MAX_LINE_CHARS = 64 * 1024
DEFAULT_MEM_MIB = 128
MAX_MEM_MIB = 2048
HOST_HEADROOM_MIB = 512
MAX_TIMEOUT_SEC = 3600

MEM_RE = re.compile(r"(\d+)([KMG]?)", re.IGNORECASE)


def parse_mem_mib(text: str) -> int:
    """Parse a QEMU -m value (128, 128M, 1G) into MiB."""
    match = MEM_RE.fullmatch(text.strip())
    if not match:
        raise ValueError(f"invalid memory size {text!r} (use 128M, 1G, ...)")
    value = int(match.group(1))
    unit = match.group(2).upper()
    factor = {"": 1, "K": 1 / 1024, "M": 1, "G": 1024}[unit]
    mib = int(value * factor)
    if mib < 16:
        raise ValueError(f"guest RAM {text!r} is below the 16 MiB floor")
    return mib


def _read_int(path: str) -> int | None:
    try:
        raw = Path(path).read_text(encoding="ascii").strip()
    except OSError:
        return None
    return int(raw) if raw.isdigit() else None


def _cgroup_available_mib() -> int:
    """Linux cgroup v2/v1 headroom; 0 on hosts without cgroups."""
    for limit_path, usage_path in (
        ("/sys/fs/cgroup/memory.max", "/sys/fs/cgroup/memory.current"),
        (
            "/sys/fs/cgroup/memory/memory.limit_in_bytes",
            "/sys/fs/cgroup/memory/memory.usage_in_bytes",
        ),
    ):
        limit = _read_int(limit_path)
        usage = _read_int(usage_path) or 0
        if limit is not None and limit < (1 << 60):
            available = limit - usage
            if available > 0:
                return available // (1024 * 1024)
    return 0


def host_total_mib() -> int:
    try:
        pages = os.sysconf("SC_PHYS_PAGES")
        size = os.sysconf("SC_PAGE_SIZE")
    except (ValueError, OSError, AttributeError):
        return 0
    if pages <= 0 or size <= 0:
        return 0
    return pages * size // (1024 * 1024)


def host_available_mib() -> int:
    """Best-effort available host RAM, portable across Linux/BSD/macOS."""
    candidates: list[int] = []
    try:
        for line in Path("/proc/meminfo").read_text(encoding="ascii").splitlines():
            if line.startswith("MemAvailable:"):
                candidates.append(int(line.split()[1]) // 1024)
                break
    except (OSError, ValueError):
        pass
    cgroup = _cgroup_available_mib()
    if cgroup:
        candidates.append(cgroup)
    if candidates:
        return min(candidates)
    # BSD/macOS: no /proc/meminfo. SC_AVPHYS_PAGES is free (not available)
    # memory, so use it only when nothing better exists.
    try:
        pages = os.sysconf("SC_AVPHYS_PAGES")
        size = os.sysconf("SC_PAGE_SIZE")
        if pages > 0 and size > 0:
            return pages * size // (1024 * 1024)
    except (ValueError, OSError, AttributeError):
        pass
    # Last resort: half of physical RAM is a safe proxy.
    total = host_total_mib()
    return total // 2 if total else 0


def ensure_qemu_mem(
    qemu_cmd: list[str],
    requested_mib: int = DEFAULT_MEM_MIB,
    max_mib: int = MAX_MEM_MIB,
    allow_low_mem: bool = False,
) -> list[str]:
    """Enforce an explicit, host-safe -m on every QEMU invocation."""
    if requested_mib <= 0:
        raise ValueError("guest RAM must be positive")
    # The 2 GiB ceiling is a contract, not a default: a caller may lower the
    # cap but never raise it.
    max_mib = min(max_mib, MAX_MEM_MIB)
    total = host_total_mib()
    host_cap = min(max_mib, max(64, total // 4)) if total else max_mib
    if requested_mib > host_cap:
        raise ValueError(
            f"guest RAM {requested_mib} MiB exceeds the host-safe cap {host_cap} MiB"
        )

    cmd = list(qemu_cmd)
    positions = [i for i, arg in enumerate(cmd) if arg == "-m"]
    if len(positions) > 1:
        raise ValueError("QEMU command has multiple -m flags; keep one")
    if positions:
        index = positions[0]
        if index + 1 >= len(cmd):
            raise ValueError("-m has no value")
        requested_mib = parse_mem_mib(cmd[index + 1])
        if requested_mib > host_cap:
            raise ValueError(
                f"guest RAM {requested_mib} MiB exceeds the host-safe cap {host_cap} MiB"
            )
    else:
        cmd += ["-m", f"{requested_mib}M"]

    available = host_available_mib()
    if (
        not allow_low_mem
        and available
        and requested_mib + HOST_HEADROOM_MIB > available
    ):
        raise RuntimeError(
            f"guest {requested_mib} MiB + {HOST_HEADROOM_MIB} MiB headroom "
            f"exceeds available host RAM ({available} MiB); stop other VMs or "
            "pass --allow-low-mem deliberately"
        )
    return cmd


def make_temp_disk(prefix: str = "ir0-ktm-") -> Path:
    """Secure temp disk path; caller owns cleanup."""
    fd, name = tempfile.mkstemp(prefix=prefix, suffix=".img")
    os.close(fd)
    return Path(name)


def read_log_text(path: Path, max_bytes: int = MAX_LOG_BYTES) -> str:
    """Read a log for analysis tools, capped at ``max_bytes``.

    Live runners must stream with :func:`iter_log_lines`; post-mortem tools
    that need the whole text use this bounded read instead of ``read_text``.
    """
    with path.open("rb") as fp:
        data = fp.read(max_bytes)
    return data.decode("utf-8", errors="replace")


def iter_log_lines(
    path: Path, max_bytes: int = MAX_LOG_BYTES
) -> Iterator[str]:
    """Yield decoded serial lines with bounded memory.

    The log is read in fixed chunks and only one partial line is retained, so a
    multi-gigabyte log cannot exhaust host RAM. Stops after ``max_bytes``.
    """
    pending = ""
    total = 0
    with path.open("rb") as fp:
        while True:
            chunk = fp.read(LOG_CHUNK)
            if not chunk:
                break
            total += len(chunk)
            if total > max_bytes:
                break
            text = chunk.decode("utf-8", errors="replace")
            pending += text.replace("\0", "").replace("\r", "")
            parts = pending.split("\n")
            pending = parts.pop()
            for line in parts:
                yield line
            if len(pending) > MAX_LINE_CHARS:
                pending = pending[-MAX_LINE_CHARS:]
    if pending:
        yield pending


def scan_log(
    path: Path, needles: list[str], max_bytes: int = MAX_LOG_BYTES
) -> tuple[set[str], list[str]]:
    """Stream a serial log with a fixed buffer.

    Returns the needles found (newlines ignored, so tags split across lines
    still match) and up to 200 interesting KTM/HOSTSHARE lines for failures.
    """
    found: set[str] = set()
    matches: list[str] = []
    if not path.is_file():
        return found, matches
    overlap = max((len(n) for n in needles), default=1) - 1
    tail = ""
    total = 0
    with path.open("rb") as fp:
        while True:
            chunk = fp.read(LOG_CHUNK)
            if not chunk:
                break
            total += len(chunk)
            if total > max_bytes:
                break
            text = (tail + chunk.decode("utf-8", errors="replace"))
            text = text.replace("\0", "").replace("\r", "")
            flat = text.replace("\n", "")
            for needle in needles:
                if needle in flat:
                    found.add(needle)
            if len(matches) < 200:
                for line in text.splitlines():
                    if "KTM|" in line or "KTM_" in line or "HOSTSHARE_" in line:
                        matches.append(line)
                        if len(matches) >= 200:
                            break
            tail = text[-overlap:] if overlap > 0 else ""
    return found, matches
