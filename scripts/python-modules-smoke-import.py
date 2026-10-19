#!/usr/bin/env python3
"""Import smoke test for the vendored third-party Python modules.

The vendored trees under programming/bigworld/third_party/python_modules are
Py3-compatible source releases (see todo.md: R2 python_modules).  They are
installed into the res-tree site-packages by
build/make/third_party_python_modules.mak, which the make default build pulls
in through BW_MISC.

Two phases, so the test proves both halves of the delivery:

  1. tarballs  - extract each vendored sdist and import the package straight
                 from the source tree, proving the set is coherent Py3
                 releases independent of any build step.
  2. installed - if the res-tree site-packages directories exist (i.e. the
                 build has run), import the same packages out of them,
                 proving the make install step produced an importable tree.
                 This phase is skipped with a note when the build has not
                 been run.

Run with the embedded interpreter so the versions match what the server uses:

    programming/bigworld/build/el7/third_party/python/python.exe \
        scripts/python-modules-smoke-import.py

Exits non-zero if any package fails to import.
"""

import os
import shutil
import sys
import tarfile

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(REPO_ROOT, "programming", "bigworld", "third_party", "python_modules")
# Extraction scratch dir. Lives under the build tree (on the source partition)
# rather than /tmp, which may carry a per-user quota.
SCRATCH = os.path.join(REPO_ROOT, "programming", "bigworld", "build", "python_modules_smoke")

# Res-tree install destinations (build/make/third_party_python_modules.mak).
COMMON_SITE = os.path.join(
    REPO_ROOT, "game", "res", "bigworld", "scripts", "common", "site-packages"
)
SERVER_COMMON_SITE = os.path.join(
    REPO_ROOT, "game", "res", "bigworld", "scripts", "server_common", "site-packages"
)

# Each entry: (tarball basename, directory inside the extraction that holds
# the importable code, module name to import).
#
# The root directory varies by packaging style: most projects ship the package
# at the top level, SQLAlchemy nests it under lib/, and the src-layout ones
# (twisted, zope.interface, attrs, automat, hyperlink, incremental,
# typing_extensions) nest it under src/.
MODULES = [
    ("setuptools-75.3.0.tar.gz", "", "pkg_resources"),
    ("typing_extensions-4.12.2.tar.gz", "src", "typing_extensions"),
    ("attrs-24.2.0.tar.gz", "src", "attr"),
    ("constantly-23.10.4.tar.gz", "", "constantly"),
    ("incremental-24.7.0.tar.gz", "src", "incremental"),
    ("automat-24.8.1.tar.gz", "src", "automat"),
    ("idna-3.10.tar.gz", "", "idna"),
    ("hyperlink-21.0.0.tar.gz", "src", "hyperlink"),
    ("zope.interface-7.0.tar.gz", "src", "zope.interface"),
    ("pika-1.3.2.tar.gz", "", "pika"),
    ("msgpack-1.1.0.tar.gz", "", "msgpack"),
    ("Pympler-1.0.tar.gz", "", "pympler"),
    ("sqlalchemy-1.4.54.tar.gz", "lib", "sqlalchemy"),
    ("twisted-24.7.0.tar.gz", "src", "twisted"),
]

# Modules imported in the installed phase, in dependency order so a failure
# names the missing piece rather than a downstream consumer of it.
INSTALLED_MODULES = [
    "pkg_resources",
    "setuptools",
    "typing_extensions",
    "attr",
    "constantly",
    "incremental",
    "automat",
    "idna",
    "hyperlink",
    "pympler",
    "pika",
    "msgpack",
    "sqlalchemy",
    "zope.interface",
    "twisted",
]


def _version_of(mod):
    """Best-effort __version__. Some packages (e.g. attr) define a module
    __getattr__ that raises when the distribution metadata is absent, so a
    plain getattr() is not enough."""
    try:
        return str(getattr(mod, "__version__", ""))
    except Exception:  # noqa: BLE001 - version is cosmetic, never fatal
        return "?"


def import_from(paths, modules):
    """Import each module with paths prepended, printing one line each.

    Returns the number of failures.
    """
    old_path = list(sys.path)
    for path in reversed(paths):
        sys.path.insert(0, path)
    failures = 0
    try:
        for module in modules:
            try:
                mod = __import__(module)
            except Exception as exc:  # noqa: BLE001 - report and keep going
                print("FAIL %-18s %s: %s" % (module, type(exc).__name__, exc))
                failures += 1
            else:
                print("OK   %-18s %s" % (module, _version_of(mod)))
    finally:
        sys.path[:] = old_path
    return failures


def main():
    if not os.path.isdir(SRC):
        print("ERROR: source tree not found: %s" % SRC)
        return 2

    failures = 0
    os.makedirs(SCRATCH, exist_ok=True)
    try:
        print("=== phase 1: vendored tarballs ===")
        for tarball, root, module in MODULES:
            path = os.path.join(SRC, tarball)
            if not os.path.isfile(path):
                print("FAIL %-20s missing tarball %s" % (module, tarball))
                failures += 1
                continue
            dest = os.path.join(SCRATCH, os.path.basename(tarball))
            shutil.rmtree(dest, ignore_errors=True)
            os.makedirs(dest, exist_ok=True)
            with tarfile.open(path) as tf:
                # PyPI sdists wrap everything in a top-level directory named
                # <name>-<version>; record it so the import root can be built.
                top = tf.getnames()[0].split("/")[0]
                tf.extractall(dest, filter="tar")
            import_root = (
                os.path.join(dest, top, root) if root else os.path.join(dest, top)
            )
            failures += import_from([import_root], [module])

        print("=== phase 2: installed site-packages ===")
        if not (os.path.isdir(COMMON_SITE) or os.path.isdir(SERVER_COMMON_SITE)):
            print(
                "SKIP installed phase: %s does not exist - run the make build "
                "first" % os.path.relpath(COMMON_SITE, REPO_ROOT)
            )
        else:
            failures += import_from([SERVER_COMMON_SITE, COMMON_SITE], INSTALLED_MODULES)
    finally:
        shutil.rmtree(SCRATCH, ignore_errors=True)

    print("%d packages, %d failed" % (len(MODULES), failures))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
