#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""
Capture and verify PID1 boot behavior for ISD init profiles (runit, sysvinit, openrc).

Writes under out/init-boot-capture/<arch>/<profile>/:
  serial.log      full QEMU serial
  boot-window.txt excerpt between contract boot markers
  manifest.json   tags found, staged init audit, pass/fail
  CAPTURE.md      human-readable report (audit trail for usmang / CI)

Usage:
  python3 scripts/init_boot_capture.py --profile minimal-sysvinit
  python3 scripts/init_boot_capture.py --profile minimal-openrc --smoke-only
  python3 scripts/init_boot_capture.py --matrix --arch x86_64
"""

from __future__ import annotations

import argparse
import datetime as dt
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Any

ROOT = Path(__file__).resolve().parents[1]
CONTRACT_PATH = ROOT / "scripts" / "init_boot_contract.json"
SMOKE_RUN = ROOT / "scripts" / "smoke_qemu_run.sh"
PROMPT_RE = re.compile(r"[a-zA-Z0-9_-]+@[a-zA-Z0-9_-]+:\S*[#$]")


class InitBootError(Exception):
    pass


class InitBootSkip(Exception):
    pass


def load_contract() -> dict[str, Any]:
    return json.loads(CONTRACT_PATH.read_text(encoding="utf-8"))


def resolve_isd_root() -> Path:
    env = os.environ.get("IR0_ISD_ROOT")
    if env:
        return Path(env).resolve()
    script = ROOT / "scripts" / "resolve_isd_root.sh"
    if script.is_file():
        result = subprocess.run(
            ["bash", str(script), str(ROOT)],
            text=True,
            capture_output=True,
            check=False,
        )
        if result.returncode == 0 and result.stdout.strip():
            return Path(result.stdout.strip()).resolve()
    return (ROOT.parent / "ISD").resolve()


def profile_init_system(isd: Path, profile: str) -> str:
    conf = isd / "profiles" / profile / "profile.conf"
    if not conf.is_file():
        return "unknown"
    for raw in conf.read_text(encoding="utf-8", errors="replace").splitlines():
        line = raw.strip()
        if line.startswith("INIT_SYSTEM="):
            return line.split("=", 1)[1].strip()
    return "runit"


def staged_rootfs(isd: Path, profile: str, arch: str) -> Path:
    return isd / "out" / arch / "rootfs" / profile


def disk_image(isd: Path, profile: str, arch: str) -> Path:
    return isd / "out" / arch / "images" / profile / "disk.img"


def ensure_disk(isd: Path, profile: str, arch: str) -> Path:
    img = disk_image(isd, profile, arch)
    if img.is_file():
        return img
    env = {
        **os.environ,
        "IR0_ROOT": str(ROOT),
        "PROFILE": profile,
        "ARCH": arch,
        "ISD_ARCH": arch,
    }
    print(f"  BUILD   ISD disk PROFILE={profile} ARCH={arch} …", flush=True)
    subprocess.run(
        ["make", "-s", "-C", str(ROOT), f"ensure-isd-disk", f"PROFILE={profile}", f"ARCH={arch}"],
        check=True,
        env=env,
    )
    if not img.is_file():
        raise InitBootError(f"missing disk after ensure-isd-disk: {img}")
    return img


def ensure_kernel_iso(arch_cfg: dict[str, Any]) -> Path:
    iso_name = arch_cfg["kernel_iso"]
    iso = ROOT / iso_name
    if iso.is_file():
        return iso
    if arch_cfg.get("status") == "scaffold":
        raise InitBootSkip(
            f"{iso_name} not built — {arch_cfg.get('note', 'build kernel ISO first')}"
        )
    print(f"  BUILD   {iso_name} …", flush=True)
    subprocess.run(["make", "-s", "-C", str(ROOT), iso_name], check=True)
    if not iso.is_file():
        raise InitBootError(f"missing kernel ISO: {iso}")
    return iso


def audit_staged_init(
    isd: Path, profile: str, arch: str, spec: dict[str, Any]
) -> dict[str, Any]:
    tree = staged_rootfs(isd, profile, arch)
    init_rel = spec.get("staged_init_path", "sbin/init")
    impl_rel = spec.get("staged_init_impl", init_rel)
    init_path = tree / init_rel
    impl_path = tree / impl_rel
    profile_init = profile_init_system(isd, profile)
    contract_init = spec.get("init_system", profile_init)
    rows: list[str] = []
    ok = True
    if not tree.is_dir():
        return {
            "ok": False,
            "staged_rootfs": str(tree),
            "reason": "rootfs not staged",
        }
    if profile_init != contract_init:
        ok = False
        rows.append(
            f"profile INIT_SYSTEM={profile_init} != contract init_system={contract_init}"
        )
    if not init_path.exists():
        ok = False
        rows.append(f"missing {init_rel}")
    if impl_rel != init_rel and not impl_path.exists():
        ok = False
        rows.append(f"missing impl {impl_rel}")
    prof_file = tree / "etc" / "ir0-profile"
    got_profile = prof_file.read_text(encoding="utf-8", errors="replace").strip()
    if got_profile != profile:
        ok = False
        rows.append(f"etc/ir0-profile={got_profile!r} expected {profile!r}")
    return {
        "ok": ok,
        "staged_rootfs": str(tree),
        "init_system_profile": profile_init,
        "init_system_contract": contract_init,
        "init_path": str(init_path),
        "impl_path": str(impl_path),
        "issues": rows,
    }


def extract_boot_window(text: str, start_tag: str, end_tag: str) -> str:
    start = text.find(start_tag)
    if start < 0:
        start = 0
    end = text.find(end_tag, start)
    if end < 0:
        return text[start:]
    end_line = text.find("\n", end)
    if end_line < 0:
        return text[start:]
    return text[start : end_line + 1]


def tag_report(text: str, required: list[str], optional: list[str]) -> dict[str, Any]:
    found_required = [t for t in required if t in text]
    missing_required = [t for t in required if t not in text]
    found_optional = [t for t in optional if t in text]
    return {
        "found_required": found_required,
        "missing_required": missing_required,
        "found_optional": found_optional,
        "has_login_prompt": bool(PROMPT_RE.search(text)),
    }


def write_capture_md(
    out_dir: Path,
    profile: str,
    arch: str,
    audit: dict[str, Any],
    tags: dict[str, Any],
    boot_window: str,
    passed: bool,
) -> None:
    now = dt.datetime.now(dt.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")
    lines = [
        f"# Init boot capture — `{profile}` ({arch})",
        "",
        f"> **Captured:** {now}",
        f"> **Contract:** `scripts/init_boot_contract.json`",
        f"> **Result:** {'PASS' if passed else 'FAIL'}",
        "",
        "## Staged init audit (pre-boot, host-side)",
        "",
        f"- init_system (profile): `{audit.get('init_system_profile')}`",
        f"- init_system (contract): `{audit.get('init_system_contract')}`",
        f"- staged rootfs: `{audit.get('staged_rootfs')}`",
        f"- `/sbin/init` path: `{audit.get('init_path')}`",
        f"- implementation: `{audit.get('impl_path')}`",
    ]
    issues = audit.get("issues") or []
    if issues:
        lines.append("- **issues:**")
        for item in issues:
            lines.append(f"  - {item}")
    else:
        lines.append("- staged audit: **OK**")
    lines.extend(
        [
            "",
            "## Serial tags",
            "",
            f"- required found: `{', '.join(tags['found_required']) or '(none)'}`",
            f"- required missing: `{', '.join(tags['missing_required']) or '(none)'}`",
            f"- optional found: `{', '.join(tags['found_optional']) or '(none)'}`",
            f"- login prompt seen: `{tags['has_login_prompt']}`",
            "",
            "## Boot window (serial excerpt)",
            "",
            "```",
            boot_window.rstrip() or "(empty — boot markers not seen)",
            "```",
            "",
        ]
    )
    (out_dir / "CAPTURE.md").write_text("\n".join(lines), encoding="utf-8")


def run_capture(
    profile: str,
    arch: str,
    *,
    smoke_only: bool = False,
    timeout: int = 90,
    stale_sec: int = 25,
    out_root: Path | None = None,
) -> dict[str, Any]:
    contract = load_contract()
    if profile not in contract["profiles"]:
        raise InitBootError(f"unknown profile in contract: {profile}")
    arch_cfg = contract["architectures"].get(arch)
    if arch_cfg is None:
        raise InitBootError(f"unknown arch in contract: {arch}")

    spec = contract["profiles"][profile]
    isd = resolve_isd_root()
    iso = ensure_kernel_iso(arch_cfg)
    disk_src = ensure_disk(isd, profile, arch)
    audit = audit_staged_init(isd, profile, arch, spec)

    out_dir = (out_root or ROOT / "out" / "init-boot-capture" / arch / profile).resolve()
    if not smoke_only:
        out_dir.mkdir(parents=True, exist_ok=True)
    log_path = out_dir / "serial.log" if not smoke_only else Path(tempfile.mktemp(suffix=".log"))

    tmp_disk = Path(tempfile.mktemp(suffix=".img"))
    shutil.copy2(disk_src, tmp_disk)
    try:
        success_tags = spec["autokill_success"]
        cmd = [
            str(SMOKE_RUN),
            "--log",
            str(log_path),
            "--timeout",
            str(timeout),
            "--stale-sec",
            str(stale_sec),
        ]
        for tag in success_tags:
            cmd.extend(["--done", tag])
        cmd.append("--")
        cmd.append(arch_cfg["qemu"])
        cmd.extend(["-cdrom", str(iso)])
        cmd.extend(arch_cfg["disk_drive"].format(disk=str(tmp_disk)).split())
        cmd.extend(arch_cfg["qemu_args"])

        print(f"  SMOKE   init boot PROFILE={profile} ARCH={arch}", flush=True)
        result = subprocess.run(cmd, cwd=ROOT, check=False)
        serial = log_path.read_text(encoding="utf-8", errors="replace") if log_path.is_file() else ""
    finally:
        tmp_disk.unlink(missing_ok=True)

    required = spec["required_serial_tags"]
    optional = spec.get("optional_serial_tags", [])
    tags = tag_report(serial, required, optional)
    boot_window = extract_boot_window(
        serial,
        spec.get("boot_window_start", required[0]),
        spec.get("boot_window_end", "GETTY_READY"),
    )

    passed = (
        result.returncode == 0
        and audit.get("ok", False)
        and not tags["missing_required"]
        and "KERNEL PANIC" not in serial
    )

    manifest = {
        "profile": profile,
        "arch": arch,
        "init_system": spec["init_system"],
        "passed": passed,
        "smoke_exit_code": result.returncode,
        "staged_audit": audit,
        "tags": tags,
        "iso": str(iso),
        "disk": str(disk_src),
        "serial_log": str(log_path),
    }

    if not smoke_only:
        log_path.write_text(serial, encoding="utf-8")
        (out_dir / "boot-window.txt").write_text(boot_window, encoding="utf-8")
        (out_dir / "manifest.json").write_text(
            json.dumps(manifest, indent=2, sort_keys=True) + "\n",
            encoding="utf-8",
        )
        write_capture_md(out_dir, profile, arch, audit, tags, boot_window, passed)
        print(f"  OUT     {out_dir}")
    elif log_path.is_file():
        log_path.unlink(missing_ok=True)

    if passed:
        print(f"✓ init boot capture OK PROFILE={profile} init={spec['init_system']}")
    else:
        print(f"✗ init boot capture FAIL PROFILE={profile}", file=sys.stderr)
        if tags["missing_required"]:
            print(f"  missing tags: {', '.join(tags['missing_required'])}", file=sys.stderr)
        if not audit.get("ok", False):
            for item in audit.get("issues") or [audit.get("reason")]:
                if item:
                    print(f"  staged: {item}", file=sys.stderr)
        if result.returncode != 0:
            print(f"  smoke exit={result.returncode}", file=sys.stderr)

    return manifest


def main() -> int:
    parser = argparse.ArgumentParser(description="IR0 init boot capture harness")
    parser.add_argument("--profile", default=os.environ.get("ISD_PROFILE", "minimal"))
    parser.add_argument("--arch", default=os.environ.get("ISD_ARCH", "x86_64"))
    parser.add_argument("--matrix", action="store_true", help="run all contract profiles")
    parser.add_argument("--smoke-only", action="store_true", help="no artifact dir; exit code only")
    parser.add_argument("--timeout", type=int, default=90)
    parser.add_argument("--stale-sec", type=int, default=25)
    parser.add_argument("--out-root", type=Path, default=None)
    args = parser.parse_args()

    profiles = list(load_contract()["profiles"]) if args.matrix else [args.profile]
    failures = 0
    skipped = 0
    for profile in profiles:
        try:
            manifest = run_capture(
                profile,
                args.arch,
                smoke_only=args.smoke_only,
                timeout=args.timeout,
                stale_sec=args.stale_sec,
                out_root=args.out_root,
            )
            if not manifest["passed"]:
                failures += 1
        except InitBootSkip as exc:
            skipped += 1
            print(f"  SKIP  PROFILE={profile}: {exc}", file=sys.stderr)
        except InitBootError as exc:
            failures += 1
            print(f"✗ PROFILE={profile}: {exc}", file=sys.stderr)

    if failures:
        return 1
    if skipped and skipped == len(profiles):
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
