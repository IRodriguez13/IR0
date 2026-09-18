#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Host-side ISD / userspace inspector (usmang).

Inspects the sibling ISD tree without rebuilding packages. Kernel build #N and
ISD_VERSION are intentionally separate identities.
"""

from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
from pathlib import Path


def read_text(path: Path) -> str:
    return path.read_text(encoding="utf-8", errors="replace") if path.is_file() else ""


def parse_kv(text: str) -> dict[str, str]:
    data: dict[str, str] = {}
    for raw in text.splitlines():
        line = raw.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue
        key, val = line.split("=", 1)
        data[key.strip()] = val.strip().strip('"')
    return data


def profile_userland(isd: Path, profile: str) -> str:
    conf = isd / "profiles" / profile / "profile.conf"
    for raw in read_text(conf).splitlines():
        if raw.startswith("USERLAND_BASE="):
            return raw.split("=", 1)[1].strip()
    return "busybox"


def cmd_version(isd: Path, profile: str, arch: str) -> dict:
    version = read_text(isd / "VERSION").strip()
    staged = isd / "out" / arch / "rootfs" / profile
    release = parse_kv(read_text(staged / "usr/share/isd/release.txt"))
    os_release = parse_kv(read_text(staged / "etc/os-release"))
    build_info = parse_kv(read_text(staged / "usr/lib/ir0/build-info"))
    return {
        "isd_root": str(isd),
        "isd_version_file": version,
        "profile": profile,
        "arch": arch,
        "userland_base": profile_userland(isd, profile),
        "staged_release": release or None,
        "staged_os_release": os_release or None,
        "staged_build_info": build_info or None,
        "note": "ISD_VERSION is independent of IR0 kernel local build #N",
    }


def cmd_packages(isd: Path, profile: str) -> dict:
    env = os.environ.copy()
    env["PROFILE"] = profile
    resolved = subprocess.run(
        ["bash", str(isd / "scripts/resolve-packages.sh")],
        cwd=isd,
        text=True,
        capture_output=True,
        check=False,
        env=env,
    )
    packages = resolved.stdout.split() if resolved.returncode == 0 else []
    origins_path = isd / "scripts/package-origins.txt"
    origins: dict[str, str] = {}
    for raw in read_text(origins_path).splitlines():
        if not raw or raw.startswith("#") or "\t" not in raw:
            continue
        name, origin = raw.split("\t", 1)
        origins[name.strip()] = origin.strip()
    rows = [
        {"name": name, "origin": origins.get(name, "third-party")}
        for name in packages
    ]
    first = sum(1 for row in rows if row["origin"].startswith("first-party"))
    third = len(rows) - first
    return {
        "profile": profile,
        "count": len(rows),
        "first_party": first,
        "third_party": third,
        "packages": rows,
    }


def cmd_userland(isd: Path, profile: str) -> dict:
    base = profile_userland(isd, profile)
    return {
        "userland_base": base,
        "implemented": base == "busybox",
        "coreutils": "reserved — IR0 must grow Linux surface before selecting it",
        "busybox_matrix": str(isd / "packages/busybox/bb_status.tsv"),
        "note": "Userspace shapes IR0: do not patch BusyBox/coreutils to hide ABI gaps",
    }


def cmd_desktop(isd: Path) -> dict:
    packages = read_text(isd / "profiles/desktop/packages.txt").splitlines()
    x_clients = [
        line.strip()
        for line in packages
        if line.strip()
        and not line.strip().startswith("#")
        and line.strip()
        in {
            "tinyx",
            "twm",
            "xterm",
            "xclock",
            "xeyes",
            "xlogo",
            "xcalc",
            "xmessage",
            "xsetroot",
            "xload",
            "xinit",
            "xauth",
        }
    ]
    wallpaper = isd / "profiles/desktop/overlay/usr/share/backgrounds/ir0-desktop.xbm"
    return {
        "x_session_packages": x_clients,
        "wallpaper_xbm": str(wallpaper) if wallpaper.is_file() else None,
        "abi_doc": str(isd / "Documentation/DESKTOP_ABI.md"),
        "golden_rule": "unmodified upstream clients; IR0 supplies Linux surfaces",
    }


def main() -> int:
    parser = argparse.ArgumentParser(description="ISD userspace manager (host)")
    parser.add_argument(
        "--isd-root",
        type=Path,
        default=Path(os.environ.get("IR0_ISD_ROOT", Path.cwd().parent / "ISD")),
    )
    parser.add_argument("--profile", default=os.environ.get("ISD_PROFILE", "desktop"))
    parser.add_argument("--arch", default=os.environ.get("ISD_ARCH", "x86_64"))
    parser.add_argument("--json", action="store_true")
    sub = parser.add_subparsers(dest="command", required=True)
    for name in ("version", "packages", "userland", "desktop", "summary"):
        sub.add_parser(name)
    args = parser.parse_args()
    isd = args.isd_root.resolve()
    if not (isd / "Makefile").is_file():
        print(f"✗ ISD not found at {isd}", file=sys.stderr)
        return 2

    if args.command == "version":
        payload = cmd_version(isd, args.profile, args.arch)
    elif args.command == "packages":
        payload = cmd_packages(isd, args.profile)
    elif args.command == "userland":
        payload = cmd_userland(isd, args.profile)
    elif args.command == "desktop":
        payload = cmd_desktop(isd)
    else:
        payload = {
            "version": cmd_version(isd, args.profile, args.arch),
            "userland": cmd_userland(isd, args.profile),
            "desktop": cmd_desktop(isd),
            "packages": {
                k: cmd_packages(isd, args.profile)[k]
                for k in ("count", "first_party", "third_party")
            },
        }

    if args.json:
        print(json.dumps(payload, sort_keys=True, indent=2))
    else:
        if args.command == "summary":
            ver = payload["version"]
            print(f"ISD {ver.get('isd_version_file')}  profile={args.profile}")
            print(f"  userland: {payload['userland']['userland_base']}")
            print(
                f"  packages: {payload['packages']['count']} "
                f"(first-party={payload['packages']['first_party']}, "
                f"third-party={payload['packages']['third_party']})"
            )
            print(f"  desktop X pkgs: {', '.join(payload['desktop']['x_session_packages'])}")
            print(f"  note: {ver.get('note')}")
        elif args.command == "packages":
            for row in payload["packages"]:
                print(f"{row['name']}\t{row['origin']}")
        else:
            print(json.dumps(payload, sort_keys=True, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
