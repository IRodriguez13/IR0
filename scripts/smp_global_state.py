#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Inventory of file-scope mutable variables (SMP-0).

Scans C sources (.c and .h) under block, drivers, fs, ipc, kernel, ktm, mm,
net, sched, smp and sound. arch/, toolchain/ and tests/ are out of scope.
A hit is a file-scope object that is not const, not a function and not a
type. extern declarations are not definitions, so a header that only
declares extern contributes no rows.

The preprocessor is not evaluated: directive lines are dropped and both
sides of an #if stay in the text. Macro invocations that hide a declarator
are not expanded.

Writes scripts/smp_entry_gate/global_state.tsv with columns path, name,
line, sorted by path, line and name. This script only builds the inventory.
It does not fail the build and it does not call the SMP entry gate.

Usage:
    python3 scripts/smp_global_state.py
"""

from pathlib import Path
import os
import re
import sys


ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "scripts" / "smp_entry_gate" / "global_state.tsv"

TREES = (
    "block",
    "drivers",
    "fs",
    "ipc",
    "kernel",
    "ktm",
    "mm",
    "net",
    "sched",
    "smp",
    "sound",
)

STORAGE = {"typedef", "extern", "static", "auto", "register", "__thread",
           "_Thread_local"}
QUAL = {"const", "volatile", "restrict", "_Atomic", "__const", "__const__",
        "__volatile", "__volatile__", "__restrict", "__restrict__"}
CONST_WORDS = {"const", "__const", "__const__"}
TYPE_WORDS = {
    "void", "char", "short", "int", "long", "float", "double", "signed",
    "unsigned", "bool", "_Bool", "_Complex", "_Imaginary", "__int128",
    "_Float16", "_Float32", "_Float64", "_Float128", "__fp16",
    "char16_t", "char32_t", "wchar_t",
}
NOISE = {"inline", "__inline", "__inline__", "_Noreturn", "__extension__",
         "__force", "__user", "__kernel", "__iomem", "__must_check"}
TYPE_INTRODUCERS = {"struct", "union", "enum"}
ATTRIBUTES = {"__attribute__", "__attribute", "__declspec"}
ASM_WORDS = {"asm", "__asm", "__asm__"}
ASSERT_WORDS = {"_Static_assert", "static_assert"}
NOT_NAMES = (STORAGE | QUAL | TYPE_WORDS | NOISE | TYPE_INTRODUCERS |
             ATTRIBUTES | ASM_WORDS | ASSERT_WORDS |
             {"if", "else", "for", "while", "do", "switch", "case", "default",
              "return", "goto", "break", "continue", "sizeof", "typeof",
              "__typeof", "__typeof__", "_Alignof", "__alignof__", "alignof",
              "_Alignas", "alignas"})
MACRO_NAME = re.compile(r"^[A-Z_][A-Z0-9_]*$")
MULTI_PUNCT = (
    "...", "<<=", ">>=", "<<", ">>", "==", "!=", "<=", ">=", "&&", "||",
    "++", "--", "->", "+=", "-=", "*=", "/=", "%=", "&=", "|=", "^=",
)


class Tok:
    __slots__ = ("text", "line")

    def __init__(self, text, line):
        self.text = text
        self.line = line


class TypeNode:
    __slots__ = ("kind", "const", "inner")

    def __init__(self, kind, is_const=False, inner=None):
        self.kind = kind
        self.const = is_const
        self.inner = inner


def is_mutable(node):
    """The object can change. Arrays follow their element; functions cannot."""
    if node.kind == "func":
        return False
    if node.kind == "array":
        return is_mutable(node.inner)
    return not node.const


def build_type(base_const, mods):
    node = TypeNode("base", base_const)
    for mod in reversed(mods):
        kind = mod[0]
        if kind == "ptr":
            node = TypeNode("ptr", mod[1], node)
        elif kind == "array":
            node = TypeNode("array", False, node)
        else:
            node = TypeNode("func", False, node)
    return node


def strip_comments_and_strings(text):
    """Replace comments and literals with whitespace, keeping line breaks."""
    out = []
    i = 0
    n = len(text)
    while i < n:
        if text.startswith("/*", i):
            j = text.find("*/", i + 2)
            if j < 0:
                chunk = text[i:]
                out.append(" " + ("\n" * chunk.count("\n")))
                break
            chunk = text[i:j + 2]
            out.append(" " + ("\n" * chunk.count("\n")))
            i = j + 2
            continue
        if text.startswith("//", i):
            j = i + 2
            while j < n and text[j] != "\n":
                if text[j] == "\\" and j + 1 < n and text[j + 1] == "\n":
                    j += 2
                    continue
                j += 1
            chunk = text[i:j]
            out.append(" " + ("\n" * chunk.count("\n")))
            i = j
            continue
        if text[i] in "\"'":
            quote = text[i]
            j = i + 1
            while j < n:
                if text[j] == "\\":
                    j += 2
                    continue
                if text[j] == "\n" or text[j] == quote:
                    if text[j] == quote:
                        j += 1
                    break
                j += 1
            chunk = text[i:j]
            out.append(" " + ("\n" * chunk.count("\n")))
            i = j
            continue
        out.append(text[i])
        i += 1
    return "".join(out)


def strip_preprocessor(text):
    lines = text.split("\n")
    out = []
    i = 0
    while i < len(lines):
        if lines[i].lstrip().startswith("#"):
            while True:
                continued = lines[i].rstrip().endswith("\\")
                out.append("")
                i += 1
                if not continued or i >= len(lines):
                    break
            continue
        out.append(lines[i])
        i += 1
    return "\n".join(out)


def tokenize(text):
    tokens = []
    i = 0
    n = len(text)
    line = 1
    while i < n:
        ch = text[i]
        if ch == "\n":
            line += 1
            i += 1
            continue
        if ch.isspace():
            i += 1
            continue
        if ch.isalpha() or ch == "_":
            j = i + 1
            while j < n and (text[j].isalnum() or text[j] == "_"):
                j += 1
            tokens.append(Tok(text[i:j], line))
            i = j
            continue
        if ch.isdigit() or (ch == "." and i + 1 < n and text[i + 1].isdigit()):
            j = i + 1
            while j < n and (text[j].isalnum() or text[j] in ".xXpP"):
                j += 1
            tokens.append(Tok(text[i:j], line))
            i = j
            continue
        matched = None
        for punct in MULTI_PUNCT:
            if text.startswith(punct, i):
                matched = punct
                break
        if matched:
            tokens.append(Tok(matched, line))
            i += len(matched)
            continue
        tokens.append(Tok(ch, line))
        i += 1
    return tokens


def consume_balanced(tokens, i, open_t, close_t):
    if i >= len(tokens) or tokens[i].text != open_t:
        return i
    depth = 0
    while i < len(tokens):
        text = tokens[i].text
        if text == open_t:
            depth += 1
        elif text == close_t:
            depth -= 1
            if depth == 0:
                return i + 1
        i += 1
    return i


def consume_attr(tokens, i):
    i += 1
    if i < len(tokens) and tokens[i].text == "(":
        i = consume_balanced(tokens, i, "(", ")")
    return i


def resync(tokens, i):
    """Skip a broken declaration through its semicolon or compound body."""
    start = i
    brace = paren = bracket = 0
    while i < len(tokens):
        text = tokens[i].text
        if text == "{":
            if brace == 0 and paren == 0 and bracket == 0 and i > start:
                return consume_balanced(tokens, i, "{", "}")
            brace += 1
        elif text == "}":
            if brace == 0 and paren == 0 and bracket == 0:
                return i + 1
            brace = max(0, brace - 1)
        elif text == "(":
            paren += 1
        elif text == ")":
            paren = max(0, paren - 1)
        elif text == "[":
            bracket += 1
        elif text == "]":
            bracket = max(0, bracket - 1)
        elif text == ";" and brace == 0 and paren == 0 and bracket == 0:
            return i + 1
        i += 1
    return i if i > start else start + 1


def paren_is_grouped_declarator(tokens, i):
    """True when '(' wraps a declarator rather than a parameter list."""
    if i >= len(tokens) or tokens[i].text != "(":
        return False
    j = i + 1
    if j >= len(tokens):
        return False
    if tokens[j].text in ("*", "("):
        return True
    if tokens[j].text in TYPE_WORDS or tokens[j].text in TYPE_INTRODUCERS:
        return False
    if tokens[j].text in QUAL or tokens[j].text in STORAGE:
        return False
    if tokens[j].text[:1].isalpha() or tokens[j].text.startswith("_"):
        k = j + 1
        return k < len(tokens) and tokens[k].text == ")"
    return False


def parse_specifiers(tokens, i, typedef_immutable):
    base_const = False
    is_typedef = False
    is_extern = False
    have_type = False
    n = len(tokens)
    while i < n:
        text = tokens[i].text
        if text in ATTRIBUTES:
            i = consume_attr(tokens, i)
            continue
        if text in ("typeof", "__typeof", "__typeof__"):
            i += 1
            if i < n and tokens[i].text == "(":
                i = consume_balanced(tokens, i, "(", ")")
            have_type = True
            continue
        if text in ("_Alignas", "alignas") and i + 1 < n and tokens[i + 1].text == "(":
            i = consume_balanced(tokens, i + 1, "(", ")")
            continue
        if text == "_Atomic" and i + 1 < n and tokens[i + 1].text == "(":
            i = consume_balanced(tokens, i + 1, "(", ")")
            have_type = True
            continue
        if text in STORAGE:
            if text == "typedef":
                is_typedef = True
            elif text == "extern":
                is_extern = True
            i += 1
            continue
        if text in QUAL or text in NOISE:
            if text in CONST_WORDS:
                base_const = True
            i += 1
            continue
        if text in TYPE_WORDS:
            have_type = True
            i += 1
            continue
        if text in TYPE_INTRODUCERS:
            i += 1
            if i < n and tokens[i].text not in NOT_NAMES and (
                    tokens[i].text[:1].isalpha() or tokens[i].text.startswith("_")):
                # A tag, unless the next token cannot be a tag.
                if tokens[i].text not in (";", "{", "*", ",", "=", "("):
                    i += 1
            if i < n and tokens[i].text == "{":
                i = consume_balanced(tokens, i, "{", "}")
            have_type = True
            continue
        if text[:1].isalpha() or text.startswith("_"):
            if have_type:
                break
            nxt = tokens[i + 1].text if i + 1 < n else ""
            if MACRO_NAME.match(text) and nxt == "(":
                break
            if nxt == "(":
                if paren_is_grouped_declarator(tokens, i + 1):
                    if typedef_immutable.get(text):
                        base_const = True
                    have_type = True
                    i += 1
                    continue
                break
            if nxt in QUAL or nxt == "*" or (
                    i + 1 < n and (tokens[i + 1].text[:1].isalpha()
                                   or tokens[i + 1].text.startswith("_"))):
                if typedef_immutable.get(text):
                    base_const = True
                have_type = True
                i += 1
                continue
            break
        break
    return i, base_const, is_typedef, is_extern, have_type


def parse_declarator(tokens, i):
    """Return (index, name, line, mods) with mods ordered closest-to-name first."""
    n = len(tokens)
    pointers = []
    while i < n and tokens[i].text == "*":
        i += 1
        pointed_const = False
        while i < n and (tokens[i].text in QUAL or tokens[i].text in NOISE
                         or tokens[i].text in ATTRIBUTES):
            if tokens[i].text in CONST_WORDS:
                pointed_const = True
            if tokens[i].text in ATTRIBUTES:
                i = consume_attr(tokens, i)
                continue
            i += 1
        pointers.append(pointed_const)

    name = None
    line = tokens[i].line if i < n else 1
    inner = []
    if i < n and tokens[i].text == "(":
        i += 1
        i, name, line, inner = parse_declarator(tokens, i)
        if i < n and tokens[i].text == ")":
            i += 1
    elif i < n and tokens[i].text not in NOT_NAMES and (
            tokens[i].text[:1].isalpha() or tokens[i].text.startswith("_")):
        name = tokens[i].text
        line = tokens[i].line
        i += 1

    suffixes = []
    while i < n and tokens[i].text in ("[", "("):
        if tokens[i].text == "[":
            i = consume_balanced(tokens, i, "[", "]")
            suffixes.append(("array",))
        else:
            i = consume_balanced(tokens, i, "(", ")")
            suffixes.append(("func",))
        while i < n and tokens[i].text in ATTRIBUTES:
            i = consume_attr(tokens, i)
    mods = inner + suffixes + [("ptr", flag) for flag in reversed(pointers)]
    return i, name, line, mods


def consume_initializer(tokens, i):
    brace = paren = bracket = 0
    while i < len(tokens):
        text = tokens[i].text
        if text == "{":
            brace += 1
        elif text == "}":
            if brace == 0:
                break
            brace -= 1
        elif text == "(":
            paren += 1
        elif text == ")":
            if paren == 0:
                break
            paren -= 1
        elif text == "[":
            bracket += 1
        elif text == "]":
            if bracket == 0:
                break
            bracket -= 1
        elif text in (",", ";") and brace == 0 and paren == 0 and bracket == 0:
            break
        i += 1
    return i


def parse_declarators(tokens, i, base_const, is_typedef, is_extern,
                      typedef_immutable):
    hits = []
    n = len(tokens)
    while i < n:
        while i < n and tokens[i].text in ATTRIBUTES:
            i = consume_attr(tokens, i)
        if i >= n or tokens[i].text == ";":
            return (i + 1 if i < n else i), hits
        if tokens[i].text == "{":
            return consume_balanced(tokens, i, "{", "}"), hits
        start = i
        i, name, line, mods = parse_declarator(tokens, i)
        if i == start and name is None:
            return resync(tokens, i), hits
        while i < n and (tokens[i].text in ATTRIBUTES or tokens[i].text in ASM_WORDS):
            if tokens[i].text in ASM_WORDS:
                i += 1
                if i < n and tokens[i].text == "(":
                    i = consume_balanced(tokens, i, "(", ")")
            else:
                i = consume_attr(tokens, i)
        had_init = False
        if i < n and tokens[i].text == "=":
            had_init = True
            i += 1
            i = consume_initializer(tokens, i)
        if name and name not in NOT_NAMES:
            typ = build_type(base_const, mods)
            if is_typedef:
                typedef_immutable[name] = not is_mutable(typ)
            elif not (is_extern and not had_init) and is_mutable(typ):
                hits.append((line, name))
        if i < n and tokens[i].text == "{":
            return consume_balanced(tokens, i, "{", "}"), hits
        if i < n and tokens[i].text == ",":
            i += 1
            continue
        if i < n and tokens[i].text == ";":
            return i + 1, hits
        return resync(tokens, i), hits
    return i, hits


def is_macro_call(tokens, i):
    return (i + 1 < len(tokens) and MACRO_NAME.match(tokens[i].text)
            and tokens[i + 1].text == "(")


def skip_macro(tokens, i):
    i += 1
    if i < len(tokens) and tokens[i].text == "(":
        i = consume_balanced(tokens, i, "(", ")")
    if i < len(tokens) and tokens[i].text == ";":
        i += 1
    return i


def scan_text(text):
    """Return (line, name) of mutable file-scope variables in one translation."""
    normalized = text.replace("\r\n", "\n").replace("\r", "\n")
    tokens = tokenize(strip_preprocessor(strip_comments_and_strings(normalized)))
    hits = []
    typedef_immutable = {}
    i = 0
    n = len(tokens)
    while i < n:
        if tokens[i].text in (";", "}"):
            i += 1
            continue
        if is_macro_call(tokens, i):
            i = skip_macro(tokens, i)
            continue
        if tokens[i].text in ASSERT_WORDS:
            i = resync(tokens, i)
            continue
        if tokens[i].text in ASM_WORDS:
            i = resync(tokens, i)
            continue
        start = i
        i, base_const, is_typedef, is_extern, have_type = parse_specifiers(
            tokens, i, typedef_immutable)
        if i == start and not have_type:
            i = resync(tokens, start)
            continue
        i, found = parse_declarators(
            tokens, i, base_const, is_typedef, is_extern, typedef_immutable)
        if i <= start:
            i = start + 1
            continue
        hits.extend(found)
    return hits


def self_check():
    cases = {
        "static int x;\n": [("x", 1)],
        "static const int x;\n": [],
        "extern int x;\n": [],
        "extern int x = 1;\n": [("x", 1)],
        "int foo(void);\n": [],
        "int foo(void) { static int x; }\n": [],
        "static const struct audio_ops *audio;\n": [("audio", 1)],
        "const char *name_at(int index)\n{\n"
        "\tstatic const char *const legacy[] = { \"hda\" };\n}\n": [],
        "static int slots[4];\nstatic int count;\n": [("slots", 1), ("count", 2)],
        "struct ip_header\n{\n\tint x;\n} __attribute__((packed));\n": [],
        "static uint8_t buf[2][8]\n\t__attribute__((aligned(16)));\n": [("buf", 1)],
        "ir0_spinlock_t tcp_lock;\n": [("tcp_lock", 1)],
        "typedef int (*handler_t)(int);\n": [],
        "static int (*fp)(void);\n": [("fp", 1)],
        "int (*tab[4])(void);\n": [("tab", 1)],
        "RESOURCE_OPS(pipe);\n": [],
        "static int a, *b, c[3];\n": [("a", 1), ("b", 1), ("c", 1)],
        "static const char msg[] = \"hi\";\n": [],
        "static char msg[] = \"hi;}\";\nstatic int t;\n": [("msg", 1), ("t", 2)],
        "enum { A = 1, B = 2 };\n": [],
        "struct s { int (*f)(int a, int b); } g;\n": [("g", 1)],
        "void f(void)\n{\n\tint x;\n}\n": [],
        "static int\n\tfoo;\n": [("foo", 2)],
        "int (*get(void))(int);\n": [],
        "static int (*const handler)(void);\n": [],
        "static const char *const msgs[];\n": [],
        "static const char *msgs[];\n": [("msgs", 1)],
        "static volatile int flag;\n": [("flag", 1)],
        "extern char name[];\n": [],
        "const char *_ktest_current_name;\n": [("_ktest_current_name", 1)],
        "int x = {1}, y = foo(2, 3);\n": [("x", 1), ("y", 1)],
        "/* static int hidden;\n */\nstatic int real;\n": [("real", 3)],
        "extern int x, y = 1;\n": [("y", 1)],
        "typedef const int cint;\ncint x;\ncint *p;\n": [("p", 3)],
        "static int /* c\n*/ foo;\n": [("foo", 2)],
    }
    failures = []
    for src, expected in cases.items():
        got = sorted((name, line) for line, name in scan_text(src))
        exp = sorted(expected)
        if got != exp:
            failures.append(f"got {got} expected {exp} for {src!r}")
    if failures:
        for failure in failures:
            print(failure, file=sys.stderr)
        raise SystemExit(1)


def iter_sources():
    for tree in TREES:
        base = ROOT / tree
        if not base.is_dir():
            continue
        for dirpath, dirnames, filenames in os.walk(base):
            dirnames[:] = [name for name in dirnames if name != ".git"]
            for filename in filenames:
                if os.path.splitext(filename)[1] in (".c", ".h"):
                    yield Path(dirpath) / filename


def main():
    self_check()
    rows = []
    for path in iter_sources():
        text = path.read_text(encoding="utf-8", errors="replace")
        rel = path.relative_to(ROOT).as_posix()
        for line, name in scan_text(text):
            rows.append((rel, name, line))
    rows.sort(key=lambda row: (row[0], row[2], row[1]))
    OUT.parent.mkdir(parents=True, exist_ok=True)
    with OUT.open("w", encoding="utf-8", newline="\n") as handle:
        for path, name, line in rows:
            handle.write(f"{path}\t{name}\t{line}\n")
    print(f"wrote {OUT.relative_to(ROOT).as_posix()} ({len(rows)} rows)")


if __name__ == "__main__":
    main()
