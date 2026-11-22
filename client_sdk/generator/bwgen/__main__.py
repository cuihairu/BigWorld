"""``python3 -m bwgen`` entry point, delegating to :mod:`bwgen.cli`."""

from bwgen.cli import main

if __name__ == "__main__":
    raise SystemExit(main())
