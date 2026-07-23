#!/usr/bin/env python3
"""
IR0 Kernel menuconfig — curses-based configuration UI.

Parses setup/Kconfig, shows an interactive terminal menu, and writes:
  .config                         — Makefile-includable CONFIG_*=y/n/value
  includes/generated/autoconf.h   — #define CONFIG_* for C source
"""

import curses
import os
import sys
import re
import subprocess

KERNEL_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
KCONFIG_PATH = os.path.join(KERNEL_ROOT, "setup", "Kconfig")
DOT_CONFIG   = os.path.join(KERNEL_ROOT, ".config")
DEFCONFIG_PATH = os.path.join(KERNEL_ROOT, "setup", "defconfig")
PROFILE_CONFIGS_DIR = os.path.join(KERNEL_ROOT, "setup", "configs")
AUTOCONF_DIR = os.path.join(KERNEL_ROOT, "includes", "generated")
AUTOCONF_H   = os.path.join(AUTOCONF_DIR, "autoconf.h")
VALID_UI_LANGS = ("en", "es")
ARCH_SYMBOLS = ("ARCH_X86_64", "ARCH_ARM64")
BOARD_SYMBOLS = ("ARM64_BOARD_QEMU_VIRT", "ARM64_BOARD_RPI4", "ARM64_BOARD_RPI5")

I18N = {
    "en": {
        "title": " IR0 Kernel Configuration ",
        "footer": " Space toggle  Enter edit  S save  D defaults  G/T/I/C/W presets  M mandocs  ' or H presets  ? symbol  L lang  Q quit ",
        "modified": " [MODIFIED] ",
        "help_title": " Symbol ",
        "preset_help_title": " Build presets ",
        "type": "Type",
        "default": "Default",
        "depends": "Depends on",
        "range": "Range",
        "saved": "Configuration saved to .config",
        "preset_generic": "Applied preset: generic (desktop x86)",
        "preset_tiny": "Applied preset: tiny",
        "preset_iot": "Applied preset: hub / IoT (ARM64 rpi4)",
        "preset_watch": "Applied preset: watch (ARM64 rpi5 stub)",
        "preset_custom": "Applied preset: defaults (customize freely)",
        "save_before_exit": "Save before exit?",
        "lang_switched": "UI language switched to English",
        "symbol": "Symbol",
        "mandocs_ok": "Mandocs build finished",
        "mandocs_fail": "Mandocs build failed — see terminal",
        "mandocs_prompt": "Mandocs language (en/es/all)",
    },
    "es": {
        "title": " Configuracion del Kernel IR0 ",
        "footer": " Espacio toggle  Enter editar  S guardar  D defecto  G/T/I/C/W presets  M mandocs  ' o H presets  ? simbolo  L idioma  Q salir ",
        "modified": " [MODIFICADO] ",
        "help_title": " Opcion ",
        "preset_help_title": " Perfiles de compilacion ",
        "type": "Tipo",
        "default": "Valor por defecto",
        "depends": "Depende de",
        "range": "Rango",
        "saved": "Configuracion guardada en .config",
        "preset_generic": "Preset aplicado: generic (desktop x86)",
        "preset_tiny": "Preset aplicado: tiny",
        "preset_iot": "Preset aplicado: hub / IoT (ARM64 rpi4)",
        "preset_watch": "Preset aplicado: watch (ARM64 rpi5 stub)",
        "preset_custom": "Preset aplicado: valores por defecto (editar)",
        "save_before_exit": "Guardar antes de salir?",
        "lang_switched": "Idioma de interfaz cambiado a Espanol",
        "symbol": "Simbolo",
        "mandocs_ok": "Mandocs generados",
        "mandocs_fail": "Fallo al generar mandocs",
        "mandocs_prompt": "Idioma mandocs (en/es/all)",
    },
}

PRESET_HELP_TEXT = {
    "en": (
        "Generic / desktop — full QEMU/PC profile (IPv4 stack, ATA, TMPFS/minix).\n"
        "Tiny — minimal footprint: ATA + filesystem bins, networking & BT off.\n"
        "IoT / hub (I) — AArch64 + ARM64_BOARD_RPI4; appliance UART path.\n"
        "Watch (W) — AArch64 + ARM64_BOARD_RPI5 stub (uart=none).\n"
        "Custom — every visible symbol reset to effective Kconfig default; edit manually.\n"
        "M — generate mandocs (en/es/all) via scripts/build_mandocs.py --yes.\n"
        "New Kconfig symbols are merged on load (menu always tracks setup/Kconfig).\n"
        "Makefile must honor CONFIG_* for true compile-outs; run build + guards after changes.\n"
    ),
    "es": (
        "Generic / desktop — perfil PC/QEMU completo (IPv4, ATA, TMPFS/minix).\n"
        "Tiny — mínimo: ATA + binarios de filesystem, sin red ni bluetooth.\n"
        "IoT / hub (I) — AArch64 + ARM64_BOARD_RPI4; ruta UART appliance.\n"
        "Watch (W) — AArch64 + ARM64_BOARD_RPI5 stub (uart=none).\n"
        "Custom — cada símbolo vuelve al default efectivo para editarlo a mano.\n"
        "M — generar mandocs (en/es/all) con scripts/build_mandocs.py --yes.\n"
        "Los símbolos nuevos de Kconfig se fusionan al cargar (el menú sigue a setup/Kconfig).\n"
        "El Makefile debe respetar CONFIG_* ; valida build tras cambios.\n"
    ),
}


def tr(lang, key):
    lang_key = lang if lang in I18N else "en"
    return I18N[lang_key].get(key, I18N["en"].get(key, key))

# ---------------------------------------------------------------------------
# Kconfig parser (minimal subset)
# ---------------------------------------------------------------------------

