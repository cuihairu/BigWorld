"""Parsing for ``entities.xml`` and the ``entity_defs/*.def`` XML files.

The parser is deliberately tolerant of the whitespace-heavy BigWorld def
format (tabs, inline text after tags) and only cares about the structure the
generator needs: entity list, property definitions, and the three method
families. ``<Type>`` elements are resolved through
:func:`parse_type`, which understands the ``ARRAY <of> T </of>`` and
``USER_TYPE <implementedBy>...`` shapes.
"""

from __future__ import annotations

import os
import xml.etree.ElementTree as ET
from typing import List, Optional

from .model import DefTree, EntityDef, MethodDef, PropertyDef, TypeRef


def parse_type(type_elem: Optional[ET.Element]) -> TypeRef:
    """Parse a ``<Type>`` element into a TypeRef.

    The base name is the first whitespace-separated word of the element's
    text (``STRING``, ``ARRAY``, ``USER_TYPE`` ...). ``ARRAY`` takes its
    element type from an ``<of>`` (or ``<Element>``) child.
    """
    raw = type_elem.text if type_elem is not None else ""
    text = " ".join((raw or "").split())
    if not text:
        return TypeRef("UNMAPPED", None, "")

    base = text.split()[0]

    if base == "ARRAY":
        element_elem = None
        if type_elem is not None:
            element_elem = type_elem.find("of")
            if element_elem is None:
                element_elem = type_elem.find("Element")
        return TypeRef("ARRAY", parse_type(element_elem), text)

    return TypeRef(base, None, text)


def _parse_args(method_elem: ET.Element) -> List[PropertyDef]:
    args: List[PropertyDef] = []
    for arg_elem in method_elem.findall("Arg"):
        # <Arg> STRING </Arg> - the element text is the type name.
        args.append(PropertyDef(name="", type_ref=parse_type(arg_elem)))
    return args


def _parse_property(name: str, prop_elem: ET.Element) -> PropertyDef:
    type_elem = prop_elem.find("Type")
    flags_elem = prop_elem.find("Flags")
    persistent = (prop_elem.find("Persistent") is not None) or (
        (prop_elem.findtext("Persistent") or "").strip().lower() == "true")
    identifier = prop_elem.find("Identifier") is not None or (
        (prop_elem.findtext("Identifier") or "").strip().lower() == "true")

    return PropertyDef(
        name=name,
        type_ref=parse_type(type_elem),
        flags=(flags_elem.text or "").strip() if flags_elem is not None else "",
        persistent=persistent,
        identifier=identifier,
        default=(prop_elem.findtext("Default") or "").strip(),
    )


def parse_def(path: str, entity_name: str, client_server: bool = True) -> EntityDef:
    """Parse one ``*.def`` file."""
    tree = ET.parse(path)
    root = tree.getroot()

    entity = EntityDef(name=entity_name, client_server=client_server,
        source=path)

    props_elem = root.find("Properties")
    if props_elem is not None:
        for child in props_elem:
            entity.properties.append(_parse_property(child.tag, child))

    def parse_methods(tag: str, component: str, exposed: bool) -> List[MethodDef]:
        methods: List[MethodDef] = []
        section = root.find(tag)
        if section is None:
            return methods
        for child in section:
            method_exposed = exposed or child.find("Exposed") is not None
            methods.append(MethodDef(
                name=child.tag,
                component=component,
                args=_parse_args(child),
                exposed=method_exposed,
            ))
        return methods

    entity.client_methods = parse_methods("ClientMethods", "client", True)
    entity.cell_methods = parse_methods("CellMethods", "cell", False)
    entity.base_methods = parse_methods("BaseMethods", "base", False)

    return entity


def load_tree(scripts_dir: str) -> DefTree:
    """Parse ``<scripts_dir>/entities.xml`` and the def files it names.

    ``scripts_dir`` is a ``res/scripts`` directory: it holds ``entities.xml``
    (listing entity names under ``ClientServerEntities`` /
    ``ServerOnlyEntities``) and the ``entity_defs/`` subdirectory.
    """
    entities_xml = os.path.join(scripts_dir, "entities.xml")
    tree = DefTree(entities_xml=entities_xml)
    root = ET.parse(entities_xml).getroot()

    def_defs = os.path.join(scripts_dir, "entity_defs")
    for section, client_server in (("ClientServerEntities", True),
                                   ("ServerOnlyEntities", False)):
        section_elem = root.find(section)
        if section_elem is None:
            continue
        for child in section_elem:
            name = child.tag
            if not name:
                continue
            def_path = os.path.join(def_defs, "%s.def" % name)
            tree.entities.append(
                parse_def(def_path, name, client_server))

    return tree
