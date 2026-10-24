#!/usr/bin/env python3
"""Verify that every internal markdown link under docs/ resolves to a real file.

VitePress serves docs/ as the site root and mirrors the locale trees
(root = English, /zh/ = Chinese), so an absolute link "/foo" resolves to
docs/foo and "/zh/foo" resolves to docs/zh/foo. A link is a dead link when
none of the candidates VitePress would try exists on disk.

Usage:  python3 scripts/check-docs-links.py [docs]
Exit:   0 when no dead links, 1 otherwise.
"""

import re
import sys
from pathlib import Path
from urllib.parse import unquote

ASSETS = frozenset({
    ".png", ".jpg", ".jpeg", ".gif", ".svg", ".webp", ".ico", ".pdf",
    ".woff", ".woff2", ".ttf", ".eot", ".mp4", ".webm", ".mp3", ".ogg",
})
PAGES = (".html", ".md", ".mdx")
INLINE = re.compile(r"(?<!!)\[[^\]]*\]\(([^)\s]+)(?:\s+\"[^\"]*\")?\)")
IMAGE = re.compile(r"!\[[^\]]*\]\(([^)\s]+)(?:\s+\"[^\"]*\")?\)")
REF = re.compile(r"^\s*\[[^\]]+\]:\s*<?([^>\s]+)>?(?:\s+[^\n]*)?$")
EXTERNAL = re.compile(r"^[a-zA-Z][a-zA-Z0-9+.-]*:")


def targets(markdown: str):
    """Every link destination in the document, reference definitions included."""
    for rx in (INLINE, IMAGE):
        for m in rx.finditer(markdown):
            yield m.group(1)
    for line in markdown.splitlines():
        m = REF.match(line)
        if m:
            yield m.group(1)


def candidate_paths(base: Path, doc: Path, raw: str):
    """Files VitePress would try for this destination, in order."""
    dest = unquote(raw.split("?", 1)[0].split("#", 1)[0].strip())
    if not dest or dest.startswith("#") or dest.startswith("//") \
            or EXTERNAL.match(dest):
        return []

    # Absolute links are rooted at the docs site root, not at the file.
    root = base if dest.startswith("/") else doc.parent
    rel = dest.lstrip("/")

    full = root / rel
    if full.suffix in ASSETS:
        return [full, base / "public" / rel]

    out = [full] if full.suffix in PAGES else []
    out += [full.with_suffix(".md"), full / "index.md", full.with_suffix(".html")]
    return out


def main():
    base = Path(sys.argv[1] if len(sys.argv) > 1 else "docs").resolve()
    if not base.is_dir():
        print(f"not a docs directory: {base}")
        return 1

    dead = []
    checked = 0
    for doc in sorted(base.rglob("*.md")):
        if ".vitepress" in doc.parts:
            continue
        text = doc.read_text(encoding="utf-8", errors="replace")
        for raw in targets(text):
            cands = candidate_paths(base, doc, raw)
            if not cands:
                continue
            checked += 1
            if not any(c.is_file() for c in cands):
                dead.append((str(doc.relative_to(base)), raw,
                             ", ".join(str(c.relative_to(base)) for c in cands)))

    for doc, raw, cands in dead:
        print(f"DEAD  {doc} -> {raw}  (tried: {cands})")
    print(f"{checked} internal links checked, {len(dead)} dead")
    return 1 if dead else 0


if __name__ == "__main__":
    sys.exit(main())
