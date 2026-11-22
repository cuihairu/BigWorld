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

The CLI lives in the package (:mod:`bwgen.cli`) so the wheel install of
``bwgen`` exposes the same ``bwgen`` console script; this file keeps the
documented script invocation working (``python3 client_sdk/generator/bwgen.py``).
"""

from __future__ import annotations

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from bwgen.cli import main  # noqa: E402

if __name__ == "__main__":
    raise SystemExit(main())