class ConfigSymbol:
    """One config SYMBOL entry."""
    def __init__(self, name):
        self.name   = name
        self.prompt = name
        self.type   = "bool"      # bool | int | string
        self.default = None
        self.value   = None       # current value (set later)
        self.depends = None       # symbol name or None
        self.range_lo = None
        self.range_hi = None
        self.help_text = ""
        self.menu   = ""          # parent menu title

    def effective_default(self):
        if self.type == "bool":
            return self.default if self.default in ("y", "n") else "y"
        if self.type == "int":
            try:
                return str(int(self.default))
            except (TypeError, ValueError):
                return "0"
        return self.default or ""


def normalize_string_value(val):
    if val is None:
        return ""
    sval = val.strip()
    if len(sval) >= 2 and sval[0] == '"' and sval[-1] == '"':
        return sval[1:-1]
    return sval


def parse_kconfig(path):
    """Return a list of ConfigSymbol from a Kconfig file."""
    symbols = []
    current = None
    menu_stack = []
    in_help = False

    with open(path) as f:
        lines = f.readlines()

    for raw in lines:
        line = raw.rstrip("\n")
        stripped = line.strip()

        if in_help:
            if stripped == "" and current and current.help_text:
                in_help = False
                continue
            if line.startswith("        ") or line.startswith("\t\t"):
                if current:
                    current.help_text += stripped + "\n"
                continue
            elif stripped and not line[0].isspace():
                in_help = False
            elif stripped:
                if current:
                    current.help_text += stripped + "\n"
                continue
            else:
                continue

        if stripped.startswith("menu "):
            title = stripped[5:].strip().strip('"')
            menu_stack.append(title)
            continue
        if stripped == "endmenu":
            if menu_stack:
                menu_stack.pop()
            continue
        if stripped.startswith("if ") or stripped == "endif":
            continue

        m = re.match(r"config\s+(\w+)", stripped)
        if m:
            current = ConfigSymbol(m.group(1))
            current.menu = menu_stack[-1] if menu_stack else ""
            symbols.append(current)
            continue

        if current is None:
            continue

        if stripped.startswith("bool"):
            current.type = "bool"
            m2 = re.search(r'"([^"]*)"', stripped)
            if m2:
                current.prompt = m2.group(1)
        elif stripped.startswith("int"):
            current.type = "int"
            m2 = re.search(r'"([^"]*)"', stripped)
            if m2:
                current.prompt = m2.group(1)
        elif stripped.startswith("string"):
            current.type = "string"
            m2 = re.search(r'"([^"]*)"', stripped)
            if m2:
                current.prompt = m2.group(1)
        elif stripped.startswith("default"):
            val = stripped.split(None, 1)[1] if len(stripped.split(None, 1)) > 1 else ""
            if current.type == "string":
                current.default = normalize_string_value(val)
            else:
                current.default = val.strip()
        elif stripped.startswith("depends on"):
            dep_expr = stripped.replace("depends on", "").strip()
            if current.depends:
                current.depends = f"({current.depends}) && ({dep_expr})"
            else:
                current.depends = dep_expr
        elif stripped.startswith("range"):
            parts = stripped.split()
            if len(parts) >= 3:
                current.range_lo = int(parts[1])
                current.range_hi = int(parts[2])
        elif stripped == "help":
            in_help = True

    return symbols


def load_dotconfig(symbols):
    """Load existing .config into symbol values."""
    vals = {}
    if os.path.isfile(DOT_CONFIG):
        with open(DOT_CONFIG) as f:
            for line in f:
                line = line.strip()
                if line.startswith("#") or "=" not in line:
                    continue
                k, v = line.split("=", 1)
                vals[k.strip()] = v.strip()
    for s in symbols:
        key = "CONFIG_" + s.name
        if key in vals:
            if s.type == "string":
                s.value = normalize_string_value(vals[key])
            else:
                s.value = vals[key]
        else:
            s.value = s.effective_default()


def save_config(symbols):
    """Write .config and includes/generated/autoconf.h."""
    with open(DOT_CONFIG, "w") as f:
        f.write("#\n# IR0 Kernel Configuration\n# Generated by menuconfig\n#\n\n")
        for s in symbols:
            key = "CONFIG_" + s.name
            if s.type == "bool":
                if s.value == "y":
                    f.write(f"{key}=y\n")
                else:
                    f.write(f"{key}=n\n")
            else:
                f.write(f"{key}={s.value}\n")

    os.makedirs(AUTOCONF_DIR, exist_ok=True)
    with open(AUTOCONF_H, "w") as f:
        f.write("/*\n * Automatically generated by menuconfig — do not edit.\n */\n")
        f.write("#ifndef _IR0_AUTOCONF_H\n#define _IR0_AUTOCONF_H\n\n")
        for s in symbols:
            macro = "CONFIG_" + s.name
            if s.type == "bool":
                if s.value == "y":
                    f.write(f"#define {macro} 1\n")
                else:
                    f.write(f"#define {macro} 0\n")
            elif s.type == "int":
                f.write(f"#define {macro} {s.value}\n")
            else:
                f.write(f'#define {macro} "{s.value}"\n')
        f.write("\n#endif /* _IR0_AUTOCONF_H */\n")


def read_config_vals(path):
    """Parse CONFIG_*=value lines from a defconfig/.config file."""
    vals = {}
    if not path or not os.path.isfile(path):
        return vals
    with open(path) as f:
        for line in f:
            line = line.strip()
            if line.startswith("#") or "=" not in line:
                continue
            k, v = line.split("=", 1)
            vals[k.strip()] = v.strip()
    return vals


