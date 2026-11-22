"""BigWorld client SDK def-to-code generator (CL-3).

A self-contained (stdlib-only) analogue of kbengine's ``kbcmd``: it reads a
``res/scripts/entities.xml`` + ``res/scripts/entity_defs/*.def`` tree and emits
per-entity generated base classes for the CL-1 (C#/Unity) and CL-2
(TypeScript) plugin runtimes. See ``client_sdk/generator/README.md`` for the
scope and the acceptance path.
"""

__version__ = "1.0.0"
