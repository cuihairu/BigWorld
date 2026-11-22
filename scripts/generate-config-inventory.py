#!/usr/bin/env python3
"""Generate the server config switch inventory (DOC-GIF offline half).

Enumerates every config option declared by the server's typed config classes
(``*_config.hpp`` declarations + ``*_config.cpp`` BW_OPTION_* definitions) and
emits a markdown table: option name, type, default, xml path, and source
location. Bool options are the "switches" (off/on evidence is the blocked half
of DOC-GIF, needing a live cluster); numeric/string options are listed for
completeness.

Re-runnable: deterministic over the config surface, run from the repo root.
Output is written to docs/review/config-switch-inventory.md.
"""

import os
import re
import sys
from collections import OrderedDict

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BW = os.path.join(REPO_ROOT, "programming", "bigworld")
OUT = os.path.join(REPO_ROOT, "docs", "review", "config-switch-inventory.md")

# config classes we enumerate: (hpp, cpp) pairs under these roots
SEARCH_ROOTS = [
    os.path.join(BW, "lib", "server"),
    os.path.join(BW, "server"),
]

# Strip // and /* */ comments, preserving newlines and not touching // inside
# double-quoted strings (config sources rarely have quoted // but be safe).
def strip_comments(text):
    out = []
    i = 0
    n = len(text)
    in_str = False
    while i < n:
        c = text[i]
        if in_str:
            out.append(c)
            if c == '"' and (i == 0 or text[i - 1] != "\\"):
                in_str = False
            i += 1
            continue
        if c == '"':
            in_str = True
            out.append(c)
            i += 1
            continue
        if c == "/" and i + 1 < n and text[i + 1] == "/":
            while i < n and text[i] != "\n":
                i += 1
            continue
        if c == "/" and i + 1 < n and text[i + 1] == "*":
            i += 2
            while i + 1 < n and not (text[i] == "*" and text[i + 1] == "/"):
                i += 1
            i += 2
            continue
        out.append(c)
        i += 1
    return "".join(out)


# Join C-preprocessor continuation lines (trailing backslash).
def join_continuations(text):
    return re.sub(r"\\\s*\n", " ", text)


MACRO_RE = re.compile(
    r"\b(BW_OPTION(?:FULL)?(?:_RO)?_AT|BW_OPTION(?:FULL)?_RO|BW_OPTION_FULL|"
    r"BW_OPTION_SETTER(?:_AT)?|BW_OPTION_RO|BW_OPTION_AT|BW_OPTION|"
    r"DERIVED_BW_OPTION(?:_FULL)?)\s*\("
)


def split_args(s):
    """Split macro args on top-level commas, respecting parens/strings."""
    args = []
    depth = 0
    cur = []
    i = 0
    n = len(s)
    in_str = False
    while i < n:
        c = s[i]
        if in_str:
            cur.append(c)
            if c == '"' and (i == 0 or s[i - 1] != "\\"):
                in_str = False
            i += 1
            continue
        if c == '"':
            in_str = True
            cur.append(c)
            i += 1
            continue
        if c in "([{":
            depth += 1
            cur.append(c)
            i += 1
            continue
        if c in ")]}":
            depth -= 1
            cur.append(c)
            i += 1
            continue
        if c == "," and depth == 0:
            args.append("".join(cur).strip())
            cur = []
            i += 1
            continue
        cur.append(c)
        i += 1
    args.append("".join(cur).strip())
    return args


def find_macro_calls(body):
    """Yield (macro_name, args) for each top-level BW_OPTION* invocation."""
    for m in MACRO_RE.finditer(body):
        name = m.group(1)
        start = m.end()
        # find the closing paren that balances this call's first '('; the
        # opening '(' is consumed by m.end(), so start the depth at 1
        depth = 1
        i = start
        n = len(body)
        in_str = False
        while i < n:
            c = body[i]
            if in_str:
                if c == '"' and body[i - 1] != "\\":
                    in_str = False
                i += 1
                continue
            if c == '"':
                in_str = True
                i += 1
                continue
            if c == "(":
                depth += 1
            elif c == ")":
                depth -= 1
                if depth == 0:
                    break
            i += 1
        args = split_args(body[start:i])
        yield name, args


def parse_hpp_decls(hpp_text):
    """Return {name: (class_name, type)} from `static XxxOption<T> name;`."""
    decls = {}
    cls = None
    for line in hpp_text.splitlines():
        cm = re.match(r"class\s+(\w+Config)\b", line)
        if cm:
            cls = cm.group(1)
        dm = re.match(
            r"\s*static\s+(\w*Option(?:GetSet)?)\s*<\s*([^>]+)\s*>\s+(\w+)\s*;",
            line,
        )
        if dm and cls:
            decls[dm.group(3)] = (cls, dm.group(2).strip())
    return decls