def apply_vals_to_symbols(symbols, vals):
    """Apply CONFIG_* map; missing symbols keep/get effective defaults. Return added names."""
    added = []
    for s in symbols:
        key = "CONFIG_" + s.name
        if key in vals:
            if s.type == "string":
                s.value = normalize_string_value(vals[key])
            else:
                s.value = vals[key]
        else:
            s.value = s.effective_default()
            added.append(s.name)
    known = {"CONFIG_" + s.name for s in symbols}
    orphans = sorted(k[7:] for k in vals if k.startswith("CONFIG_") and k not in known)
    return added, orphans


def write_config_file(symbols, path, header_lines=None):
    """Write CONFIG_* file only (no autoconf.h)."""
    with open(path, "w") as f:
        if header_lines:
            for hl in header_lines:
                f.write(hl if hl.endswith("\n") else hl + "\n")
            if header_lines and header_lines[-1].strip() != "":
                f.write("\n")
        else:
            f.write("#\n# IR0 Kernel Configuration\n# Generated by menuconfig sync\n#\n\n")
        for s in symbols:
            key = "CONFIG_" + s.name
            if s.type == "bool":
                f.write(f"{key}={'y' if s.value == 'y' else 'n'}\n")
            elif s.type == "string":
                f.write(f"{key}={s.value}\n")
            else:
                f.write(f"{key}={s.value}\n")


def set_board_selection(symbols, selected_name):
    """Exclusive ARM64 board choice (like ARCH_*)."""
    changed = False
    for sym in symbols:
        if sym.name in BOARD_SYMBOLS:
            desired = "y" if sym.name == selected_name else "n"
            if sym.value != desired:
                sym.value = desired
                changed = True
    return changed


def sync_capabilities(defconfig_only=False, dry_run=False):
    """
    Merge Kconfig symbols into .config and/or setup/defconfig (+ profile configs).
    Returns exit code: 0 if clean / written OK; 1 if dry-run would change something.
    """
    targets = []
    if defconfig_only:
        targets.append(DEFCONFIG_PATH)
        if os.path.isdir(PROFILE_CONFIGS_DIR):
            for name in sorted(os.listdir(PROFILE_CONFIGS_DIR)):
                if name.endswith(".defconfig"):
                    targets.append(os.path.join(PROFILE_CONFIGS_DIR, name))
    else:
        targets.append(DOT_CONFIG)

    any_change = False
    for path in targets:
        symbols = parse_kconfig(KCONFIG_PATH)
        vals = read_config_vals(path)
        header = None
        if path != DOT_CONFIG and os.path.isfile(path):
            with open(path) as f:
                lines = f.readlines()
            hdr = []
            for line in lines:
                if line.startswith("#") or line.strip() == "":
                    hdr.append(line)
                else:
                    break
            if hdr:
                header = hdr
        added, orphans = apply_vals_to_symbols(symbols, vals)
        enforce_dependencies(symbols)
        if added or orphans:
            any_change = True
            rel = os.path.relpath(path, KERNEL_ROOT)
            print(f"{rel}: +{len(added)} new symbol(s), -{len(orphans)} orphan(s)")
            if added:
                print("  add:", ", ".join(added[:20]) + ("…" if len(added) > 20 else ""))
            if orphans:
                print("  drop:", ", ".join(orphans[:20]) + ("…" if len(orphans) > 20 else ""))
        if dry_run:
            continue
        if path == DOT_CONFIG:
            save_config(symbols)
        else:
            write_config_file(symbols, path, header_lines=header)

    if dry_run:
        if any_change:
            print("sync-capabilities: changes needed")
            return 1
        print("sync-capabilities: up to date")
        return 0

    if not defconfig_only:
        print("Synced .config and includes/generated/autoconf.h")
    else:
        print("Synced setup/defconfig and setup/configs/*.defconfig")
    return 0


def run_mandocs_batch(lang):
    """Invoke build_mandocs.py --yes (noninteractive)."""
    script = os.path.join(KERNEL_ROOT, "scripts", "build_mandocs.py")
    langs = ("en", "es") if lang == "all" else (lang,)
    rc = 0
    for L in langs:
        cmd = [sys.executable, script, "--lang", L, "--yes", "--mandoc-only"]
        print(" ".join(cmd), flush=True)
        r = subprocess.call(cmd, cwd=KERNEL_ROOT)
        if r != 0:
            rc = r
    return rc


# ---------------------------------------------------------------------------
# Dependency helpers
# ---------------------------------------------------------------------------

def sym_lookup(symbols, name):
    for s in symbols:
        if s.name == name:
            return s
    return None

def dep_satisfied(symbols, sym):
    if sym.depends is None:
        return True
    expr = sym.depends
    expr = expr.replace("&&", " and ").replace("||", " or ").replace("!", " not ")

    def _repl(match):
        token = match.group(0)
        dep = sym_lookup(symbols, token)
        if dep is None:
            return "True"
        return "True" if dep.value == "y" else "False"

    expr = re.sub(r"\b[A-Z0-9_]+\b", _repl, expr)
    try:
        return bool(eval(expr, {"__builtins__": {}}, {}))
    except Exception:
        return True


def set_arch_selection(symbols, selected_name):
    changed = False
    for sym in symbols:
        if sym.name in ARCH_SYMBOLS:
            desired = "y" if sym.name == selected_name else "n"
            if sym.value != desired:
                sym.value = desired
                changed = True
    return changed


def enforce_arch_choice(symbols):
    arch_syms = [sym for sym in symbols if sym.name in ARCH_SYMBOLS]
    if not arch_syms:
        return False

    changed = False
    selected = [sym for sym in arch_syms if sym.value == "y"]
    if len(selected) == 1:
        return False

    preferred = "ARCH_X86_64"
    if len(selected) > 1:
        preferred = selected[0].name

    changed |= set_arch_selection(symbols, preferred)
    return changed


