#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Host-side ISD / userspace inspector (usmang).

Inspects the sibling ISD tree without rebuilding packages. Kernel build #N and
ISD_VERSION are intentionally separate identities.
"""

from __future__ import annotations

import argparse
import curses
import json
import os
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent


def resolve_isd_root(explicit: Path | None = None) -> Path:
    if explicit is not None:
        return explicit.resolve()
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

USMANG_HELP_LINES = (
    "IR0 Userspace Manager (usmang) — host inspector for sibling ISD/",
    "",
    "Today: curses TUI (make usmang) or read-only CLI. Future: userland composition",
    "choices (BusyBox vs GNU coreutils, runit vs sysvinit vs systemd) without kernel changes.",
    "",
    "Commands:",
    "  summary     ISD version, userland base, package counts, desktop ABI",
    "  version     VERSION file + staged release metadata",
    "  packages    Resolved package list with origins",
    "  userland    USERLAND_BASE and implementation status",
    "  desktop     X client set for PROFILE=desktop only",
    "  verify      Postcondition check against staged rootfs (ISD-owned rules)",
    "              Optional: --packages pkg[,pkg...] checks only those entries;",
    "              logs each failure and continues (does not abort mid-run)",
    "  help        This legend",
    "  tui         Interactive curses inspector (default when make usmang on a TTY)",
    "",
    "Environment:",
    "  IR0_ISD_ROOT   Path to ISD checkout (default: ../ISD)",
    "  ISD_PROFILE    Profile name (default: desktop)",
    "  ISD_ARCH       Architecture (default: x86_64)",
    "",
    "Examples:",
    "  make usmang",
    "  python3 scripts/userspace_manager.py --isd-root ../ISD tui",
    "  python3 scripts/userspace_manager.py --profile minimal --json userland",
    "  python3 scripts/userspace_manager.py --profile minimal verify -p busybox -p runit",
    "",
    "See also: ISD/Documentation/LOGIN_SESSION.md (login one-shot, kmang-owned),",
    "Documentation/USERSPACE.md, make kmang (kernel ISO catalog).",
)


def format_usmang_help() -> str:
    return "\n".join(USMANG_HELP_LINES)


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


def profile_conf(isd: Path, profile: str) -> dict[str, str]:
    conf = isd / "profiles" / profile / "profile.conf"
    return parse_kv(read_text(conf))


def profile_userland(isd: Path, profile: str) -> str:
    return profile_conf(isd, profile).get("USERLAND_BASE", "busybox")


def profile_init_system(isd: Path, profile: str) -> str:
    init = profile_conf(isd, profile).get("INIT_SYSTEM", "runit")
    return init if init in ("runit", "sysvinit") else "runit"


def list_profiles(isd: Path) -> list[str]:
    root = isd / "profiles"
    if not root.is_dir():
        return []
    names = [
        path.name
        for path in sorted(root.iterdir())
        if path.is_dir() and (path / "profile.conf").is_file()
    ]
    return names


def staged_rootfs(isd: Path, profile: str, arch: str) -> Path:
    return isd / "out" / arch / "rootfs" / profile


def staged_ready(isd: Path, profile: str, arch: str) -> bool:
    tree = staged_rootfs(isd, profile, arch)
    return tree.is_dir() and (tree / "etc" / "ir0-profile").is_file()


def build_summary_lines(isd: Path, profile: str, arch: str) -> list[str]:
    ver = cmd_version(isd, profile, arch)
    land = cmd_userland(isd, profile)
    pkg = cmd_packages(isd, profile)
    lines = [
        f"ISD {ver.get('isd_version_file')}  profile={profile}  arch={arch}",
        f"userland={land['userland_base']}  init={land['init_system']}",
        f"packages={pkg['count']} "
        f"(first={pkg['first_party']}, third={pkg['third_party']})",
    ]
    if staged_ready(isd, profile, arch):
        lines.append(f"staged: {staged_rootfs(isd, profile, arch)}")
    else:
        lines.append("staged: (none — run make -C ISD rootfs-tree)")
    desktop = cmd_desktop(isd, profile)
    if desktop.get("applies"):
        clients = ", ".join(desktop["x_session_packages"][:6])
        extra = "…" if len(desktop["x_session_packages"]) > 6 else ""
        lines.append(f"desktop clients: {clients}{extra}")
    else:
        lines.append(f"desktop: n/a ({desktop.get('note') or 'non-desktop profile'})")
    return lines


def tui_help_lines() -> list[str]:
    return [
        "Keys:",
        "  Up/Down or j/k   move profile selection",
        "  Enter            set active profile",
        "  v                verify staged rootfs (ISD script)",
        "  s                refresh summary",
        "  h / ?            toggle this help",
        "  q                quit",
    ]


def tui_draw_summary(
    screen: curses.window,
    isd: Path,
    profiles: list[str],
    selected: int,
    active: str,
    arch: str,
    message: str,
    show_help: bool,
    help_scroll: int,
) -> int:
    height, width = screen.getmaxyx()
    screen.erase()
    if show_help:
        overlay = tui_help_lines()
        body_rows = max(1, height - 2)
        max_scroll = max(0, len(overlay) - body_rows)
        help_scroll = min(help_scroll, max_scroll)
        for row, line in enumerate(overlay[help_scroll : help_scroll + body_rows]):
            if row + 1 >= height:
                break
            screen.addstr(row + 1, 2, line[: max(0, width - 4)])
        screen.addstr(0, 2, "usmang help (q/Esc close)", curses.A_BOLD)
        return help_scroll

    screen.addstr(0, 2, "IR0 Userspace Manager (usmang)", curses.A_BOLD)
    screen.addstr(1, 2, f"ISD: {isd}"[: max(0, width - 4)])
    list_top = 3
    for index, name in enumerate(profiles):
        y = list_top + index
        if y >= height - 8:
            break
        marker = ">" if name == active else " "
        attr = curses.A_REVERSE if index == selected else 0
        init = profile_init_system(isd, name)
        label = f"{marker} {name:<20} init={init}"
        screen.addstr(y, 2, label[: max(0, width - 4)], attr)

    detail_top = list_top + min(len(profiles), max(1, height - 11))
    screen.addstr(detail_top, 2, f"Active profile: {active}", curses.A_BOLD)
    for offset, line in enumerate(build_summary_lines(isd, active, arch)):
        y = detail_top + 1 + offset
        if y >= height - 2:
            break
        screen.addstr(y, 4, line[: max(0, width - 6)])
    screen.addstr(height - 1, 2, message[: max(0, width - 4)])
    screen.refresh()
    return help_scroll


def width_msg(screen: curses.window) -> int:
    _height, width = screen.getmaxyx()
    return max(20, width - 4)


def tui(screen: curses.window, isd: Path, profile: str, arch: str) -> int:
    try:
        curses.curs_set(0)
    except curses.error:
        pass
    profiles = list_profiles(isd)
    if not profiles:
        print("✗ no ISD profiles found", file=sys.stderr)
        return 2
    selected = profiles.index(profile) if profile in profiles else 0
    active = profiles[selected]
    message = "h/? help  v verify  Enter apply profile  q quit"
    show_help = False
    help_scroll = 0
    while True:
        help_scroll = tui_draw_summary(
            screen, isd, profiles, selected, active, arch, message,
            show_help, help_scroll,
        )
        if show_help:
            key = screen.getch()
            if key in (ord("q"), ord("Q"), 27):
                show_help = False
            continue
        key = screen.getch()
        if key in (ord("q"), ord("Q")):
            return 0
        if key in (ord("h"), ord("?")):
            show_help = True
            help_scroll = 0
            continue
        if key in (curses.KEY_UP, ord("k")) and profiles:
            selected = (selected - 1) % len(profiles)
            continue
        if key in (curses.KEY_DOWN, ord("j")) and profiles:
            selected = (selected + 1) % len(profiles)
            continue
        if key in (10, 13, curses.KEY_ENTER) and profiles:
            active = profiles[selected]
            message = f"active profile → {active}"
            continue
        if key == ord("s"):
            message = "summary refreshed"
            continue
        if key == ord("v"):
            payload, code = cmd_verify(isd, active, arch, None)
            if payload.get("status") == "unknown":
                message = payload.get("reason", "verify skipped")
            elif code == 0:
                message = f"verify OK profile={active}"
            else:
                detail = payload.get("stderr") or payload.get("stdout") or "verify failed"
                message = detail.splitlines()[0][: width_msg(screen)]
            continue


def cmd_version(isd: Path, profile: str, arch: str) -> dict:
    version = read_text(isd / "VERSION").strip()
    staged = staged_rootfs(isd, profile, arch)
    release = parse_kv(read_text(staged / "usr/share/isd/release.txt"))
    os_release = parse_kv(read_text(staged / "etc/os-release"))
    build_info = parse_kv(read_text(staged / "usr/lib/ir0/build-info"))
    return {
        "isd_root": str(isd),
        "isd_version_file": version,
        "profile": profile,
        "arch": arch,
        "userland_base": profile_userland(isd, profile),
        "init_system": profile_init_system(isd, profile),
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
    init = profile_init_system(isd, profile)
    return {
        "userland_base": base,
        "init_system": init,
        "init_implemented": init in ("runit", "sysvinit"),
        "implemented": base == "busybox",
        "coreutils": "reserved — IR0 must grow Linux surface before selecting it",
        "busybox_matrix": str(isd / "packages/busybox/bb_status.tsv"),
        "note": "Userspace shapes IR0: do not patch BusyBox/coreutils to hide ABI gaps",
    }


# Interactive X session clients: metadata owned by ISD (profiles/desktop/x-session-clients.txt).
def x_session_clients(isd: Path) -> frozenset[str]:
    path = isd / "profiles" / "desktop" / "x-session-clients.txt"
    names: list[str] = []
    for raw in read_text(path).splitlines():
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        names.append(line)
    return frozenset(names)


def profile_packages(isd: Path, profile: str) -> list[str]:
    path = isd / "profiles" / profile / "packages.txt"
    names: list[str] = []
    for raw in read_text(path).splitlines():
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        names.append(line)
    return names


def cmd_desktop(isd: Path, profile: str) -> dict:
    packages = profile_packages(isd, profile)
    clients = x_session_clients(isd)
    x_clients = [name for name in packages if name in clients]
    overlay = isd / "profiles" / profile / "overlay"
    wallpaper_candidates = [
        overlay / "usr/share/backgrounds/ir0-desktop.xbm",
        overlay / "usr/share/backgrounds/ir0desk.xbm",
        isd / "profiles/desktop/overlay/usr/share/backgrounds/ir0-desktop.xbm",
    ]
    wallpaper = next((path for path in wallpaper_candidates if path.is_file()), None)
    abi_candidates = [
        isd / "Documentation/DESKTOP_ABI.md",
        ROOT / "Documentation/DESKTOP_ABI.md",
    ]
    abi_doc = next((str(path) for path in abi_candidates if path.is_file()), None)
    return {
        "profile": profile,
        "applies": profile == "desktop",
        "x_session_packages": x_clients,
        "x_session_missing_from_profile": sorted(clients - set(x_clients)),
        "wallpaper_xbm": str(wallpaper) if wallpaper else None,
        "abi_doc": abi_doc,
        "golden_rule": "unmodified upstream clients; IR0 supplies Linux surfaces",
        "note": (
            "Desktop ABI applies to PROFILE=desktop only; other profiles omit X clients."
            if profile != "desktop"
            else None
        ),
    }


def load_verify_manifest(isd: Path, profile: str) -> list[tuple[str, str]]:
    manifest = isd / "profiles" / profile / "verify-paths.txt"
    if not manifest.is_file():
        manifest = isd / "profiles" / "minimal" / "verify-paths.txt"
    entries: list[tuple[str, str]] = []
    for raw in read_text(manifest).splitlines():
        line = raw.split("#", 1)[0].strip()
        if not line:
            continue
        parts = line.split(None, 1)
        if len(parts) != 2:
            continue
        pkg, rel = parts[0], parts[1].strip()
        if rel:
            entries.append((pkg, rel))
    return entries


def parse_package_list(values: list[str] | None) -> list[str] | None:
    if not values:
        return None
    names: list[str] = []
    for item in values:
        for part in item.split(","):
            name = part.strip()
            if name:
                names.append(name)
    return names or None


def cmd_verify_selected(
    isd: Path,
    profile: str,
    arch: str,
    staged: Path,
    selected: list[str],
) -> tuple[dict, int]:
    manifest = load_verify_manifest(isd, profile)
    by_pkg: dict[str, list[str]] = {}
    for pkg, rel in manifest:
        by_pkg.setdefault(pkg, []).append(rel)

    unknown = sorted(set(selected) - set(by_pkg))
    for pkg in unknown:
        print(f"⚠ verify: unknown package {pkg!r} (no verify-paths entry)", file=sys.stderr)

    rows: list[dict[str, str]] = []
    failures = 0
    checked = 0
    for pkg in selected:
        paths = by_pkg.get(pkg)
        if not paths:
            rows.append({"package": pkg, "status": "unknown", "reason": "no verify-paths entry"})
            continue
        for rel in paths:
            checked += 1
            target = staged / rel
            if target.exists():
                rows.append({"package": pkg, "path": rel, "status": "ok"})
                print(f"  OK  {pkg}: {rel}")
            else:
                failures += 1
                rows.append({"package": pkg, "path": rel, "status": "missing"})
                print(f"⚠ verify {pkg}: missing {rel} in {staged}", file=sys.stderr)

    payload = {
        "status": "verified" if failures == 0 and not unknown else "partial",
        "profile": profile,
        "arch": arch,
        "staged_rootfs": str(staged),
        "selected_packages": selected,
        "checked_paths": checked,
        "failures": failures,
        "unknown_packages": unknown,
        "results": rows,
    }
    if failures or unknown:
        return payload, 1
    return payload, 0


def cmd_verify(
    isd: Path,
    profile: str,
    arch: str,
    selected: list[str] | None = None,
) -> tuple[dict, int]:
    staged = isd / "out" / arch / "rootfs" / profile
    if not staged.is_dir():
        return {
            "status": "unknown",
            "profile": profile,
            "arch": arch,
            "staged_rootfs": str(staged),
            "reason": "rootfs not staged — run make -C ISD rootfs-tree first",
        }, 3
    if selected:
        return cmd_verify_selected(isd, profile, arch, staged, selected)

    script = isd / "scripts" / "verify-profile-rootfs.sh"
    if not script.is_file():
        return {
            "status": "error",
            "reason": f"missing ISD verify script: {script}",
        }, 2
    result = subprocess.run(
        ["bash", str(script), str(staged)],
        cwd=isd,
        text=True,
        capture_output=True,
        check=False,
        env={**os.environ, "PROFILE": profile, "ARCH": arch},
    )
    payload = {
        "status": "verified" if result.returncode == 0 else "error",
        "profile": profile,
        "arch": arch,
        "staged_rootfs": str(staged),
        "stdout": result.stdout.strip(),
        "stderr": result.stderr.strip(),
    }
    return payload, 0 if result.returncode == 0 else 2


def main() -> int:
    parser = argparse.ArgumentParser(description="ISD userspace manager (host)")
    parser.add_argument(
        "--isd-root",
        type=Path,
        default=None,
    )
    parser.add_argument("--profile", default=os.environ.get("ISD_PROFILE", "desktop"))
    parser.add_argument("--arch", default=os.environ.get("ISD_ARCH", "x86_64"))
    parser.add_argument("--json", action="store_true")
    sub = parser.add_subparsers(dest="command", required=True)
    for name in ("version", "packages", "userland", "desktop", "summary", "help"):
        sub.add_parser(name)
    sub.add_parser("tui")
    verify_parser = sub.add_parser("verify")
    verify_parser.add_argument(
        "-p",
        "--packages",
        action="append",
        default=None,
        help="check only these packages (comma-separated ok); log failures and continue",
    )
    args = parser.parse_args()
    isd = resolve_isd_root(args.isd_root)
    if not (isd / "Makefile").is_file():
        print(f"✗ ISD not found at {isd}", file=sys.stderr)
        return 2

    if args.command == "version":
        payload = cmd_version(isd, args.profile, args.arch)
    elif args.command == "help":
        print(format_usmang_help())
        return 0
    elif args.command == "tui":
        if not sys.stdout.isatty() or not sys.stdin.isatty():
            print("✗ usmang tui requires a TTY (use summary/verify for CI)", file=sys.stderr)
            return 2
        return curses.wrapper(tui, isd, args.profile, args.arch)
    elif args.command == "packages":
        payload = cmd_packages(isd, args.profile)
    elif args.command == "userland":
        payload = cmd_userland(isd, args.profile)
    elif args.command == "desktop":
        payload = cmd_desktop(isd, args.profile)
    elif args.command == "verify":
        selected = parse_package_list(getattr(args, "packages", None))
        payload, code = cmd_verify(isd, args.profile, args.arch, selected)
        if args.json:
            print(json.dumps(payload, sort_keys=True, indent=2))
        else:
            if selected:
                print(
                    f"verify packages profile={args.profile} "
                    f"failures={payload.get('failures', 0)} "
                    f"unknown={len(payload.get('unknown_packages', []))}"
                )
            else:
                print(payload.get("stdout") or payload.get("reason") or payload.get("status"))
                if payload.get("stderr"):
                    print(payload["stderr"], file=sys.stderr)
        return code
    else:
        desktop = cmd_desktop(isd, args.profile)
        payload = {
            "version": cmd_version(isd, args.profile, args.arch),
            "userland": cmd_userland(isd, args.profile),
            "packages": {
                k: cmd_packages(isd, args.profile)[k]
                for k in ("count", "first_party", "third_party")
            },
        }
        if desktop["applies"]:
            payload["desktop"] = desktop
        else:
            payload["desktop"] = {
                "profile": args.profile,
                "applies": False,
                "note": desktop["note"],
            }

    if args.json:
        print(json.dumps(payload, sort_keys=True, indent=2))
    else:
        if args.command == "summary":
            ver = payload["version"]
            print(f"ISD {ver.get('isd_version_file')}  profile={args.profile}")
            print(f"  userland: {payload['userland']['userland_base']}")
            print(f"  init: {payload['userland']['init_system']}")
            print(
                f"  packages: {payload['packages']['count']} "
                f"(first-party={payload['packages']['first_party']}, "
                f"third-party={payload['packages']['third_party']})"
            )
            desktop = payload["desktop"]
            if desktop.get("applies"):
                print(
                    f"  desktop X clients ({len(desktop['x_session_packages'])}): "
                    f"{', '.join(desktop['x_session_packages'])}"
                )
                if desktop.get("wallpaper_xbm"):
                    print(f"  wallpaper: {desktop['wallpaper_xbm']}")
            else:
                print(f"  desktop: n/a (profile={args.profile}; use PROFILE=desktop)")
            print(f"  note: {ver.get('note')}")
        elif args.command == "packages":
            for row in payload["packages"]:
                print(f"{row['name']}\t{row['origin']}")
        else:
            print(json.dumps(payload, sort_keys=True, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
