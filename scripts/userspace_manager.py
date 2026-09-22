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
    "  boot-contract  Staged init audit + expected QEMU serial tags (read-only)",
    "  admin       Show ADMIN_ELEVATION (doas|sudo) for profile",
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


SUPPORTED_INIT_SYSTEMS = frozenset({"runit", "sysvinit", "openrc"})


class UnsupportedInitSystem(ValueError):
    """Profile asked for an init the ISD/IR0 adapter does not implement."""


def profile_init_system(isd: Path, profile: str) -> str:
    init = profile_conf(isd, profile).get("INIT_SYSTEM", "").strip()
    if not init:
        raise UnsupportedInitSystem(f"profile {profile}: missing INIT_SYSTEM")
    if init not in SUPPORTED_INIT_SYSTEMS:
        raise UnsupportedInitSystem(
            f"profile {profile}: unsupported INIT_SYSTEM={init}"
        )
    return init


def isdconfig_path(isd: Path, profile: str) -> Path:
    env = os.environ.get("ISD_CONFIG")
    if env:
        return Path(env)
    return isd / ".isdconfig.d" / profile


def profile_admin_elevation(isd: Path, profile: str) -> str:
    """Effective admin tool: profile.conf default, .isdconfig.d override."""
    tool = profile_conf(isd, profile).get("ADMIN_ELEVATION", "doas")
    cfg = parse_kv(read_text(isdconfig_path(isd, profile)))
    override = cfg.get("ADMIN_ELEVATION", "").strip().lower()
    if override in ("doas", "sudo"):
        tool = override
    pkg_sudo = cfg.get("CONFIG_PKG_SUDO", "n").lower() in ("y", "yes", "1")
    pkg_doas = cfg.get("CONFIG_PKG_OPENDOAS", "n").lower() in ("y", "yes", "1")
    if pkg_sudo:
        tool = "sudo"
    elif pkg_doas and override != "sudo":
        tool = "doas"
    return tool if tool in ("doas", "sudo") else "doas"


def admin_package_for(tool: str) -> str:
    return "sudo" if tool == "sudo" else "opendoas"


def profile_wants_admin(isd: Path, profile: str) -> bool:
    packages = profile_packages(isd, profile)
    if "opendoas" in packages or "sudo" in packages:
        return True
    conf = profile_conf(isd, profile)
    if conf.get("ADMIN_ELEVATION", "").lower() in ("doas", "sudo"):
        return True
    if conf.get("LOGIN_POLICY") == "firstboot":
        return True
    cfg = parse_kv(read_text(isdconfig_path(isd, profile)))
    for key in ("CONFIG_PKG_OPENDOAS", "CONFIG_PKG_SUDO"):
        if cfg.get(key, "n").lower() in ("y", "yes", "1"):
            return True
    return False


def write_admin_elevation(isd: Path, profile: str, tool: str) -> Path:
    """Persist ADMIN_ELEVATION to profile-local .isdconfig.d (gitignored)."""
    if tool not in ("doas", "sudo"):
        raise ValueError(f"invalid admin tool: {tool!r}")
    cfg_path = isdconfig_path(isd, profile)
    cfg_path.parent.mkdir(parents=True, exist_ok=True)
    lines: list[str] = []
    if cfg_path.is_file():
        for raw in read_text(cfg_path).splitlines():
            line = raw.strip()
            if not line or line.startswith("#"):
                lines.append(raw)
                continue
            key = line.split("=", 1)[0].strip()
            if key in ("ADMIN_ELEVATION", "CONFIG_PKG_OPENDOAS", "CONFIG_PKG_SUDO"):
                continue
            lines.append(raw)
    while lines and not lines[-1].strip():
        lines.pop()
    if lines and lines[-1].strip():
        lines.append("")
    lines.append(f"ADMIN_ELEVATION={tool}")
    pkg = "SUDO" if tool == "sudo" else "OPENDOAS"
    other = "OPENDOAS" if tool == "sudo" else "SUDO"
    if profile_wants_admin(isd, profile):
        lines.append(f"CONFIG_PKG_{pkg}=y")
        lines.append(f"CONFIG_PKG_{other}=n")
    cfg_path.write_text("\n".join(lines) + "\n", encoding="utf-8")
    return cfg_path