def sanitize_ui_lang(lang_value):
    if not lang_value:
        return "en"
    lang = str(lang_value).strip().lower()
    if lang not in VALID_UI_LANGS:
        return "en"
    return lang


def get_ui_lang(symbols):
    sym = sym_lookup(symbols, "TOOL_MENUCONFIG_LANG")
    if not sym:
        return "en"
    return sanitize_ui_lang(sym.value)


def set_ui_lang(symbols, lang):
    sym = sym_lookup(symbols, "TOOL_MENUCONFIG_LANG")
    if sym:
        sym.value = sanitize_ui_lang(lang)

def enforce_dependencies(symbols):
    changed = False
    changed |= enforce_arch_choice(symbols)
    for sym in symbols:
        if sym.type == "bool" and sym.value == "y" and not dep_satisfied(symbols, sym):
            sym.value = "n"
            changed = True
    return changed

def set_symbol_value(symbols, assignment):
    if "=" not in assignment:
        raise ValueError(f"Invalid --set assignment: {assignment}")
    key, raw_val = assignment.split("=", 1)
    key = key.strip()
    raw_val = raw_val.strip()
    if key.startswith("CONFIG_"):
        key = key[len("CONFIG_"):]
    sym = sym_lookup(symbols, key)
    if sym is None:
        raise ValueError(f"Unknown symbol in --set: {key}")

    if sym.type == "bool":
        if raw_val not in ("y", "n"):
            raise ValueError(f"Boolean symbol {key} requires y/n")
        if key in ARCH_SYMBOLS and raw_val == "y":
            set_arch_selection(symbols, key)
        elif key in BOARD_SYMBOLS and raw_val == "y":
            set_board_selection(symbols, key)
        else:
            sym.value = raw_val
    elif sym.type == "int":
        ival = int(raw_val, 10)
        if sym.range_lo is not None:
            ival = max(sym.range_lo, min(sym.range_hi, ival))
        sym.value = str(ival)
    else:
        sym.value = normalize_string_value(raw_val)
        if key == "TOOL_MENUCONFIG_LANG":
            sym.value = sanitize_ui_lang(sym.value)


