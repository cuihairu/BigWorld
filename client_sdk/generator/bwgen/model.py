"""The def model the emitters consume.

Everything is plain data: an :class:`EntityDef` per parsed ``*.def`` file,
holding properties and the three method families (ClientMethods,
CellMethods, BaseMethods) with their parsed ``<Type>`` references. The
wire-level facts (fixed byte size vs variable) live in
:mod:`bwgen.typemap`; the id numbering lives in :mod:`bwgen.ids`.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import List, Optional


class TypeRef:
    """One parsed ``<Type>`` reference.

    ``base`` is the canonical BigWorld type name (``STRING``, ``INT32``,
    ``ARRAY``, ``USER_TYPE`` ...). ``element`` is the element type for
    ``ARRAY``. Anything :func:`bwgen.typemap.resolve` cannot map keeps
    ``supported=False`` and is emitted as an opaque TODO (its id slot is
    still reserved so the numbering matches the server).
    """

    def __init__(self, base: str, element: Optional["TypeRef"] = None,
            raw: str = ""):
        self.base = base
        self.element = element
        self.raw = raw.strip()

    def __repr__(self) -> str:  # pragma: no cover - debugging aid
        if self.element is not None:
            return "ARRAY<%r>" % (self.element,)
        return self.base


@dataclass
class PropertyDef:
    """One ``<Properties>`` entry."""

    name: str
    type_ref: TypeRef
    flags: str = ""          # e.g. BASE_AND_CLIENT, ALL_CLIENTS, CELL_PRIVATE
    persistent: bool = False
    identifier: bool = False
    default: str = ""


@dataclass
class MethodDef:
    """One method of any of the three families.

    ``component`` is ``"client"``, ``"cell"`` or ``"base"``; ``exposed`` is
    True when the def carries ``<Exposed/>`` (implicit for ClientMethods,
    where every method is exposed to the client).
    """

    name: str
    component: str
    args: List[PropertyDef] = field(default_factory=list)
    exposed: bool = False
    return_values: List[PropertyDef] = field(default_factory=list)

    @property
    def is_client_method(self) -> bool:
        return self.component == "client"


@dataclass
class EntityDef:
    """One entity parsed from ``entity_defs/<Name>.def``."""

    name: str
    client_server: bool = True   # declared under ClientServerEntities
    properties: List[PropertyDef] = field(default_factory=list)
    client_methods: List[MethodDef] = field(default_factory=list)
    cell_methods: List[MethodDef] = field(default_factory=list)
    base_methods: List[MethodDef] = field(default_factory=list)
    source: str = ""            # the .def path, relative to the repo root

    def all_methods(self) -> List[MethodDef]:
        return self.client_methods + self.cell_methods + self.base_methods


@dataclass
class DefTree:
    """The parsed ``entities.xml`` + def file collection."""

    entities: List[EntityDef] = field(default_factory=list)
    entities_xml: str = ""
