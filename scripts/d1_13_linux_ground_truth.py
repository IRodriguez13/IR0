#!/usr/bin/env python3
"""
D1.13 — Linux ground truth for BusyBox/musl (same ELF as IR0 smoke).

Runs PTY-like workload (python openpty + fork) and captures:
  - strace: read, brk, mmap/mmap2/munmap
  - /proc/pid/maps at key moments
  - optional gdb batch: memmove entry @ 0x4422bf, rep movsq @ 0x4422e3

Usage:
  python3 scripts/d1_13_linux_ground_truth.py [path/to/fase50_busybox_real]
"""

from __future__ import annotations

import os
import pty
import select
import shutil
import subprocess
import sys
import time
from pathlib import Path

MEMMOVE_ENTRY = 0x4422BF
REP_MOVSQ = 0x4422E3
INPUT_SCRIPT = b"echo hi\nexit\n"


def repo_root() -> Path:
    return Path(__file__).resolve().parents[1]


def default_bb() -> Path:
    return repo_root() / "setup/pid1/fase50_busybox_real"


def sh_exec_copy(bb: Path, out_dir: Path) -> Path:
    """BusyBox selects applet from argv[0] basename — file must be named sh."""
    sh_path = out_dir / "sh"
    shutil.copy2(bb, sh_path)
    sh_path.chmod(0o755)
    return sh_path


def run_pty_strace(bb: Path, out_dir: Path) -> None:
    strace_log = out_dir / "d1_13_linux_strace.log"
    maps_snap = out_dir / "d1_13_linux_maps.txt"
    timeline = out_dir / "d1_13_linux_timeline.txt"
    sh_path = sh_exec_copy(bb, out_dir)

    lines: list[str] = []

    pid, master_fd = pty.fork()
    if pid == 0:
        os.execvp(
            "strace",
            [
                "strace",
                "-f",
                "-o",
                str(strace_log),
                "-e",
                "trace=read,brk,mmap,mmap2,munmap,write",
                "-s",
                "256",
                "-yy",
                str(sh_path),
            ],
        )
        sys.exit(1)

    child = pid

    def snap_maps(label: str) -> None:
        maps_path = Path(f"/proc/{child}/maps")
        if not maps_path.exists():
            lines.append(f"[maps] {label}: child exited\n")
            return
        text = maps_path.read_text()
        with maps_snap.open("a") as f:
            f.write(f"=== {label} pid={child} ===\n")
            f.write(text)
            if not text.endswith("\n"):
                f.write("\n")
        lines.append(f"[maps] {label}: saved ({len(text.splitlines())} lines)\n")

    time.sleep(0.3)
    snap_maps("after_start")
    os.write(master_fd, INPUT_SCRIPT)
    lines.append(f"[pty] sent {len(INPUT_SCRIPT)} bytes: echo hi / exit\n")

    deadline = time.time() + 8.0
    out_buf = bytearray()
    while time.time() < deadline:
        r, _, _ = select.select([master_fd], [], [], 0.2)
        if master_fd in r:
            try:
                chunk = os.read(master_fd, 4096)
            except OSError:
                break
            if not chunk:
                break
            out_buf.extend(chunk)
        try:
            w = os.waitpid(child, os.WNOHANG)
            if w[0] == child:
                lines.append(f"[wait] child exit status={w[1]}\n")
                break
        except ChildProcessError:
            break
    else:
        os.kill(child, 9)
        lines.append("[wait] timeout — killed child\n")

    snap_maps("before_exit")
    try:
        os.waitpid(child, 0)
    except ChildProcessError:
        pass

    lines.append(f"[pty] output ({len(out_buf)} bytes):\n")
    lines.append(out_buf.decode("utf-8", errors="replace"))
    lines.append("\n")

    timeline.write_text("".join(lines))
    print(f"D1.13 Linux PTY strace -> {strace_log}")
    print(f"D1.13 Linux maps       -> {maps_snap}")
    print(f"D1.13 Linux timeline   -> {timeline}")


