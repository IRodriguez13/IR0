#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""
Build and install IR0 kernel manual pages (mdoc) from Documentation/*.md.

Interactive flow (make mandocs / mandocs-en / mandocs-es):
  1. Language (mandocs only; en/es targets skip this)
  2. All chapters? [Y/n]
  3. If no — pick from numbered list (in the selected language)
  4. Build + install only the selection

Default install: ~/.local/share/man/man7 (no sudo).
System-wide: sudo MANDOC_PREFIX=/usr/local make mandocs-en

Usage:
  make mandocs
  make mandocs-en
  make mandocs-es
  make mandocs-uninstall
  man IR0-krnl
"""

from __future__ import annotations

import argparse
import os
import re
import shutil
import subprocess
import sys
import textwrap
from dataclasses import dataclass
from datetime import date
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MANDOC_ROOT = ROOT / "build" / "mandoc"
LAST_LANG_FILE = MANDOC_ROOT / "last-lang"
LAST_CHAPTERS_FILE = MANDOC_ROOT / "last-chapters"
DEFAULT_PREFIX = Path(os.environ.get("MANDOC_PREFIX", Path.home() / ".local"))


def manifest_path(lang: str) -> Path:
    return MANDOC_ROOT / f"{lang}.install-manifest"


WRAP_COL = 78

# Spanish mirror has no ai_driven_dev tree; inject a short pointer chapter.
SPANISH_AI_DEV_MD = """\
# Desarrollo asistido por IA

Las reglas para agentes de IA se mantienen solo en ingles.

Consulte `Documentation/ai_driven_dev/` en el arbol del repositorio.
Instalacion local para Cursor: `make ai-dev-rules-install`.
"""


@dataclass(frozen=True)
class Chapter:
    slug: str
    basename: str
    desc_en: str
    desc_es: str
    english_only: bool = False


@dataclass(frozen=True)
class MandocChapter:
    slug: str
    basename: str
    man_name: str
    desc_en: str
    desc_es: str


MANDOC_ROOT_DOCS = ROOT / "Documentation" / "mandocs"

MANDOC_CHAPTERS: list[MandocChapter] = [
    MandocChapter(
        "vfs",
        "vfs.md",
        "IR0-vfs",
        "Virtual File System — mount router and pseudo-FS syscall paths",
        "Sistema de ficheros virtual — router de montajes y rutas pseudo en syscalls",
    ),
    MandocChapter(
        "boot",
        "boot.md",
        "IR0-boot",
        "Boot pipeline from GRUB through kmain and userspace handoff",
        "Pipeline de arranque desde GRUB hasta handoff a userspace",
    ),
    MandocChapter(
        "scheduler",
        "scheduler.md",
        "IR0-scheduler",
        "Scheduler policies, process states, wake/sleep, and timers",
        "Politicas del planificador, estados, wake/sleep y timers",
    ),
    MandocChapter(
        "memory",
        "memory.md",
        "IR0-memory",
        "Physical memory, paging, allocators, mmap, and ELF mappings",
        "Memoria fisica, paginacion, allocators, mmap y mapeos ELF",
    ),
    MandocChapter(
        "syscalls",
        "syscalls.md",
        "IR0-syscalls",
        "Syscall ABI, dispatch, copy_user, fd model, and isolation",
        "ABI de syscalls, dispatch, copy_user, modelo fd y aislamiento",
    ),
    MandocChapter(
        "filesystems",
        "filesystems.md",
        "IR0-filesystems",
        "Backend filesystems: tmpfs, minix, virtio-9p hostshare, proc/dev/sys",
        "Backends FS: tmpfs, minix, virtio-9p hostshare, proc/dev/sys",
    ),
    MandocChapter(
        "tty",
        "tty.md",
        "IR0-tty",
        "Console, TTY, keyboard path, line discipline, and devfs nodes",
        "Consola, TTY, teclado, line discipline y nodos devfs",
    ),
    MandocChapter(
        "drivers",
        "drivers.md",
        "IR0-drivers",
        "Driver registry, bootstrap, and hardware families",
        "Registro de drivers, bootstrap y familias de hardware",
    ),
    MandocChapter(
        "process",
        "process.md",
        "IR0-process",
        "Process model, fork/spawn, exec, fd inheritance, credentials",
        "Modelo de proceso, fork/spawn, exec, herencia fd y credenciales",
    ),
    MandocChapter(
        "userspace",
        "userspace.md",
        "IR0-userspace",
        "BusyBox, irinit, musl, TCC, DoomGeneric bootstrap",
        "BusyBox, irinit, musl, TCC y bootstrap DoomGeneric",
    ),
    MandocChapter(
        "multi-arch",
        "multi-arch.md",
        "IR0-multi-arch",
        "Architecture abstraction, HAL boundaries, and porting",
        "Abstraccion multi-arquitectura, HAL y porting",
    ),
    MandocChapter(
        "net",
        "net.md",
        "IR0-net",
        "IPv4 stack, RTL8139, /dev/net, AF_INET sock_stream TCP wire",
        "Stack IPv4, RTL8139, /dev/net, AF_INET sock_stream TCP wire",
    ),
    MandocChapter(
        "interrupts",
        "interrupts.md",
        "IR0-interrupts",
        "IDT, PIC, IRQ dispatch, exceptions, and syscall entry",
        "IDT, PIC, despacho IRQ, excepciones y entrada syscall",
    ),
    MandocChapter(
        "ipc",
        "ipc.md",
        "IR0-ipc",
        "Kernel IPC channels and POSIX pipes",
        "Canales IPC del kernel y pipes POSIX",
    ),
    MandocChapter(
        "input",
        "input.md",
        "IR0-input",
        "evdev-style input queue, keyboard and PS/2 mouse",
        "Cola de input estilo evdev, teclado y raton PS/2",
    ),
    MandocChapter(
        "graphics",
        "graphics.md",
        "IR0-graphics",
        "Framebuffer, VBE, /dev/fb0, mmap, and console renderer",
        "Framebuffer, VBE, /dev/fb0, mmap y console renderer",
    ),
    MandocChapter(
        "debug-bins",
        "debug-bins.md",
        "IR0-debug-bins",
        "In-kernel debug shell and syscall-only commands",
        "Shell de depuracion in-kernel y comandos solo-syscall",
    ),
    MandocChapter(
        "signals",
        "signals.md",
        "IR0-signals",
        "Signal delivery, handlers, and rt_sig* syscalls",
        "Entrega de senales, handlers y syscalls rt_sig*",
    ),
    MandocChapter(
        "security",
        "security.md",
        "IR0-security",
        "Credentials, permissions, chmod/chown, and sudo_auth",
        "Credenciales, permisos, chmod/chown y sudo_auth",
    ),
]

CHAPTERS: list[Chapter] = [
    Chapter("overview", "README.md", "Documentation index and map",
            "Indice y mapa de documentacion"),
    Chapter("tooling", "TOOLING.md", "Build, config, and validation targets",
            "Build, configuracion y validacion"),
    Chapter("makefile", "MAKEFILE.md", "Makefile orchestration and target taxonomy",
            "Makefile como orquestador central"),
    Chapter("decoupling", "DECOUPLING.md", "Subsystem boundaries and facades",
            "Limites de subsistemas y facades"),
    Chapter("filesystem", "FILESYSTEM.md", "VFS and mounted filesystem backends",
            "VFS y backends de filesystem"),
    Chapter("virtual-filesystems", "VIRTUAL_FILESYSTEMS.md", "/proc, /dev, /sys",
            "/proc, /dev, /sys"),
    Chapter("drivers", "DRIVERS.md", "Driver registry and bootstrap",
            "Registro de drivers e init"),
    Chapter("interrupts", "INTERRUPTS.md", "IDT, PIC, syscalls, exceptions",
            "IDT, PIC, syscalls, excepciones"),
    Chapter("memory", "MEMORY.md", "PMM, paging, and allocators",
            "PMM, paginacion y allocators"),
    Chapter("processes", "PROCESSES.md", "Process lifecycle and credentials",
            "Ciclo de vida de procesos y credenciales"),
    Chapter("scheduling", "SCHEDULING.md", "Scheduler policies",
            "Politicas del planificador"),
    Chapter("unix-differences", "UNIX_DIFFERENCES.md",
            "POSIX/Linux compatibility boundaries",
            "Limites de compatibilidad POSIX/Linux"),
    Chapter("ai-driven-dev", "README.md", "AI-assisted development rules",
            "Reglas para desarrollo asistido por IA", english_only=True),
]

UI_STRINGS = {
    "en": {
        "manual_title": "IR0 kernel developer manual",
        "manual_desc": (
            "This manual aggregates subsystem documentation from the in-tree "
            "Documentation/ directory."
        ),
        "chapters_intro": "Chapters follow the same order as the documentation map.",
        "see_also_setup": "at the repository root for bootstrap and QEMU usage.",
        "see_also_chapters": "Per-chapter pages:",
        "authors": "IR0 kernel contributors",
        "source": "Source",
    },
    "es": {
        "manual_title": "Manual de desarrollo del kernel IR0",
        "manual_desc": (
            "Este manual reune la documentacion de subsistemas del arbol "
            "Documentation/ (espejo en Documentation/esp/ cuando existe)."
        ),
        "chapters_intro": "Los capitulos siguen el mismo orden que el mapa de documentacion.",
        "see_also_setup": "en la raiz del repositorio para bootstrap y QEMU.",
        "see_also_chapters": "Paginas por capitulo:",
        "authors": "Contribuidores del kernel IR0",
        "source": "Fuente",
    },
}

PROMPTS = {
    "en": {
        "pick_language": "Select documentation language:",
        "lang_en": "  [1] english (default)",
        "lang_es": "  [2] spanish",
        "lang_prompt": "Choice [1]: ",
        "install_note": (
            "Install target: {prefix}/share/man/man7\n"
            "No sudo needed for ~/.local (default).\n"
            "System-wide: sudo MANDOC_PREFIX=/usr/local make mandocs-en"
        ),
        "all_chapters_q": "Install all kernel documentation chapters?",
        "all_yes_hint": "  [Y] yes, all chapters (default)",
        "all_no_hint": "  [n] no, choose specific chapters",
        "all_prompt": "All chapters [Y/n]: ",
        "pick_chapters_title": "Available chapters:",
        "pick_hint": "Enter numbers, ranges, or slugs (comma-separated).",
        "pick_examples": "Examples: 1,3,5   tooling,memory   2-6",
        "pick_prompt": "Chapters to install: ",
        "invalid_lang": "Invalid choice {choice!r}, using english.",
        "invalid_pick": "Invalid selection: {err}; using all chapters.",
        "non_tty_lang": "Non-interactive: using english (make mandocs-en / mandocs-es).",
        "non_tty_all": "Non-interactive: installing all chapters.",
        "perm_denied": (
            "Cannot write to {path}\n"
            "User install (no sudo): make mandocs-en\n"
            "System-wide: sudo MANDOC_PREFIX=/usr/local make mandocs-en"
        ),
        "uninstall_hint": "Uninstall: make mandocs-uninstall",
        "manpath_hint": "If man IR0-krnl is not found: export MANPATH={prefix}/share/man:$MANPATH",
    },
    "es": {
        "pick_language": "Seleccione idioma de documentacion:",
        "lang_en": "  [1] ingles (predeterminado)",
        "lang_es": "  [2] espanol",
        "lang_prompt": "Opcion [1]: ",
        "install_note": (
            "Destino: {prefix}/share/man/man7\n"
            "Sin sudo si usa ~/.local (predeterminado).\n"
            "Todo el sistema: sudo MANDOC_PREFIX=/usr/local make mandocs-es"
        ),
        "all_chapters_q": "Instalar toda la documentacion del kernel?",
        "all_yes_hint": "  [Y] si, todos los capitulos (predeterminado)",
        "all_no_hint": "  [n] no, elegir capitulos concretos",
        "all_prompt": "Todos los capitulos [Y/n]: ",
        "pick_chapters_title": "Capitulos disponibles:",
        "pick_hint": "Numeros, rangos o slugs separados por coma.",
        "pick_examples": "Ejemplos: 1,3,5   tooling,memory   2-6",
        "pick_prompt": "Capitulos a instalar: ",
        "invalid_lang": "Opcion invalida {choice!r}, usando ingles.",
        "invalid_pick": "Seleccion invalida: {err}; usando todos los capitulos.",
        "non_tty_lang": "No interactivo: ingles (use make mandocs-en / mandocs-es).",
        "non_tty_all": "No interactivo: instalando todos los capitulos.",
        "perm_denied": (
            "No se puede escribir en {path}\n"
            "Instalacion de usuario (sin sudo): make mandocs-es\n"
            "Todo el sistema: sudo MANDOC_PREFIX=/usr/local make mandocs-es"
        ),
        "uninstall_hint": "Desinstalar: make mandocs-uninstall",
        "manpath_hint": "Si man IR0-krnl-es no aparece: export MANPATH={prefix}/share/man:$MANPATH",
    },
}

_LINE_MACRO_START = re.compile(
    r"^(It|Ic|Pa|Ev|En|Ad|Ar|Fl|Fn|Fa|Fd|Ft|Sy|Em|Li|Ql|Ms|St|Ux|Dx|Ox|Os|Dt|Dd|"
    r"Sh|Ss|Bl|El|Bd|Ed|Nm|Nd|Xr|An|Ap|Bq|Brq|Dq|Eq|Ne|Pq|Sq|Vt|At|Bf|Ef|Rs|Re)\b"
)


def normalize_lang(raw: str) -> str:
    val = raw.strip().lower()
    if val in ("en", "english", "inglés", "ingles", "1"):
        return "en"
    if val in ("es", "spanish", "español", "espanol", "2"):
        return "es"
    raise ValueError(f"unsupported language: {raw!r} (use english or spanish)")


def ui(lang: str) -> dict[str, str]:
    return PROMPTS[lang]


def ask_yes_default(prompt: str) -> bool:
    try:
        choice = input(prompt).strip().lower()
    except EOFError:
        return True
    if not choice or choice in ("y", "yes", "s", "si", "sí"):
        return True
    if choice in ("n", "no"):
        return False
    return True


def print_install_banner(lang: str, prefix: Path) -> None:
    print("")
    print(ui(lang)["install_note"].format(prefix=prefix))


def ask_language() -> str:
    if not sys.stdin.isatty():
        print(PROMPTS["en"]["non_tty_lang"])
        return "en"
    p = PROMPTS["en"]
    print("")
    print(f"{p['pick_language']} / {PROMPTS['es']['pick_language']}")
    print(p["lang_en"])
    print(p["lang_es"])
    print("")
    try:
        choice = input(f"{p['lang_prompt']}").strip()
    except EOFError:
        return "en"
    if not choice:
        return "en"
    try:
        return normalize_lang(choice)
    except ValueError:
        print(p["invalid_lang"].format(choice=choice))
        return "en"


def resolve_language(explicit: str | None) -> str:
    if explicit:
        return normalize_lang(explicit)
    return ask_language()


def chapter_by_slug(slug: str) -> Chapter | None:
    key = slug.strip().lower().replace("_", "-")
    for ch in CHAPTERS:
        if ch.slug == key:
            return ch
    return None


def parse_chapter_selection(raw: str) -> list[Chapter]:
    """Parse comma-separated slugs, numbers, or ranges (e.g. 1,3,tooling,5-7)."""
    tokens = [t.strip() for t in raw.split(",") if t.strip()]
    if not tokens:
        return CHAPTERS[:]

    picked: list[Chapter] = []
    seen: set[str] = set()

    def add(ch: Chapter) -> None:
        if ch.slug not in seen:
            seen.add(ch.slug)
            picked.append(ch)

    for tok in tokens:
        low = tok.lower()
        if low in ("all", "a", "todos", "*"):
            return CHAPTERS[:]
        if re.match(r"^\d+\s*-\s*\d+$", tok):
            start_s, end_s = re.split(r"\s*-\s*", tok)
            start, end = int(start_s), int(end_s)
            if start > end:
                start, end = end, start
            for n in range(start, end + 1):
                if 1 <= n <= len(CHAPTERS):
                    add(CHAPTERS[n - 1])
            continue
        if tok.isdigit():
            n = int(tok)
            if 1 <= n <= len(CHAPTERS):
                add(CHAPTERS[n - 1])
            else:
                raise ValueError(f"chapter number out of range: {n}")
            continue
        ch = chapter_by_slug(tok)
        if ch is None:
            known = ", ".join(c.slug for c in CHAPTERS)
            raise ValueError(f"unknown chapter {tok!r} (known: {known})")
        add(ch)

    if not picked:
        return CHAPTERS[:]
    return picked


def ask_chapters(lang: str) -> list[Chapter]:
    p = ui(lang)

    if not sys.stdin.isatty():
        print(p["non_tty_all"])
        return CHAPTERS[:]

    print("")
    print(p["all_chapters_q"])
    print(p["all_yes_hint"])
    print(p["all_no_hint"])
    print("")
    if ask_yes_default(p["all_prompt"]):
        return CHAPTERS[:]

    print("")
    print(p["pick_chapters_title"])
    for i, ch in enumerate(CHAPTERS, start=1):
        print(f"  [{i:2d}] {ch.slug:<22} {chapter_desc(ch, lang)}")
    print("")
    print(p["pick_hint"])
    print(p["pick_examples"])
    print("")
    try:
        choice = input(p["pick_prompt"]).strip()
    except EOFError:
        return CHAPTERS[:]
    if not choice:
        return CHAPTERS[:]
    try:
        selected = parse_chapter_selection(choice)
        if not selected:
            return CHAPTERS[:]
        return selected
    except ValueError as exc:
        print(p["invalid_pick"].format(err=exc))
        return CHAPTERS[:]


def resolve_chapters(explicit: str | None, lang: str) -> list[Chapter]:
    if explicit:
        return parse_chapter_selection(explicit)
    return ask_chapters(lang)


def resolve_mandoc_chapter_source(lang: str, chapter: MandocChapter) -> tuple[Path, str]:
    sub = "esp" if lang == "es" else "en"
    src = MANDOC_ROOT_DOCS / sub / chapter.basename
    display = f"Documentation/mandocs/{sub}/{chapter.basename}"
    if lang == "es" and not src.is_file():
        en = MANDOC_ROOT_DOCS / "en" / chapter.basename
        if en.is_file():
            print(
                f"warning: no Spanish mandoc for {chapter.basename}, using English",
                file=sys.stderr,
            )
            return en, f"Documentation/mandocs/en/{chapter.basename}"
    return src, display


def read_mandoc_chapter_markdown(lang: str, chapter: MandocChapter) -> str:
    src, _ = resolve_mandoc_chapter_source(lang, chapter)
    if not src.is_file():
        raise FileNotFoundError(f"missing mandoc source: {src}")
    return src.read_text(encoding="utf-8")


def build_mandoc_chapter_page(lang: str, chapter: MandocChapter) -> str:
    desc = chapter.desc_es if lang == "es" else chapter.desc_en
    body = md_to_mdoc_body(read_mandoc_chapter_markdown(lang, chapter))
    header = page_header(chapter.man_name, 7, desc)
    return "\n".join(header + body) + "\n"


def resolve_chapter_source(lang: str, chapter: Chapter) -> tuple[Path, str]:
    """Return (source path, display path string)."""
    if chapter.english_only:
        if lang == "es":
            return Path("__inline__"), "Documentation/ai_driven_dev/ (en)"
        return (
            ROOT / "Documentation" / "ai_driven_dev" / chapter.basename,
            "Documentation/ai_driven_dev/README.md",
        )

    if lang == "es":
        esp = ROOT / "Documentation" / "esp" / chapter.basename
        if esp.is_file():
            return esp, f"Documentation/esp/{chapter.basename}"
        en = ROOT / "Documentation" / chapter.basename
        print(
            f"warning: no Spanish file for {chapter.basename}, using English",
            file=sys.stderr,
        )
        return en, f"Documentation/{chapter.basename}"

    en = ROOT / "Documentation" / chapter.basename
    return en, f"Documentation/{chapter.basename}"


def read_chapter_markdown(lang: str, chapter: Chapter) -> str:
    src, _ = resolve_chapter_source(lang, chapter)
    if src == Path("__inline__"):
        return SPANISH_AI_DEV_MD
    if not src.is_file():
        raise FileNotFoundError(f"missing doc source: {src}")
    return src.read_text(encoding="utf-8")


def esc_mdoc(text: str) -> str:
    return text.replace("\\", "\\e").replace("&", "\\&")


def esc_line_start(text: str) -> str:
    if _LINE_MACRO_START.match(text):
        return "\\&" + text
    return text


def wrap_mdoc_line(text: str) -> str:
    text = esc_line_start(esc_mdoc(text))
    if len(text) <= WRAP_COL:
        return text
    return "\n".join(textwrap.wrap(text, width=WRAP_COL))


def inline_md_to_mdoc(text: str) -> str:
    text = re.sub(r"`([^`]+)`", r".Sy \1", text)
    text = re.sub(r"\*\*([^*]+)\*\*", r".Sy \1", text)
    text = re.sub(r"\*([^*]+)\*", r".Em \1", text)
    text = re.sub(r"\[([^\]]+)\]\(([^)]+)\)", r"\1 (\2)", text)
    return wrap_mdoc_line(text)


def md_to_mdoc_body(md: str) -> list[str]:
    lines = md.splitlines()
    out: list[str] = []
    i = 0
    in_code = False
    list_mode: str | None = None
    need_pp = False

    def close_list() -> None:
        nonlocal list_mode
        if list_mode:
            out.append(".El")
            list_mode = None

    def emit_text(text: str) -> None:
        nonlocal need_pp
        close_list()
        if need_pp:
            out.append(".Pp")
        out.append(inline_md_to_mdoc(text))
        need_pp = True

    while i < len(lines):
        line = lines[i].rstrip()

        if line.startswith("```"):
            if in_code:
                out.append(".Ed")
                in_code = False
                need_pp = True
            else:
                close_list()
                if need_pp:
                    out.append(".Pp")
                    need_pp = False
                out.append(".Bd -literal -offset indent")
                in_code = True
            i += 1
            continue

        if in_code:
            out.append(esc_mdoc(line))
            i += 1
            continue

        if not line.strip():
            i += 1
            continue

        if line.startswith("#"):
            level = len(line) - len(line.lstrip("#"))
            title = line.lstrip("#").strip()
            close_list()
            if need_pp:
                out.append(".Pp")
            need_pp = False
            if level == 1:
                out.append(".Sh " + esc_mdoc(title.upper()))
            else:
                out.append(".Ss " + esc_mdoc(title))
            i += 1
            continue

        if re.match(r"^[-*]\s+", line):
            if list_mode != "bullet":
                close_list()
                if need_pp:
                    out.append(".Pp")
                    need_pp = False
                out.append(".Bl -bullet -compact")
                list_mode = "bullet"
            item = re.sub(r"^[-*]\s+", "", line)
            out.append(".It")
            out.append(inline_md_to_mdoc(item))
            i += 1
            continue

        if re.match(r"^\d+\.\s+", line):
            if list_mode != "enum":
                close_list()
                if need_pp:
                    out.append(".Pp")
                    need_pp = False
                out.append(".Bl -enum -compact")
                list_mode = "enum"
            item = re.sub(r"^\d+\.\s+", "", line)
            out.append(".It")
            out.append(inline_md_to_mdoc(item))
            i += 1
            continue

        if line.startswith("|") and "|" in line[1:]:
            close_list()
            if need_pp:
                out.append(".Pp")
                need_pp = False
            table_rows: list[str] = []
            while i < len(lines) and lines[i].strip().startswith("|"):
                row = lines[i].strip()
                if not re.match(r"^\|[-: |]+\|$", row):
                    cells = [c.strip() for c in row.strip("|").split("|")]
                    table_rows.append(" | ".join(cells))
                i += 1
            if table_rows:
                out.append(".Bd -literal -offset indent")
                for row in table_rows:
                    out.append(esc_mdoc(row))
                out.append(".Ed")
                need_pp = True
            continue

        emit_text(line)
        i += 1

    close_list()
    return out


def page_header(title: str, section: int, desc: str) -> list[str]:
    today = date.today().strftime("%B %d, %Y")
    return [
        f".Dd {today}",
        f".Dt {title.upper()} {section}",
        ".Os Local",
        ".Sh NAME",
        f".Nm {title.lower()}",
        f".Nd {esc_mdoc(desc)}",
    ]


def chapter_desc(chapter: Chapter, lang: str) -> str:
    return chapter.desc_es if lang == "es" else chapter.desc_en


def build_chapter_page(lang: str, chapter: Chapter) -> str:
    name = f"IR0-krnl-{chapter.slug}"
    body = md_to_mdoc_body(read_chapter_markdown(lang, chapter))
    header = page_header(name, 7, chapter_desc(chapter, lang))
    return "\n".join(header + body) + "\n"


def build_unified_page(lang: str, chapters: list[Chapter]) -> str:
    ui = UI_STRINGS[lang]
    header = page_header("IR0-krnl", 7, ui["manual_title"])
    parts = header + [
        ".Sh DESCRIPTION",
        wrap_mdoc_line(ui["manual_desc"]),
        ".Pp",
        wrap_mdoc_line(ui["chapters_intro"]),
        ".Sh CONTENTS",
        ".Bl -bullet -compact",
    ]
    for chapter in chapters:
        _src, display = resolve_chapter_source(lang, chapter)
        parts.append(".It")
        parts.append(wrap_mdoc_line(f"{chapter.slug} ({display})"))
        parts.append(wrap_mdoc_line(chapter_desc(chapter, lang)))
    parts.append(".El")

    for chapter in chapters:
        _src, display = resolve_chapter_source(lang, chapter)
        parts.append(f".Sh {chapter.slug.upper().replace('-', ' ')}")
        parts.append(wrap_mdoc_line(f"{ui['source']}: {display}"))
        parts.extend(md_to_mdoc_body(read_chapter_markdown(lang, chapter)))

    parts.extend([
        ".Sh SEE ALSO",
        ".Pa SETUP.md",
        wrap_mdoc_line(ui["see_also_setup"]),
        ".Pp",
        wrap_mdoc_line(ui["see_also_chapters"]),
        wrap_mdoc_line(f"build/mandoc/{lang}/IR0-krnl-<chapter>.7"),
        ".Sh AUTHORS",
        f".An {ui['authors']}",
    ])
    return "\n".join(parts) + "\n"


def lint_mdoc(path: Path, strict: bool) -> bool:
    try:
        args = ["mandoc", "-T", "utf8"]
        if strict:
            args.extend(["-W", "all"])
        mandoc = subprocess.run(args + [str(path)], capture_output=True, text=True)
    except FileNotFoundError:
        print("mandoc not installed; skipping render check")
        return True

    if mandoc.returncode != 0 and not mandoc.stdout:
        print(f"mandoc render failed for {path}:", file=sys.stderr)
        print(mandoc.stderr, file=sys.stderr)
        return False

    if strict and mandoc.stderr.strip():
        print(f"mandoc strict lint for {path}:", file=sys.stderr)
        print(mandoc.stderr, file=sys.stderr)
        return False

    if mandoc.stderr.strip() and not strict:
        warns = mandoc.stderr.count("mdoc warning:")
        if warns:
            print(f"note: {warns} mandoc cosmetic warning(s); use --strict-lint to fail")
    return True


def installed_page_name(src_name: str, lang: str) -> str:
    """Spanish pages get a -es suffix so EN and ES can coexist in man7."""
    if lang != "es":
        return src_name
    if src_name.startswith("IR0-") and src_name.endswith(".7") and not src_name.endswith("-es.7"):
        return src_name[:-2] + "-es.7"
    return src_name


def primary_man_name(lang: str) -> str:
    return "IR0-krnl-es" if lang == "es" else "IR0-krnl"


def install_pages(lang: str, out_dir: Path, prefix: Path) -> tuple[Path, list[str]]:
    man7 = prefix / "share" / "man" / "man7"
    try:
        man7.mkdir(parents=True, exist_ok=True)
    except PermissionError:
        raise PermissionError(str(man7)) from None

    pages = sorted(out_dir.glob("*.7"))
    if not pages:
        raise FileNotFoundError(f"no .7 pages in {out_dir}")

    installed: list[str] = []
    for src in pages:
        dst_name = installed_page_name(src.name, lang)
        dst = man7 / dst_name
        try:
            shutil.copy2(src, dst)
        except PermissionError:
            raise PermissionError(str(dst)) from None
        installed.append(dst_name)
        print(f"installed {dst}")

    return man7, installed


def write_install_manifest(lang: str, prefix: Path, installed: list[str]) -> None:
    MANDOC_ROOT.mkdir(parents=True, exist_ok=True)
    manifest_path(lang).write_text(
        f"prefix={prefix}\n"
        f"lang={lang}\n"
        f"pages={','.join(installed)}\n",
        encoding="utf-8",
    )


def read_install_manifest(lang: str) -> tuple[Path, list[str]] | None:
    path = manifest_path(lang)
    if not path.is_file():
        return None
    data: dict[str, str] = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        if "=" in line:
            k, v = line.split("=", 1)
            data[k.strip()] = v.strip()
    prefix = Path(data.get("prefix", str(DEFAULT_PREFIX))).expanduser()
    pages_raw = data.get("pages", "")
    pages = [p for p in pages_raw.split(",") if p]
    if not pages:
        return None
    return prefix, pages


def uninstall_mandocs(lang: str | None, prefix: Path | None) -> int:
    if lang in (None, ""):
        if LAST_LANG_FILE.is_file():
            lang = LAST_LANG_FILE.read_text(encoding="utf-8").strip()
        else:
            lang = "all"

    langs: list[str]
    if lang == "all":
        langs = ["en", "es"]
    else:
        langs = [normalize_lang(lang)]

    removed = 0
    for lg in langs:
        info = read_install_manifest(lg)
        if info is None:
            print(f"no install manifest for lang={lg} ({manifest_path(lg)})")
            continue
        man_prefix, pages = info
        man7 = (prefix or man_prefix) / "share" / "man" / "man7"
        for name in pages:
            target = man7 / name
            if target.is_file():
                target.unlink()
                print(f"removed {target}")
                removed += 1
            else:
                print(f"skip missing {target}")
        manifest_path(lg).unlink(missing_ok=True)

    if removed == 0 and lang not in (None, "", "all"):
        print("nothing removed — run make mandocs first or try MANDOC_LANG=all")
        return 1
    print(f"uninstall done ({removed} file(s))")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--lang",
        choices=["en", "es", "english", "spanish", "all"],
        help="Documentation language (english or spanish)",
    )
    parser.add_argument(
        "--chapters",
        metavar="LIST",
        help="Chapter slugs, numbers, or ranges (e.g. 1,3,tooling,5-7)",
    )
    parser.add_argument(
        "--mandoc-only",
        action="store_true",
        help="Build only Documentation/mandocs/ chapters (IR0-vfs, …)",
    )
    parser.add_argument(
        "--legacy-only",
        action="store_true",
        help="Build only legacy Documentation/*.md chapters",
    )
    parser.add_argument("--uninstall", action="store_true", help="Remove installed mandoc pages")
    parser.add_argument("--no-lint", action="store_true", help="Skip mandoc render check")
    parser.add_argument(
        "--strict-lint",
        action="store_true",
        help="Fail on mandoc -W all STYLE/WARNING messages",
    )
    parser.add_argument(
        "--no-install",
        action="store_true",
        help="Build only; do not copy into MANPATH",
    )
    parser.add_argument(
        "--prefix",
        type=Path,
        default=DEFAULT_PREFIX,
        help="Install root (default: $MANDOC_PREFIX or ~/.local)",
    )
    args = parser.parse_args()

    if args.uninstall:
        lang = None if not args.lang or args.lang == "all" else args.lang
        return uninstall_mandocs(lang, args.prefix.expanduser().resolve() if args.prefix else None)

    do_install = not args.no_install
    lang = resolve_language(args.lang if args.lang not in (None, "all") else None)
    prefix = args.prefix.expanduser().resolve()

    if do_install and sys.stdin.isatty():
        print_install_banner(lang, prefix)

    if args.mandoc_only and args.legacy_only:
        print("error: --mandoc-only and --legacy-only are mutually exclusive", file=sys.stderr)
        return 1

    build_legacy = not args.mandoc_only
    build_mandoc = not args.legacy_only

    chapters = resolve_chapters(args.chapters, lang) if build_legacy else []
    mandoc_chapters = list(MANDOC_CHAPTERS) if build_mandoc else []

    out_dir = MANDOC_ROOT / lang
    if out_dir.is_dir():
        for old in out_dir.glob("*.7"):
            old.unlink()
    out_dir.mkdir(parents=True, exist_ok=True)
    LAST_LANG_FILE.write_text(lang + "\n", encoding="utf-8")
    slug_log = [c.slug for c in chapters] + [c.slug for c in mandoc_chapters]
    LAST_CHAPTERS_FILE.write_text(",".join(slug_log) + "\n", encoding="utf-8")

    print(f"language: {lang} ({'english' if lang == 'en' else 'spanish'})")
    if chapters:
        print(f"legacy chapters: {', '.join(c.slug for c in chapters)}")
    if mandoc_chapters:
        print(f"mandoc chapters: {', '.join(c.slug for c in mandoc_chapters)}")

    lint_target: Path | None = None

    if build_legacy:
        unified = out_dir / "IR0-krnl.7"
        unified.write_text(build_unified_page(lang, chapters), encoding="utf-8")
        print(f"wrote {unified.relative_to(ROOT)}")
        lint_target = unified

        for chapter in chapters:
            out = out_dir / f"IR0-krnl-{chapter.slug}.7"
            out.write_text(build_chapter_page(lang, chapter), encoding="utf-8")
            print(f"wrote {out.relative_to(ROOT)}")

    if build_mandoc:
        for chapter in mandoc_chapters:
            out = out_dir / f"{chapter.man_name}.7"
            out.write_text(build_mandoc_chapter_page(lang, chapter), encoding="utf-8")
            print(f"wrote {out.relative_to(ROOT)}")
            if lint_target is None:
                lint_target = out

    if not args.no_lint and lint_target is not None:
        ok = lint_mdoc(lint_target, strict=args.strict_lint)
        if not ok:
            return 1

    man_name = primary_man_name(lang)
    if do_install:
        try:
            man7, installed = install_pages(lang, out_dir, prefix)
        except PermissionError as exc:
            print(ui(lang)["perm_denied"].format(path=exc.args[0]), file=sys.stderr)
            return 1
        write_install_manifest(lang, prefix, installed)
        print("")
        print(f"Installed under {man7}")
        print(f"  man {man_name}")
        for name in installed:
            if name != installed_page_name("IR0-krnl.7", lang):
                print(f"  man {name[:-3] if name.endswith('.7') else name}")
        print(ui(lang)["manpath_hint"].format(prefix=prefix))
        print(ui(lang)["uninstall_hint"])
    else:
        print("")
        if lint_target is not None:
            print(f"  man -l {lint_target}")
        for chapter in mandoc_chapters:
            out = out_dir / f"{chapter.man_name}.7"
            print(f"  man -l {out}")
    print(f"  make mandocs-view")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
