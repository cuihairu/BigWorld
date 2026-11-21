#!/usr/bin/env python3
"""Generate per-entity base classes for the client SDK runtimes.

Usage:

    python3 client_sdk/generator/bwgen.py <res/scripts-dir> --out <dir> \
        [--lang cs|ts|both]

``<res/scripts-dir>`` is a ``res/scripts`` directory (``entities.xml`` plus
``entity_defs/*.def``); the in-repo golden tree is
``programming/bigworld/examples/client_integration/python/simple/res/scripts``.

Batch-one scope, limitations and the type/id mapping: ``TYPEMAP.md`` next to
this file, and ``README.md`` for the acceptance path. Stdlib only.
"""

from __future__ import annotations

import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from bwgen.defparse import load_tree      # noqa: E402
from bwgen.emit_cs import emit_entity as emit_cs        # noqa: E402
from bwgen.emit_ts import emit_entity as emit_ts        # noqa: E402


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


if __name__ == "__main__":
    raise SystemExit(main())