def toggle_admin_elevation(isd: Path, profile: str) -> tuple[str, Path]:
    current = profile_admin_elevation(isd, profile)
    new_tool = "sudo" if current == "doas" else "doas"
    return new_tool, write_admin_elevation(isd, profile, new_tool)


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
    try:
        ver = cmd_version(isd, profile, arch)
        land = cmd_userland(isd, profile)
    except UnsupportedInitSystem as exc:
        return [str(exc), "configuration invalid — fix INIT_SYSTEM"]
    pkg = cmd_packages(isd, profile)
    admin = profile_admin_elevation(isd, profile)
    lines = [
        f"ISD {ver.get('isd_version_file')}  profile={profile}  arch={arch}",
        f"userland={land['userland_base']}  init={land['init_system']}",
        f"admin={admin}  pkg={admin_package_for(admin)}",
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
        "  c                boot-contract audit (init + expected serial tags)",
        "  a                toggle admin tool (doas ↔ sudo)",
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
        try:
            init = profile_init_system(isd, name)
        except UnsupportedInitSystem:
            init = "INVALID"
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
    message = "h/? help  a admin  v verify  c boot-contract  Enter apply  q quit"
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
        if key == ord("a"):
            try:
                new_tool, cfg_path = toggle_admin_elevation(isd, active)
            except ValueError as exc:
                message = str(exc)[: width_msg(screen)]
                continue
            message = (
                f"admin → {new_tool} ({admin_package_for(new_tool)}); "
                f"wrote {cfg_path.name}; rebuild ISD rootfs"
            )
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
        if key == ord("c"):
            payload = cmd_boot_contract(isd, active, arch)
            if payload.get("status") == "ok":
                tags = payload.get("required_serial_tags") or []
                preview = ",".join(tags[:3])
                if len(tags) > 3:
                    preview += ",…"
                message = f"boot-contract OK tags={preview}"
            else:
                issues = payload.get("issues") or [payload.get("note", "mismatch")]
                message = str(issues[0])[: width_msg(screen)]
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
    admin = profile_admin_elevation(isd, profile)
    return {
        "userland_base": base,
        "init_system": init,
        "admin_elevation": admin,
        "admin_package": admin_package_for(admin),
        "init_implemented": init in ("runit", "sysvinit", "openrc"),
        "implemented": base == "busybox",
        "coreutils": "reserved — IR0 must grow Linux surface before selecting it",
        "busybox_matrix": str(isd / "packages/busybox/bb_status.tsv"),
        "note": "Userspace shapes IR0: do not patch BusyBox/coreutils to hide ABI gaps",
    }


def cmd_admin(isd: Path, profile: str) -> dict:
    tool = profile_admin_elevation(isd, profile)
    cfg = isdconfig_path(isd, profile)
    return {
        "profile": profile,
        "admin_elevation": tool,
        "admin_package": admin_package_for(tool),
        "profile_wants_admin": profile_wants_admin(isd, profile),
        "isdconfig": str(cfg),
        "persist": (
            "Toggle in usmang TUI (a) writes ADMIN_ELEVATION to .isdconfig.d/<profile>; "
            "rebuild ISD rootfs to apply."
        ),
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


def load_boot_contract() -> dict[str, object]:
    path = ROOT / "scripts" / "init_boot_contract.json"
    if not path.is_file():
        return {}
    return json.loads(path.read_text(encoding="utf-8"))


def cmd_boot_contract(isd: Path, profile: str, arch: str) -> dict[str, object]:
    contract = load_boot_contract()
    profiles = contract.get("profiles", {})
    spec = profiles.get(profile) if isinstance(profiles, dict) else None
    init = profile_init_system(isd, profile)
    staged = staged_rootfs(isd, profile, arch)
    tree_ok = staged_ready(isd, profile, arch)
    payload: dict[str, object] = {
        "profile": profile,
        "arch": arch,
        "init_system_profile": init,
        "staged_ready": tree_ok,
        "staged_rootfs": str(staged),
        "contract_defined": spec is not None,
    }
    if not spec or not isinstance(spec, dict):
        payload["status"] = "no_contract"
        payload["note"] = "profile not in init_boot_contract.json (boot capture N/A)"
        return payload

    payload["init_system_contract"] = spec.get("init_system")
    payload["required_serial_tags"] = spec.get("required_serial_tags", [])
    payload["boot_window"] = {
        "start": spec.get("boot_window_start"),
        "end": spec.get("boot_window_end"),
    }
    payload["future_systemd"] = (
        "systemd not in contract yet; add profile + staged audit before claiming support"
    )

    issues: list[str] = []
    if init != spec.get("init_system"):
        issues.append(f"profile INIT_SYSTEM={init} != contract {spec.get('init_system')}")

    if tree_ok:
        init_path = staged / "sbin/init"
        impl_rel = str(spec.get("staged_init_impl", "sbin/init"))
        impl_path = staged / impl_rel
        if not init_path.exists():
            issues.append("missing sbin/init in staged rootfs")
        if impl_rel != "sbin/init" and not impl_path.exists():
            issues.append(f"missing {impl_rel}")
        payload["init_path"] = str(init_path)
        payload["impl_path"] = str(impl_path)
    else:
        issues.append("rootfs not staged — run make -C ISD rootfs-tree or ensure-isd-disk")

    payload["issues"] = issues
    payload["status"] = "ok" if not issues else "mismatch"
    return payload


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
    for name in (
        "version", "packages", "userland", "desktop", "summary", "help",
        "boot-contract", "admin",
    ):
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
    elif args.command == "boot-contract":
        payload = cmd_boot_contract(isd, args.profile, args.arch)
        if args.json:
            print(json.dumps(payload, sort_keys=True, indent=2))
        else:
            print(
                f"boot-contract profile={args.profile} status={payload.get('status')} "
                f"init={payload.get('init_system_profile')}"
            )
            for item in payload.get("issues") or []:
                print(f"  issue: {item}", file=sys.stderr)
            tags = payload.get("required_serial_tags") or []
            if tags:
                print(f"  expected serial tags: {', '.join(tags)}")
            note = payload.get("note")
            if note:
                print(f"  note: {note}")
        return 0 if payload.get("status") == "ok" else 1
    elif args.command == "tui":
        if not sys.stdout.isatty() or not sys.stdin.isatty():
            print("✗ usmang tui requires a TTY (use summary/verify for CI)", file=sys.stderr)
            return 2
        return curses.wrapper(tui, isd, args.profile, args.arch)
    elif args.command == "packages":
        payload = cmd_packages(isd, args.profile)
    elif args.command == "admin":
        payload = cmd_admin(isd, args.profile)
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
                f"  admin: {payload['userland']['admin_elevation']} "
                f"({payload['userland']['admin_package']})"
            )
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
    try:
        raise SystemExit(main())
    except UnsupportedInitSystem as exc:
        print(f"usmang: {exc}", file=sys.stderr)
        raise SystemExit(2)