def parse_cpp_defs(cpp_text):
    """Return {name: (default, config_path, macro_name)} from BW_OPTION_*."""
    defs = {}
    prefix = None
    for m in re.finditer(r'#define\s+BW_CONFIG_PREFIX\s+"([^"]*)"', cpp_text):
        prefix = m.group(1)
        break
    for macro_name, args in find_macro_calls(cpp_text):
        if len(args) < 2:
            continue
        typ = args[0]
        name = args[1]
        value = None
        path = None
        if macro_name in ("BW_OPTION", "BW_OPTION_RO"):
            value = args[2] if len(args) > 2 else None
            path = (prefix or "") + name
        elif macro_name in ("BW_OPTION_AT", "BW_OPTION_RO_AT"):
            value = args[2] if len(args) > 2 else None
            path = (args[3].strip('"') + name) if len(args) > 3 else (prefix or "") + name
        elif macro_name in ("BW_OPTION_FULL", "BW_OPTION_FULL_RO"):
            value = args[2] if len(args) > 2 else None
            path = args[3].strip('"') if len(args) > 3 else (prefix or "") + name
        elif macro_name in ("BW_OPTION_SETTER", "BW_OPTION_SETTER_AT"):
            value = "(getter)"  # setter-driven; no simple default
            path = (prefix or "") + name
            if macro_name == "BW_OPTION_SETTER_AT" and len(args) > 4:
                path = args[4] + name
        else:  # DERIVED_BW_OPTION / DERIVED_BW_OPTION_FULL
            value = "(default-constructed)"
            path = ""
        defs[name] = (value, path, macro_name)

    # Explicit (non-macro) initializers, e.g.
    #   ServerAppOption< bool >\n\tServerAppConfig::isProduction( true, "production", ... );
    # value is the first arg, the config path is the (possibly macro-prefixed)
    # second arg which is usually a quoted string.
    explicit_re = re.compile(
        r"ServerAppOption(?:GetSet)?\s*<\s*[^>]+?\s*>\s*\n?\s*(\w+)::(\w+)\s*"
        r"\(\s*([^,]+?)\s*,\s*(?:BW_OPTION_CONFIG_DIR\s*)?\"([^\"]*)\"",
        re.MULTILINE,
    )
    for m in explicit_re.finditer(cpp_text):
        name, value, path = m.group(2), m.group(3).strip(), m.group(4)
        defs.setdefault(name, (value, path, "explicit"))

    return defs, prefix


def main():
    rows = []  # (class, prefix, name, type, default, path, macro, hpp_file, cpp_file, line)
    files = []
    for root in SEARCH_ROOTS:
        for dirpath, _dirs, fnames in os.walk(root):
            for fn in fnames:
                if fn.endswith("_config.hpp"):
                    files.append(os.path.join(dirpath, fn))

    for hpp in sorted(files):
        cpp = hpp[: -len(".hpp")] + ".cpp"
        if not os.path.exists(cpp):
            continue
        hpp_text = strip_comments(open(hpp, encoding="utf-8").read())
        cpp_text = strip_comments(open(cpp, encoding="utf-8").read())
        cpp_text = join_continuations(cpp_text)

        decls = parse_hpp_decls(hpp_text)
        defs, prefix = parse_cpp_defs(cpp_text)

        # line numbers for hpp decls
        hpp_lines = {}
        for ln, line in enumerate(hpp_text.splitlines(), 1):
            dm = re.match(r"\s*static\s+\w*Option(?:GetSet)?\s*<\s*[^>]+\s*>\s+(\w+)\s*;", line)
            if dm:
                hpp_lines[dm.group(1)] = ln

        for name, (cls, typ) in decls.items():
            value, path, macro = defs.get(name, (None, None, None))
            if value is None:
                # declared but defined via a mechanism we did not capture; still list
                value = "(see cpp)"
                macro = macro or "?"
            rows.append(
                (cls, prefix, name, typ, value, path, macro,
                 os.path.relpath(hpp, REPO_ROOT), hpp_lines.get(name))
            )

    rows.sort(key=lambda r: (r[0], r[2]))
    switches = [r for r in rows if r[3] == "bool"]
    others = [r for r in rows if r[3] != "bool"]

    lines = []
    lines.append("# Config switch inventory")
    lines.append("")
    lines.append(
        "Generated by `scripts/generate-config-inventory.py` (re-runnable, "
        "deterministic). Enumerates the server's typed config options; bool "
        "options are the off/on switches of DOC-GIF. The per-switch off/on "
        "evidence (log diff + GIF) is captured by "
        "`scripts/config-switch-evidence.py` and published in the "
        "[config effect page](config-effect/index.md)."
    )
    lines.append("")
    lines.append(
        "Total options: %d (bool switches: %d, other: %d)." %
        (len(rows), len(switches), len(others))
    )
    lines.append("")

    def table(header, data):
        lines.append("| " + " | ".join(header) + " |")
        lines.append("|" + "|".join(["---"] * len(header)) + "|")
        for r in data:
            lines.append("| " + " | ".join(str(x) for x in r) + " |")
        lines.append("")

    lines.append("## Switches (bool)")
    lines.append("")
    table(
        ["class", "name", "default", "xml path", "declared at"],
        [
            (r[0], r[2], r[4], r[5] or "(watcher-only)", "%s:%s" % (r[7], r[8]))
            for r in switches
        ],
    )
    lines.append("## Other options (non-bool)")
    lines.append("")
    table(
        ["class", "name", "type", "default", "xml path"],
        [
            (r[0], r[2], r[3], r[4], r[5] or "(watcher-only)")
            for r in others
        ],
    )
    lines.append("## All options by file")
    lines.append("")
    table(
        ["class", "name", "type", "default", "macro", "source"],
        [
            (r[0], r[2], r[3], r[4], r[6], "%s:%s" % (r[7], r[8]))
            for r in rows
        ],
    )

    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")
    print("wrote %s (%d options, %d bool switches)" % (OUT, len(rows), len(switches)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