def apply_preset(symbols, preset):
    """Apply a predefined config profile."""
    preset_key = preset.strip().lower()
    if preset_key == "desktop":
        preset_key = "generic"
    if preset_key == "hub":
        preset_key = "iot"

    if preset_key == "custom":
        for sym in symbols:
            sym.value = sym.effective_default()
        enforce_dependencies(symbols)
        return

    values = {}
    for sym in symbols:
        sym.value = sym.effective_default()
    if preset_key == "generic":
        values = {
            "KERNEL_DEBUG_SHELL": "y",
            "ARCH_X86_64": "y",
            "ARCH_ARM64": "n",
            "TICK_RATE_HZ": "1000",
            "SCHEDULER_POLICY": "0",
            "ROOT_BLOCK_DEVICE": "hda",
            "ROOT_FILESYSTEM": "minix",
            "ENABLE_SMP": "n",
            "DEBUG_BINS_GROUP_CORE": "y",
            "DEBUG_BINS_GROUP_FS": "y",
            "DEBUG_BINS_GROUP_TEXT": "y",
            "DEBUG_BINS_GROUP_IDENTITY": "y",
            "DEBUG_BINS_GROUP_DIAG": "y",
            "DEBUG_BINS_GROUP_NET": "y",
            "DEBUG_BINS_GROUP_BT": "y",
            "ENABLE_NETWORKING": "y",
            "DRV_NIC_RTL8139": "y",
            "DRV_NIC_E1000": "n",
            "ENABLE_SOUND": "y",
            "ENABLE_BLUETOOTH": "y",
            "ENABLE_MOUSE": "y",
            "ENABLE_STORAGE_ATA": "y",
            "ENABLE_STORAGE_ATA_BLOCK": "y",
            "ENABLE_FS_MINIX": "y",
            "ENABLE_FS_TMPFS": "y",
            "ENABLE_FS_SIMPLEFS": "y",
            "ENABLE_FS_FAT16": "y",
            "ENABLE_PC_SPEAKER": "y",
            "ENABLE_VBE": "y",
            "ENABLE_EXAMPLE_DRIVERS": "n",
            "INIT_PS2_CONTROLLER": "y",
            "INIT_PC_SPEAKER": "y",
            "INIT_STORAGE_ATA": "y",
            "INIT_STORAGE_ATA_BLOCK": "y",
            "INIT_SOUND_DRIVERS": "y",
            "INIT_MOUSE_DRIVER": "y",
            "INIT_NETWORK_STACK": "y",
            "INIT_BLUETOOTH_DRIVER": "y",
            "TOOL_DEFAULT_DISK_FS": "0",
            "TOOL_DEFAULT_DISK_SIZE_MB": "200",
            "TOOL_AUTO_RUN_DEPTEST": "n",
            "TOOL_MENUCONFIG_LANG": "en",
        }
    elif preset_key == "tiny":
        values = {
            "KERNEL_DEBUG_SHELL": "y",
            "ARCH_X86_64": "y",
            "ARCH_ARM64": "n",
            "TICK_RATE_HZ": "250",
            "SCHEDULER_POLICY": "0",
            "ROOT_BLOCK_DEVICE": "hda",
            "ROOT_FILESYSTEM": "minix",
            "ENABLE_SMP": "n",
            "DEBUG_BINS_GROUP_CORE": "y",
            "DEBUG_BINS_GROUP_FS": "y",
            "DEBUG_BINS_GROUP_TEXT": "y",
            "DEBUG_BINS_GROUP_IDENTITY": "y",
            "DEBUG_BINS_GROUP_DIAG": "y",
            "DEBUG_BINS_GROUP_NET": "n",
            "DEBUG_BINS_GROUP_BT": "n",
            "ENABLE_NETWORKING": "n",
            "DRV_NIC_RTL8139": "n",
            "DRV_NIC_E1000": "n",
            "ENABLE_SOUND": "n",
            "ENABLE_BLUETOOTH": "n",
            "ENABLE_MOUSE": "n",
            "ENABLE_STORAGE_ATA": "y",
            "ENABLE_STORAGE_ATA_BLOCK": "y",
            "ENABLE_FS_MINIX": "y",
            "ENABLE_FS_TMPFS": "y",
            "ENABLE_FS_SIMPLEFS": "y",
            "ENABLE_FS_FAT16": "y",
            "ENABLE_PC_SPEAKER": "n",
            "ENABLE_VBE": "n",
            "ENABLE_EXAMPLE_DRIVERS": "n",
            "INIT_PS2_CONTROLLER": "y",
            "INIT_PC_SPEAKER": "n",
            "INIT_STORAGE_ATA": "y",
            "INIT_STORAGE_ATA_BLOCK": "y",
            "INIT_SOUND_DRIVERS": "n",
            "INIT_MOUSE_DRIVER": "n",
            "INIT_NETWORK_STACK": "n",
            "INIT_BLUETOOTH_DRIVER": "n",
            "ENABLE_USB_HOST": "n",
            "INIT_USB_HOST": "n",
            "TOOL_MENUCONFIG_LANG": "en",
        }
    elif preset_key == "iot":
        values = {
            "KERNEL_DEBUG_SHELL": "y",
            "ARCH_X86_64": "n",
            "ARCH_ARM64": "y",
            "TICK_RATE_HZ": "500",
            "SCHEDULER_POLICY": "0",
            "ROOT_BLOCK_DEVICE": "hda",
            "ROOT_FILESYSTEM": "tmpfs",
            "ENABLE_SMP": "n",
            "DEBUG_BINS_GROUP_CORE": "y",
            "DEBUG_BINS_GROUP_FS": "y",
            "DEBUG_BINS_GROUP_TEXT": "y",
            "DEBUG_BINS_GROUP_IDENTITY": "y",
            "DEBUG_BINS_GROUP_DIAG": "y",
            "DEBUG_BINS_GROUP_NET": "y",
            "DEBUG_BINS_GROUP_BT": "y",
            "ENABLE_NETWORKING": "y",
            "DRV_NIC_RTL8139": "n",
            "DRV_NIC_E1000": "n",
            "ENABLE_SOUND": "n",
            "ENABLE_BLUETOOTH": "y",
            "ENABLE_MOUSE": "n",
            "ENABLE_STORAGE_ATA": "y",
            "ENABLE_STORAGE_ATA_BLOCK": "y",
            "ENABLE_FS_MINIX": "y",
            "ENABLE_FS_TMPFS": "y",
            "ENABLE_FS_SIMPLEFS": "y",
            "ENABLE_FS_FAT16": "n",
            "ENABLE_PC_SPEAKER": "n",
            "ENABLE_VBE": "n",
            "ENABLE_EXAMPLE_DRIVERS": "n",
            "INIT_PS2_CONTROLLER": "y",
            "INIT_PC_SPEAKER": "n",
            "INIT_STORAGE_ATA": "y",
            "INIT_STORAGE_ATA_BLOCK": "y",
            "INIT_SOUND_DRIVERS": "n",
            "INIT_MOUSE_DRIVER": "n",
            "INIT_NETWORK_STACK": "y",
            "INIT_BLUETOOTH_DRIVER": "y",
            "ENABLE_USB_HOST": "y",
            "INIT_USB_HOST": "y",
            "TOOL_MENUCONFIG_LANG": "en",
            "ARM64_BOARD_QEMU_VIRT": "n",
            "ARM64_BOARD_RPI4": "y",
            "ARM64_BOARD_RPI5": "n",
        }
    elif preset_key == "watch":
        values = {
            "KERNEL_DEBUG_SHELL": "y",
            "ARCH_X86_64": "n",
            "ARCH_ARM64": "y",
            "TICK_RATE_HZ": "250",
            "SCHEDULER_POLICY": "0",
            "ROOT_BLOCK_DEVICE": "hda",
            "ROOT_FILESYSTEM": "tmpfs",
            "ENABLE_SMP": "n",
            "DEBUG_BINS_GROUP_CORE": "y",
            "DEBUG_BINS_GROUP_FS": "n",
            "DEBUG_BINS_GROUP_TEXT": "n",
            "DEBUG_BINS_GROUP_IDENTITY": "n",
            "DEBUG_BINS_GROUP_DIAG": "y",
            "DEBUG_BINS_GROUP_NET": "n",
            "DEBUG_BINS_GROUP_BT": "y",
            "ENABLE_NETWORKING": "n",
            "DRV_NIC_RTL8139": "n",
            "DRV_NIC_E1000": "n",
            "ENABLE_SOUND": "n",
            "ENABLE_BLUETOOTH": "y",
            "ENABLE_MOUSE": "n",
            "ENABLE_STORAGE_ATA": "n",
            "ENABLE_STORAGE_ATA_BLOCK": "n",
            "ENABLE_FS_MINIX": "n",
            "ENABLE_FS_TMPFS": "y",
            "ENABLE_FS_SIMPLEFS": "n",
            "ENABLE_FS_FAT16": "n",
            "ENABLE_PC_SPEAKER": "n",
            "ENABLE_VBE": "n",
            "ENABLE_EXAMPLE_DRIVERS": "n",
            "INIT_PS2_CONTROLLER": "n",
            "INIT_PC_SPEAKER": "n",
            "INIT_STORAGE_ATA": "n",
            "INIT_STORAGE_ATA_BLOCK": "n",
            "INIT_SOUND_DRIVERS": "n",
            "INIT_MOUSE_DRIVER": "n",
            "INIT_NETWORK_STACK": "n",
            "INIT_BLUETOOTH_DRIVER": "y",
            "ENABLE_USB_HOST": "n",
            "INIT_USB_HOST": "n",
            "TOOL_MENUCONFIG_LANG": "en",
            "ARM64_BOARD_QEMU_VIRT": "n",
            "ARM64_BOARD_RPI4": "n",
            "ARM64_BOARD_RPI5": "y",
        }
    elif preset_key == "userspace":
        values = {
            "KERNEL_DEBUG_SHELL": "n",
            "ARCH_X86_64": "y",
            "ARCH_ARM64": "n",
            "TICK_RATE_HZ": "1000",
            "SCHEDULER_POLICY": "0",
            "ROOT_BLOCK_DEVICE": "hda",
            "ROOT_FILESYSTEM": "minix",
            "ENABLE_SMP": "n",
            "DEBUG_BINS_GROUP_CORE": "n",
            "DEBUG_BINS_GROUP_FS": "n",
            "DEBUG_BINS_GROUP_TEXT": "n",
            "DEBUG_BINS_GROUP_IDENTITY": "n",
            "DEBUG_BINS_GROUP_DIAG": "n",
            "DEBUG_BINS_GROUP_NET": "n",
            "DEBUG_BINS_GROUP_BT": "n",
            "ENABLE_NETWORKING": "n",
            "DRV_NIC_RTL8139": "n",
            "DRV_NIC_E1000": "n",
            "ENABLE_SOUND": "n",
            "ENABLE_BLUETOOTH": "n",
            "ENABLE_MOUSE": "y",
            "ENABLE_STORAGE_ATA": "y",
            "ENABLE_STORAGE_ATA_BLOCK": "y",
            "ENABLE_FS_MINIX": "y",
            "ENABLE_FS_TMPFS": "y",
            "ENABLE_FS_SIMPLEFS": "y",
            "ENABLE_FS_FAT16": "y",
            "ENABLE_PC_SPEAKER": "n",
            "ENABLE_VBE": "y",
            "ENABLE_EXAMPLE_DRIVERS": "n",
            "INIT_PS2_CONTROLLER": "y",
            "INIT_PC_SPEAKER": "n",
            "INIT_STORAGE_ATA": "y",
            "INIT_STORAGE_ATA_BLOCK": "y",
            "INIT_SOUND_DRIVERS": "n",
            "INIT_MOUSE_DRIVER": "y",
            "INIT_NETWORK_STACK": "n",
            "INIT_BLUETOOTH_DRIVER": "n",
            "ENABLE_USB_HOST": "n",
            "INIT_USB_HOST": "n",
            "TOOL_MENUCONFIG_LANG": "en",
        }
    for sym in symbols:
        if sym.name in values:
            sym.value = values[sym.name]
    # Exclusive board choice if any board symbol is set
    for bname in BOARD_SYMBOLS:
        if values.get(bname) == "y":
            set_board_selection(symbols, bname)
            break
    enforce_dependencies(symbols)