def parse_strace_summary(strace_log: Path, summary: Path) -> None:
    if not strace_log.exists():
        summary.write_text("strace log missing\n")
        return

    max_read_ret = 0
    read_events: list[str] = []
    mmap_events: list[str] = []
    brk_events: list[str] = []

    for line in strace_log.read_text(errors="replace").splitlines():
        if "= " not in line:
            continue
        if "read(" in line and "<unfinished" not in line:
            read_events.append(line)
            try:
                ret = line.rsplit("=", 1)[1].strip()
                if ret.isdigit() or (ret.startswith("-") and ret[1:].isdigit()):
                    val = int(ret)
                    if val > max_read_ret:
                        max_read_ret = val
            except ValueError:
                pass
        elif "mmap" in line:
            mmap_events.append(line)
        elif "brk(" in line:
            brk_events.append(line)

    with summary.open("w") as f:
        f.write("=== D1.13 Linux strace summary ===\n")
        f.write(f"read() events: {len(read_events)}\n")
        f.write(f"max read() return: {max_read_ret}\n")
        f.write(f"mmap events: {len(mmap_events)}\n")
        f.write(f"brk events: {len(brk_events)}\n\n")

        f.write("--- read() (all) ---\n")
        for ln in read_events:
            f.write(ln + "\n")
        f.write("\n--- brk() ---\n")
        for ln in brk_events:
            f.write(ln + "\n")
        f.write("\n--- mmap/mmap2 ---\n")
        for ln in mmap_events:
            f.write(ln + "\n")

    print(f"D1.13 Linux strace summary -> {summary}")


def run_gdb_memmove(bb: Path, out_dir: Path, use_pty: bool) -> None:
    gdb_log = out_dir / "d1_13_linux_gdb_memmove.log"
    gdb_cmds = out_dir / "d1_13_linux_gdb.cmds"
    sh_path = sh_exec_copy(bb, out_dir)

    gdb_cmds.write_text(
        f"""set pagination off
set disable-randomization on
file {sh_path}
set $d113_max = 0
set $d113_hits = 0
break *{MEMMOVE_ENTRY:#x}
commands 1
  silent
  set $d113_hits = $d113_hits + 1
  if $rdx > $d113_max
    set $d113_max = $rdx
    printf "D1.13[GDB][MEMMOVE_ENTRY] n=%#lx rsi=%#lx rdi=%#lx rip=%#lx\\n", $rdx, $rsi, $rdi, $rip
  end
  continue
end
break *{REP_MOVSQ:#x}
commands 2
  silent
  if $rdx > 0x1000 || $rcx > 0x100
    printf "D1.13[GDB][REP_MOVSQ] rcx=%#lx rdx=%#lx rsi=%#lx rdi=%#lx\\n", $rcx, $rdx, $rsi, $rdi
  end
  continue
end
run
printf "D1.13[GDB][SUMMARY] memmove_entries=%d max_n=%#lx\\n", $d113_hits, $d113_max
quit
"""
    )

    env = os.environ.copy()
    if use_pty:
        # GDB drives the binary; feed script via inferior stdin (pipe PTY via shell)
        wrapper = out_dir / "d1_13_gdb_wrapper.sh"
        wrapper.write_text(
            f"""#!/bin/bash
printf '{INPUT_SCRIPT.decode()}' | setarch -R '{bb}'
"""
        )
        wrapper.chmod(0o755)
        run_target = str(wrapper)
    else:
        run_target = str(bb)

    with gdb_log.open("w") as logf:
        proc = subprocess.run(
            [
                "gdb",
                "-batch",
                "-x",
                str(gdb_cmds),
            ],
            input=INPUT_SCRIPT,
            stdout=logf,
            stderr=subprocess.STDOUT,
            timeout=30,
            env=env,
        )
    _ = proc
    print(f"D1.13 Linux gdb memmove -> {gdb_log} (exit {proc.returncode})")


def main() -> int:
    bb = Path(sys.argv[1]) if len(sys.argv) > 1 else default_bb()
    if not bb.is_file():
        print(f"error: missing BusyBox ELF: {bb}", file=sys.stderr)
        return 1

    out_dir = Path("/tmp")
    maps_snap = out_dir / "d1_13_linux_maps.txt"
    maps_snap.write_text("")

    run_pty_strace(bb, out_dir)
    parse_strace_summary(out_dir / "d1_13_linux_strace.log", out_dir / "d1_13_linux_strace_summary.txt")
    run_gdb_memmove(bb, out_dir, use_pty=False)
    return 0


if __name__ == "__main__":
    sys.exit(main())
