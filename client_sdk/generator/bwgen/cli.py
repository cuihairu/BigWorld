"""The bwgen command line (shared by ``bwgen.py`` and ``python3 -m bwgen``).

Usage:

    bwgen <res/scripts-dir> --out <dir> [--lang cs|ts|both]

``<res/scripts-dir>`` is a ``res/scripts`` directory (``entities.xml`` plus
``entity_defs/*.def``); the in-repo golden tree is
``programming/bigworld/examples/client_integration/python/simple/res/scripts``.
"""

from __future__ import annotations

import argparse
import os

from .defparse import load_tree
from .emit_cs import emit_entity as emit_cs
from .emit_ts import emit_entity as emit_ts


def generate(scripts_dir: str, out_dir: str, lang: str = "both"):
    """Generate all entities into out_dir/{cs,ts}. Returns the file list."""
    tree = load_tree(scripts_dir)
    written = []
    for entity in tree.entities:
        if lang in ("cs", "both"):
            path = os.path.join(out_dir, "cs", "%sBase.cs" % entity.name)
            os.makedirs(os.path.dirname(path), exist_ok=True)
            with open(path, "w") as handle:
                handle.write(emit_cs(entity))
            written.append(path)
        if lang in ("ts", "both"):
            path = os.path.join(out_dir, "ts", "%sBase.ts" % entity.name)
            os.makedirs(os.path.dirname(path), exist_ok=True)
            with open(path, "w") as handle:
                handle.write(emit_ts(entity))
            written.append(path)
    return written


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(
        description="BigWorld client SDK def-to-code generator (CL-3)")
    parser.add_argument("scripts_dir", help="a res/scripts directory")
    parser.add_argument("--out", required=True, help="output directory")
    parser.add_argument("--lang", choices=("cs", "ts", "both"), default="both")
    args = parser.parse_args(argv)

    for path in generate(args.scripts_dir, args.out, args.lang):
        print(path)
    return 0