# ---------------------------------------------------------------------------
# Curses TUI
# ---------------------------------------------------------------------------

def run_menu(stdscr, symbols):
    curses.curs_set(0)
    curses.start_color()
    curses.use_default_colors()
    curses.init_pair(1, curses.COLOR_WHITE, curses.COLOR_BLUE)
    curses.init_pair(2, curses.COLOR_YELLOW, -1)
    curses.init_pair(3, curses.COLOR_GREEN, -1)
    curses.init_pair(4, curses.COLOR_RED, -1)
    curses.init_pair(5, curses.COLOR_CYAN, -1)

    visible = [s for s in symbols if dep_satisfied(symbols, s)]
    cursor = 0
    scroll = 0
    show_help = False
    show_preset_help = False
    ui_lang = get_ui_lang(symbols)
    dirty = False

    while True:
        visible = [s for s in symbols if dep_satisfied(symbols, s)]
        if cursor >= len(visible):
            cursor = max(0, len(visible) - 1)

        stdscr.erase()
        h, w = stdscr.getmaxyx()

        title = tr(ui_lang, "title")
        stdscr.attron(curses.color_pair(1) | curses.A_BOLD)
        stdscr.addstr(0, 0, " " * w)
        stdscr.addstr(0, max(0, (w - len(title)) // 2), title)
        stdscr.attroff(curses.color_pair(1) | curses.A_BOLD)

        footer = tr(ui_lang, "footer")
        stdscr.attron(curses.color_pair(1))
        stdscr.addstr(h - 1, 0, footer[:w - 1].ljust(w - 1))
        stdscr.attroff(curses.color_pair(1))

        if dirty:
            mod = tr(ui_lang, "modified")
            stdscr.attron(curses.color_pair(4) | curses.A_BOLD)
            stdscr.addstr(0, w - len(mod) - 1, mod)
            stdscr.attroff(curses.color_pair(4) | curses.A_BOLD)

        list_top = 2
        list_h = h - 4

        if cursor < scroll:
            scroll = cursor
        if cursor >= scroll + list_h:
            scroll = cursor - list_h + 1

        current_menu = None
        row = list_top
        for idx in range(scroll, min(scroll + list_h, len(visible))):
            sym = visible[idx]
            if sym.menu and sym.menu != current_menu:
                current_menu = sym.menu
                header = f"--- {current_menu} ---"
                if row < h - 2:
                    stdscr.attron(curses.color_pair(5) | curses.A_BOLD)
                    stdscr.addstr(row, 2, header[:w - 4])
                    stdscr.attroff(curses.color_pair(5) | curses.A_BOLD)
                    row += 1
                    if row >= h - 2:
                        break

            if row >= h - 2:
                break

            is_sel = (idx == cursor)

            if sym.type == "bool":
                marker = "[*]" if sym.value == "y" else "[ ]"
                color = curses.color_pair(3) if sym.value == "y" else curses.color_pair(0)
            elif sym.type == "int":
                marker = f"({sym.value})"
                color = curses.color_pair(2)
            else:
                marker = f'"{sym.value}"'
                color = curses.color_pair(2)

            line = f"  {marker} {sym.prompt}"
            if len(line) > w - 2:
                line = line[:w - 5] + "..."

            if is_sel:
                stdscr.attron(curses.A_REVERSE)
            stdscr.attron(color)
            stdscr.addstr(row, 0, line.ljust(w - 1)[:w - 1])
            stdscr.attroff(color)
            if is_sel:
                stdscr.attroff(curses.A_REVERSE)

            row += 1

        if show_help and cursor < len(visible):
            sym = visible[cursor]
            help_lines = [
                f"{tr(ui_lang, 'symbol')}: CONFIG_{sym.name}",
                f"{tr(ui_lang, 'type')}: {sym.type}",
                f"{tr(ui_lang, 'default')}: {sym.effective_default()}",
            ]
            if sym.depends:
                help_lines.append(f"{tr(ui_lang, 'depends')}: CONFIG_{sym.depends}")
            if sym.range_lo is not None:
                help_lines.append(f"{tr(ui_lang, 'range')}: {sym.range_lo}..{sym.range_hi}")
            if sym.help_text.strip():
                help_lines.append("")
                help_lines.extend(sym.help_text.strip().split("\n"))

            box_w = min(60, w - 4)
            box_h = min(len(help_lines) + 2, h - 4)
            box_y = max(1, (h - box_h) // 2)
            box_x = max(1, (w - box_w) // 2)

            try:
                win = curses.newwin(box_h, box_w, box_y, box_x)
                win.box()
                win.addstr(0, 2, tr(ui_lang, "help_title"))
                for i, hl in enumerate(help_lines[:box_h - 2]):
                    win.addstr(i + 1, 1, hl[:box_w - 3])
                win.refresh()
            except curses.error:
                pass

        if show_preset_help:
            ph = PRESET_HELP_TEXT.get(ui_lang, PRESET_HELP_TEXT["en"])
            help_lines = ph.strip().split("\n")
            box_w = min(72, w - 4)
            box_h = min(len(help_lines) + 2, h - 4)
            box_y = max(1, (h - box_h) // 2)
            box_x = max(1, (w - box_w) // 2)
            try:
                win = curses.newwin(box_h, box_w, box_y, box_x)
                win.box()
                win.addstr(0, 2, tr(ui_lang, "preset_help_title"))
                for i, hl in enumerate(help_lines[: box_h - 2]):
                    win.addstr(i + 1, 1, hl[: box_w - 3])
                win.refresh()
            except curses.error:
                pass

        stdscr.refresh()

        key = stdscr.getch()

        if show_help:
            show_help = False
            continue

        if show_preset_help:
            show_preset_help = False
            continue

        if key == curses.KEY_UP or key == ord('k'):
            cursor = max(0, cursor - 1)
        elif key == curses.KEY_DOWN or key == ord('j'):
            cursor = min(len(visible) - 1, cursor + 1)
        elif key == curses.KEY_HOME:
            cursor = 0
        elif key == curses.KEY_END:
            cursor = len(visible) - 1
        elif key == curses.KEY_PPAGE:
            cursor = max(0, cursor - list_h)
        elif key == curses.KEY_NPAGE:
            cursor = min(len(visible) - 1, cursor + list_h)
        elif key in (ord(' '), ord('\n'), curses.KEY_ENTER, 10, 13):
            if cursor < len(visible):
                sym = visible[cursor]
                if sym.type == "bool":
                    if sym.name in ARCH_SYMBOLS:
                        if sym.value != "y":
                            dirty |= set_arch_selection(symbols, sym.name)
                    elif sym.name in BOARD_SYMBOLS:
                        if sym.value != "y":
                            dirty |= set_board_selection(symbols, sym.name)
                    else:
                        sym.value = "n" if sym.value == "y" else "y"
                        dirty = True
                elif sym.type == "int":
                    val = curses_input(stdscr, f"CONFIG_{sym.name}", sym.value, h, w)
                    if val is not None:
                        try:
                            v = int(val)
                            if sym.range_lo is not None:
                                v = max(sym.range_lo, min(sym.range_hi, v))
                            sym.value = str(v)
                            dirty = True
                        except ValueError:
                            pass
                else:
                    val = curses_input(stdscr, f"CONFIG_{sym.name}", sym.value, h, w)
                    if val is not None:
                        sym.value = val
                        dirty = True
        elif key == ord('?'):
            show_help = True
        elif key in (ord("'"), ord("H"), ord("h")):
            show_preset_help = True
        elif key == ord('s') or key == ord('S'):
            save_config(symbols)
            dirty = False
            flash_msg(stdscr, tr(ui_lang, "saved"), h, w)
        elif key == ord('d') or key == ord('D'):
            for s in symbols:
                s.value = s.effective_default()
            dirty = True
        elif key == ord('g') or key == ord('G'):
            apply_preset(symbols, "generic")
            ui_lang = get_ui_lang(symbols)
            dirty = True
            flash_msg(stdscr, tr(ui_lang, "preset_generic"), h, w)
        elif key == ord('t') or key == ord('T'):
            apply_preset(symbols, "tiny")
            ui_lang = get_ui_lang(symbols)
            dirty = True
            flash_msg(stdscr, tr(ui_lang, "preset_tiny"), h, w)
        elif key == ord('i') or key == ord('I'):
            apply_preset(symbols, "iot")
            ui_lang = get_ui_lang(symbols)
            dirty = True
            flash_msg(stdscr, tr(ui_lang, "preset_iot"), h, w)
        elif key == ord('w') or key == ord('W'):
            apply_preset(symbols, "watch")
            ui_lang = get_ui_lang(symbols)
            dirty = True
            flash_msg(stdscr, tr(ui_lang, "preset_watch"), h, w)
        elif key == ord('c') or key == ord('C'):
            apply_preset(symbols, "custom")
            ui_lang = get_ui_lang(symbols)
            dirty = True
            flash_msg(stdscr, tr(ui_lang, "preset_custom"), h, w)
        elif key == ord('m') or key == ord('M'):
            default_lang = get_ui_lang(symbols)
            choice = curses_input(stdscr, tr(ui_lang, "mandocs_prompt"), default_lang, h, w)
            if choice is not None:
                choice = choice.strip().lower() or default_lang
                if choice not in ("en", "es", "all", "english", "spanish"):
                    choice = default_lang
                if choice == "english":
                    choice = "en"
                if choice == "spanish":
                    choice = "es"
                curses.endwin()
                rc = run_mandocs_batch(choice)
                # Resume curses
                stdscr.refresh()
                curses.doupdate()
                if rc == 0:
                    flash_msg(stdscr, tr(ui_lang, "mandocs_ok"), h, w)
                else:
                    flash_msg(stdscr, tr(ui_lang, "mandocs_fail"), h, w)
        elif key == ord('l') or key == ord('L'):
            ui_lang = "es" if ui_lang == "en" else "en"
            set_ui_lang(symbols, ui_lang)
            dirty = True
            flash_msg(stdscr, tr(ui_lang, "lang_switched"), h, w)
        elif key == ord('q') or key == ord('Q'):
            if dirty:
                if confirm(stdscr, tr(ui_lang, "save_before_exit"), h, w):
                    save_config(symbols)
            break


def curses_input(stdscr, label, current, h, w):
    """Simple single-line input dialog."""
    box_w = min(50, w - 4)
    box_h = 5
    box_y = (h - box_h) // 2
    box_x = (w - box_w) // 2
    win = curses.newwin(box_h, box_w, box_y, box_x)
    win.box()
    win.addstr(0, 2, f" {label} ")
    win.addstr(2, 2, "> ")
    win.refresh()

    curses.curs_set(1)
    curses.echo()
    buf = win.getstr(2, 4, box_w - 6)
    curses.noecho()
    curses.curs_set(0)

    try:
        return buf.decode("utf-8").strip()
    except Exception:
        return current


def flash_msg(stdscr, msg, h, w):
    box_w = min(len(msg) + 6, w - 2)
    box_y = h // 2
    box_x = (w - box_w) // 2
    try:
        win = curses.newwin(3, box_w, box_y, box_x)
        win.box()
        win.attron(curses.color_pair(3) | curses.A_BOLD)
        win.addstr(1, 2, msg[:box_w - 4])
        win.attroff(curses.color_pair(3) | curses.A_BOLD)
        win.refresh()
        curses.napms(1200)
    except curses.error:
        pass


def confirm(stdscr, msg, h, w):
    box_w = min(len(msg) + 16, w - 2)
    box_y = h // 2
    box_x = (w - box_w) // 2
    try:
        win = curses.newwin(3, box_w, box_y, box_x)
        win.box()
        win.addstr(1, 2, f"{msg} [y/N] ")
        win.refresh()
        key = win.getch()
        return key in (ord('y'), ord('Y'))
    except curses.error:
        return False


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------

def main():
    if not os.path.isfile(KCONFIG_PATH):
        print(f"Error: {KCONFIG_PATH} not found", file=sys.stderr)
        sys.exit(1)

    args = sys.argv[1:]

    # --sync-capabilities [--defconfig-only] [--dry-run]
    if len(args) > 0 and args[0] == "--sync-capabilities":
        defconfig_only = "--defconfig-only" in args[1:]
        dry_run = "--dry-run" in args[1:]
        sys.exit(sync_capabilities(defconfig_only=defconfig_only, dry_run=dry_run))

    # --mandocs en|es|all
    if len(args) > 0 and args[0] == "--mandocs":
        lang = args[1].strip().lower() if len(args) > 1 else "en"
        if lang in ("english",):
            lang = "en"
        if lang in ("spanish",):
            lang = "es"
        if lang not in ("en", "es", "all"):
            print("usage: menuconfig.py --mandocs en|es|all", file=sys.stderr)
            sys.exit(2)
        sys.exit(run_mandocs_batch(lang))

    symbols = parse_kconfig(KCONFIG_PATH)
    load_dotconfig(symbols)
    enforce_dependencies(symbols)

    if len(args) > 0 and args[0] == "--sync":
        enforce_dependencies(symbols)
        save_config(symbols)
        print("Synced .config and includes/generated/autoconf.h")
        return

    if len(args) > 1 and args[0] == "--preset":
        preset_name = args[1].strip().lower()
        allowed = ("generic", "desktop", "tiny", "iot", "hub", "watch", "userspace", "custom")
        if preset_name not in allowed:
            print(f"Unknown preset: {preset_name}", file=sys.stderr)
            sys.exit(2)
        apply_preset(symbols, preset_name)
        save_config(symbols)
        print(f"Applied preset '{preset_name}' and synced config files")
        return

    if len(args) > 0 and args[0] == "--set":
        if len(args) < 2:
            print("--set requires one or more SYMBOL=value assignments", file=sys.stderr)
            sys.exit(2)
        for assignment in args[1:]:
            set_symbol_value(symbols, assignment)
        enforce_dependencies(symbols)
        save_config(symbols)
        print("Applied explicit symbol assignments and synced config files")
        return

    # Interactive: symbols already merged with Kconfig defaults via load_dotconfig
    curses.wrapper(run_menu, symbols)


if __name__ == "__main__":
    main()
