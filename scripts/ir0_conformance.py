#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Boot an IR0 conformance or fuzz guest and judge its serial log.

The guest program (tests/conformance) runs as /sbin/init under the bounded
QEMU runner (scripts/smoke_qemu_run.sh: capped memory, hard timeout, stale
window, process-group kill). This script never reads a whole log into memory.

  run       boot one guest and judge it (conformance against a baseline, or
            the fuzzer's health verdict)
  judge     judge an existing log only (no QEMU)
  ltp-build build listed LTP testcases from an existing LTP checkout into a
            private directory (never modifies LTP_SRC)
"""

from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import stat
import subprocess
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import ktm_host_safety as safety  # noqa: E402

ROOT = Path(__file__).resolve().parents[1]
STATUSES = ("PASS", "FAIL", "TCONF", "BROK")
PLAN_RE = re.compile(r"^1\.\.(\d+)\s*$")
RESULT_RE = re.compile(
    r"^(ok|not ok) (\d+) ([A-Za-z0-9_.+-]+)(?: # (SKIP TCONF|ERROR BROK)(.*))?\s*$"
)
PANIC_RE = re.compile(r"KERNEL PANIC|DOUBLE PANIC|panicex\(|\bpanic\(")
CONF_DONE = "IR0_CONFORMANCE_DONE"
FUZZ_DONE = "IR0_FUZZ_DONE"
FUZZ_LOST = "IR0_FUZZ_HEALTH_LOST"
FUZZ_SETUP = "IR0_FUZZ_SETUP_BROKEN"
# The ARM64 early rootfs buffer (arch/arm64/sources/boot_port.c).
ARM64_DISK_MAX = 128 * 1024
TAIL_LINES = 30


def run_cmd(argv: list[str], timeout: int) -> int:
    return subprocess.run(argv, cwd=str(ROOT), check=False, timeout=timeout).returncode


# --- guest images ----------------------------------------------------------


def cpio_member(name: str, mode: int, data: bytes, ino: int) -> bytes:
    encoded = name.encode("utf-8") + b"\0"
    fields = (ino, mode, 0, 0, 1, 0, len(data), 0, 0, 0, 0, len(encoded), 0)
    out = bytearray(b"070701")
    out += b"".join(f"{v:08x}".encode("ascii") for v in fields)
    out += encoded
    out += b"\0" * (-len(out) % 4)
    out += data
    out += b"\0" * (-len(out) % 4)
    return bytes(out)


ARM64_STUB = ROOT / "build" / "conformance" / "arm64" / "init-stub"


def arm64_cpio(init: Path, out: Path) -> None:
    """newc archive: /init is a stub ELF (the early rootfs binds a CPIO only
    with an ELF /init), /sbin/init the program the kernel execs. Two copies
    of the runner would not fit the 128 KiB early-rootfs buffer."""
    if not ARM64_STUB.is_file():
        raise SystemExit(f"ir0-conformance: missing {ARM64_STUB} "
                         "(make -C tests/conformance ARCH=arm64 stub)")
    entries = (
        ("init", stat.S_IFREG | 0o755, ARM64_STUB.read_bytes()),
        ("sbin", stat.S_IFDIR | 0o755, b""),
        ("sbin/init", stat.S_IFREG | 0o755, init.read_bytes()),
        ("etc", stat.S_IFDIR | 0o755, b""),
        # The bpivot commit checks this marker (fs/bpivot.c).
        ("etc/pivot-probe", stat.S_IFREG | 0o644, b"bpivot-arm64\n"),
        ("TRAILER!!!", 0, b""),
    )
    archive = bytearray()
    for ino, (name, mode, payload) in enumerate(entries, start=1):
        archive += cpio_member(name, mode, payload, ino)
    archive += b"\0" * (-len(archive) % 512)
    if len(archive) > ARM64_DISK_MAX:
        raise SystemExit(
            f"ir0-conformance: {init.name} makes a {len(archive)} byte archive; "
            f"the ARM64 early rootfs holds {ARM64_DISK_MAX}"
        )
    out.write_bytes(archive)


def x86_disk(init: Path, out: Path, ltp_dir: Path | None) -> int:
    # The large layout (2048 inodes, 64 MiB of zones) leaves room for cases
    # that fill a directory; the default one has 64 inodes in all.
    with out.open("wb") as fp:
        fp.truncate(64 * 1024 * 1024)
    inject = [sys.executable, str(ROOT / "scripts" / "inject_init_minix.py")]
    if run_cmd([*inject, "--format-large", str(out)], 60) != 0:
        return 1
    if run_cmd([*inject, str(out), str(init), "sbin/init"], 60) != 0:
        return 1
    if ltp_dir is None:
        return 0
    manifest = ltp_dir / "manifest"
    if run_cmd([*inject, str(out), str(manifest), "ltp/manifest"], 60) != 0:
        return 1
    for name in read_manifest(manifest):
        binary = ltp_dir / "bin" / name
        if run_cmd([*inject, "--mode", "755", str(out), str(binary), f"ltp/bin/{name}"], 60) != 0:
            return 1
    return 0


def read_manifest(path: Path) -> list[str]:
    names = []
    for line in safety.iter_log_lines(path, max_bytes=1024 * 1024):
        name = line.split("#", 1)[0].strip()
        if not name:
            continue
        if "/" in name or name.startswith(".") or len(name) > 14:
            raise SystemExit(f"ir0-conformance: bad LTP case name {name!r} in {path}")
        names.append(name)
    return names


def boot(args: argparse.Namespace, done: str, fail_tags: list[str]) -> int:
    binary = Path(args.binary).resolve()
    log = Path(args.log)
    if not binary.is_file():
        raise SystemExit(f"ir0-conformance: missing {binary} (build it first)")
    log.parent.mkdir(parents=True, exist_ok=True)
    if log.exists():
        log.unlink()
    smoke = [str(ROOT / "scripts" / "smoke_qemu_run.sh"), "--log", str(log),
             "--timeout", str(args.timeout), "--stale-sec", str(args.stale_sec),
             "--mem", "256M"]
    for tag in fail_tags:
        smoke += ["--fail-regex", tag]
    tmpdir = os.environ.get("TMPDIR") or None
    if args.arch == "x86_64":
        iso = ROOT / "kernel-x64-userspace.iso"
        if not iso.exists():
            raise SystemExit("ir0-conformance: missing kernel-x64-userspace.iso "
                             "(make kernel-x64-userspace.iso)")
        fd, name = tempfile.mkstemp(prefix="ir0-conf-x86.", suffix=".img", dir=tmpdir)
        os.close(fd)
        disk = Path(name)
        try:
            ltp = Path(args.ltp_dir) if args.ltp_dir else None
            if x86_disk(binary, disk, ltp) != 0:
                return 1
            argv = [*smoke, "--done", done, "--",
                    "qemu-system-x86_64", "-cdrom", str(iso),
                    "-drive", f"file={disk},format=raw,if=ide,index=0",
                    "-serial", "stdio", "-display", "none", "-no-reboot",
                    "-net", "none"]
            return run_cmd(argv, args.timeout + 30)
        finally:
            disk.unlink(missing_ok=True)
    product = ROOT / "kernel-arm64-product.img"
    if not product.is_file():
        product = ROOT / "kernel-arm64-product.bin"
    if not product.is_file():
        raise SystemExit("ir0-conformance: missing kernel-arm64-product.bin "
                         "(make kernel-arm64-product.bin)")
    if args.ltp_dir:
        print("ir0-conformance: LTP binaries do not fit the ARM64 early rootfs; "
              "running IR0 cases only", file=sys.stderr)
    fd, name = tempfile.mkstemp(prefix="ir0-conf-arm64.", dir=tmpdir)
    os.close(fd)
    cpio = Path(name)
    try:
        arm64_cpio(binary, cpio)
        argv = [*smoke, "--expected-panic", "--success-mode", "all",
                "--done", "ARM64_CPIO_ROOT_OK", "--done", "INIT_EXEC_LOADED_OK",
                "--done", done, "--",
                "qemu-system-aarch64", "-M", "virt,gic-version=2",
                "-cpu", "cortex-a57", "-global", "virtio-mmio.force-legacy=false",
                "-kernel", str(product), "-nographic", "-serial", "mon:stdio",
                "-drive", f"file={cpio},format=raw,if=none,id=conf",
                "-device", "virtio-blk-device,drive=conf",
                "-display", "none", "-no-reboot"]
        return run_cmd(argv, args.timeout + 30)
    finally:
        cpio.unlink(missing_ok=True)


# --- judging ---------------------------------------------------------------


def parse_conformance(log: Path) -> dict:
    planned = None
    cases: dict[str, dict] = {}
    order: list[str] = []
    panic: list[str] = []
    post_done: list[str] = []
    tail: list[str] = []
    done = False
    summary = ""
    for raw in safety.iter_log_lines(log):
        line = raw.strip()
        tail.append(line)
        if len(tail) > TAIL_LINES:
            tail.pop(0)
        # After the done tag the verdict is in; a later panic (for example
        # when PID 1 exits) is reported but does not change it.
        if PANIC_RE.search(line) and len(panic) < 5:
            (post_done if done else panic).append(line)
        if line == CONF_DONE:
            done = True
            continue
        if line.startswith("# IR0_CONFORMANCE_SUMMARY"):
            summary = line[2:]
            continue
        m = PLAN_RE.match(line)
        if m and planned is None:
            planned = int(m.group(1))
            continue
        m = RESULT_RE.match(line)
        if not m:
            continue
        ok, _num, name, directive, detail = m.groups()
        if directive == "SKIP TCONF":
            status = "TCONF"
        elif directive == "ERROR BROK":
            status = "BROK"
        else:
            status = "PASS" if ok == "ok" else "FAIL"
        if name not in cases:
            order.append(name)
        cases[name] = {"status": status, "detail": (detail or "").strip()}
    return {"planned": planned, "cases": cases, "order": order, "done": done,
            "panic": panic, "panic_after_done": post_done, "tail": tail,
            "summary": summary}


def read_baseline(path: Path | None) -> dict[str, str]:
    """Lines: '<case> <STATUS> [# why]'. Unlisted cases are expected to PASS."""
    expected: dict[str, str] = {}
    if path is None or not path.is_file():
        return expected
    for line in safety.iter_log_lines(path, max_bytes=1024 * 1024):
        body = line.split("#", 1)[0].split()
        if not body:
            continue
        if len(body) != 2 or body[1] not in STATUSES:
            raise SystemExit(f"ir0-conformance: bad baseline line {line!r} in {path}")
        expected[body[0]] = body[1]
    return expected


DEFAULT_CASES = ROOT / "tests" / "conformance" / "cases.h"


def known_cases(cases_h: Path = DEFAULT_CASES) -> list[str]:
    names = []
    for line in safety.iter_log_lines(cases_h, max_bytes=1024 * 1024):
        m = re.match(r"^CONF_CASE\(([a-z0-9_]+)\)", line)
        if m:
            names.append(m.group(1))
    return names


def judge_conformance(log: Path, baseline: Path | None, arch: str,
                      summary_path: Path | None, strict: bool,
                      cases_h: Path = DEFAULT_CASES) -> int:
    parsed = parse_conformance(log)
    expected = read_baseline(baseline)
    cases = parsed["cases"]
    # Cases the guest never reported (it died or hung) are BROK.
    for name in known_cases(cases_h):
        if name not in cases:
            why = "guest stopped before this case"
            if parsed["panic"]:
                why += " (kernel panic)"
            cases[name] = {"status": "BROK", "detail": why}
            parsed["order"].append(name)
    totals = {s: 0 for s in STATUSES}
    regressions = []
    fixed = []
    changed = []
    for name in parsed["order"]:
        got = cases[name]["status"]
        want = expected.get(name, "PASS")
        cases[name]["expected"] = want
        totals[got] += 1
        if got == want:
            continue
        if got == "PASS":
            fixed.append(name)
        elif want == "PASS" or got == "BROK":
            regressions.append(name)
        else:
            changed.append(name)
    stale = sorted(n for n in expected if n not in cases)
    result = {
        "arch": arch,
        "log": str(log),
        "baseline": str(baseline) if baseline else None,
        "guest_done": parsed["done"],
        "kernel_panic": parsed["panic"],
        "panic_after_done": parsed["panic_after_done"],
        "totals": totals,
        "regressions": regressions,
        "fixed": fixed,
        "changed": changed,
        "stale_baseline": stale,
        "cases": cases,
    }
    if summary_path:
        summary_path.parent.mkdir(parents=True, exist_ok=True)
        summary_path.write_text(json.dumps(result, indent=2, sort_keys=True) + "\n")
    print(f"ir0-conformance[{arch}]: pass={totals['PASS']} fail={totals['FAIL']} "
          f"tconf={totals['TCONF']} brok={totals['BROK']} "
          f"(known non-pass in baseline: {len(expected)})")
    for name in regressions:
        print(f"  REGRESSION {name}: {cases[name]['status']} "
              f"(baseline {cases[name]['expected']}) {cases[name]['detail']}")
    for name in changed:
        print(f"  CHANGED {name}: {cases[name]['status']} (baseline {cases[name]['expected']})")
    for name in fixed:
        print(f"  FIXED {name}: now PASS; drop it from {baseline}")
    for name in stale:
        print(f"  STALE baseline entry {name}: no such case")
    if not parsed["done"]:
        print("ir0-conformance: guest never printed " + CONF_DONE, file=sys.stderr)
        for line in parsed["panic"]:
            print(f"  panic: {line}", file=sys.stderr)
        for line in parsed["tail"][-10:]:
            print(f"  tail: {line}", file=sys.stderr)
    if summary_path:
        print(f"ir0-conformance: summary {summary_path}")
    bad = bool(regressions or stale or not parsed["done"])
    if strict and (fixed or changed):
        bad = True
    return 1 if bad else 0


def judge_fuzz(log: Path, arch: str, summary_path: Path | None) -> int:
    done = False
    lost = ""
    panic: list[str] = []
    summary = ""
    oracles: list[str] = []
    crashes: list[str] = []
    last_batch = ""
    tail: list[str] = []
    for raw in safety.iter_log_lines(log):
        line = raw.strip()
        tail.append(line)
        if len(tail) > TAIL_LINES:
            tail.pop(0)
        if line == FUZZ_DONE:
            done = True
        elif line.startswith(FUZZ_LOST) or line.startswith(FUZZ_SETUP):
            lost = line
        elif line.startswith("# IR0_FUZZ_SUMMARY"):
            summary = line[2:]
        elif line.startswith("# oracle") and len(oracles) < 50:
            oracles.append(line[2:])
        elif line.startswith("# batch "):
            last_batch = line[2:]
            if ("child=signal" in line or "child=hung" in line) and len(crashes) < 50:
                crashes.append(line[2:])
        if PANIC_RE.search(line) and len(panic) < 5 and not done:
            panic.append(line)
    healthy = done and not lost and not panic
    result = {"arch": arch, "log": str(log), "healthy": healthy, "done": done,
              "health_lost": lost, "kernel_panic": panic, "summary": summary,
              "last_batch": last_batch, "oracle_findings": oracles,
              "child_crashes": crashes}
    if summary_path:
        summary_path.parent.mkdir(parents=True, exist_ok=True)
        summary_path.write_text(json.dumps(result, indent=2, sort_keys=True) + "\n")
    print(f"ir0-fuzz[{arch}]: {'HEALTHY' if healthy else 'KERNEL UNHEALTHY'} {summary}")
    for line in oracles:
        print(f"  ORACLE {line}")
    if not healthy:
        print(f"  last batch: {last_batch or '(none)'}", file=sys.stderr)
        if lost:
            print(f"  {lost}", file=sys.stderr)
        for line in panic:
            print(f"  panic: {line}", file=sys.stderr)
        for line in tail[-12:]:
            print(f"  tail: {line}", file=sys.stderr)
        print("  replay one batch: make ir0-fuzz-<isa> SEED=<n> COUNT=<n> "
              "BATCH=<b> TRACE=1", file=sys.stderr)
    return 0 if healthy and not oracles else 1


# --- LTP -------------------------------------------------------------------


def ltp_build(args: argparse.Namespace) -> int:
    """Copy LTP_SRC to a private tree, build lib + the listed cases statically."""
    src = Path(args.ltp_src).resolve()
    if not (src / "lib" / "tst_test.c").is_file():
        raise SystemExit(f"ir0-conformance: {src} is not an LTP checkout (lib/tst_test.c)")
    out = Path(args.out).resolve()
    names = []
    for line in safety.iter_log_lines(Path(args.cases), max_bytes=1024 * 1024):
        body = line.split("#", 1)[0].split()
        if body:
            names.append((body[0], body[1]))
    work = Path(tempfile.mkdtemp(prefix="ir0-ltp-build.", dir=os.environ.get("TMPDIR") or None))
    try:
        tree = work / "ltp"
        shutil.copytree(src, tree, symlinks=True, ignore=shutil.ignore_patterns(".git"))
        env = dict(os.environ, CC=args.cc, CFLAGS="-static -O2", LDFLAGS="-static",
                   LC_ALL="C")
        steps = []
        if not (tree / "configure").is_file():
            steps.append(["make", "autotools"])
        steps.append(["./configure", f"--host={args.host}", "--without-numa",
                      "--without-tirpc"])
        steps.append(["make", "-C", "lib", f"-j{args.jobs}"])
        for _name, rel in names:
            steps.append(["make", "-C", f"testcases/kernel/syscalls/{rel}", f"-j{args.jobs}"])
        for step in steps:
            print("ir0-ltp:", " ".join(step))
            if subprocess.run(step, cwd=str(tree), env=env, check=False,
                              timeout=1800).returncode != 0:
                print(f"ir0-ltp: step failed: {' '.join(step)}", file=sys.stderr)
                return 1
        (out / "bin").mkdir(parents=True, exist_ok=True)
        built = []
        for name, rel in names:
            binary = tree / "testcases" / "kernel" / "syscalls" / rel / name
            if binary.is_file():
                shutil.copy2(binary, out / "bin" / name)
                built.append(name)
            else:
                print(f"ir0-ltp: {name} was not built", file=sys.stderr)
        (out / "manifest").write_text("".join(f"{n}\n" for n in built))
        print(f"ir0-ltp: {len(built)} testcases in {out}")
        return 0
    finally:
        shutil.rmtree(work, ignore_errors=True)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="cmd", required=True)
    for name in ("run", "judge"):
        p = sub.add_parser(name)
        p.add_argument("--arch", choices=("x86_64", "arm64"), required=True)
        p.add_argument("--mode", choices=("conformance", "fuzz"), required=True)
        p.add_argument("--log", required=True)
        p.add_argument("--baseline")
        p.add_argument("--summary")
        p.add_argument("--cases", default=str(DEFAULT_CASES),
                       help="case list header (CONF_CASE lines)")
        p.add_argument("--strict", action="store_true",
                       help="also fail when a known failure now passes or changes")
        if name == "run":
            p.add_argument("--binary", required=True)
            p.add_argument("--timeout", type=int, default=900)
            p.add_argument("--stale-sec", type=int, default=40)
            p.add_argument("--ltp-dir", default="")
    p = sub.add_parser("ltp-build")
    p.add_argument("--ltp-src", required=True)
    p.add_argument("--cases", required=True)
    p.add_argument("--out", required=True)
    p.add_argument("--cc", required=True)
    p.add_argument("--host", default="x86_64-linux-musl")
    p.add_argument("--jobs", type=int, default=4)
    args = parser.parse_args()

    if args.cmd == "ltp-build":
        return ltp_build(args)
    log = Path(args.log)
    summary = Path(args.summary) if args.summary else None
    if args.cmd == "run":
        if args.mode == "conformance":
            rc = boot(args, CONF_DONE, [])
        else:
            rc = boot(args, FUZZ_DONE, [FUZZ_LOST, FUZZ_SETUP])
        if rc != 0:
            print(f"ir0-conformance: QEMU runner rc={rc} log={log}", file=sys.stderr)
        if not log.is_file():
            return 1
    if args.mode == "fuzz":
        return judge_fuzz(log, args.arch, summary)
    baseline = Path(args.baseline) if args.baseline else None
    return judge_conformance(log, baseline, args.arch, summary, args.strict,
                             Path(args.cases))


if __name__ == "__main__":
    raise SystemExit(main())
