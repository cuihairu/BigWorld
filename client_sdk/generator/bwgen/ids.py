"""Wire id numbering for exposed methods and client-server properties.

Two facts are replicated from the server:

* **Exposed method ids** - there is ONE id space per entity, shared by
  client methods (server->client) and exposed base/cell methods
  (client->server). ``EntityMethodDescriptions::init`` appends every exposed
  method to one joint ``exposedMethods_`` list (every ClientMethod is
  exposed; base/cell need ``<Exposed/>``), and ``setExposedMsgIDs``
  ``stable_sort``s the joint list with ``ExposedMethodsSortHelper``
  (entity_method_descriptions.cpp:202). The comparator puts fixed-size
  messages first in ascending byte size, then variable-size ones ascending by
  the magnitude of their (negative) stream size - which is the
  variable-length header size, defaulting to 1 for every method without a
  ``VariableLengthHeaderSize`` override. The position in that sorted list is
  the exposed msg id the wire carries both ways.
* **Client-server property ids** - ``clientServerProperties_`` is filled
  from the CLIENT-flagged properties and ``allocateClientServerFullIndexes``
  sorts it with the same fixed-ascending/variable-ascending rule
  (entity_description.cpp), so the per-entity property update stream indexes
  follow the same order.

Both sorts are **stable**, so methods/properties that compare equal keep
their pre-sort order. For methods that order is the server's parse order:
BaseMethods, CellMethods, ClientMethods, def-file order within each.
"""

from __future__ import annotations

from typing import Dict, List

from .model import EntityDef, MethodDef, PropertyDef
from .typemap import ResolvedType, resolve

# Property flags that make a property visible to a client. BASE / CELL /
# CELL_PRIVATE / BASE_AND_CELL are server-side only.
CLIENT_FLAGS = {
    "BASE_AND_CLIENT",
    "CELL_AND_CLIENT",
    "ALL_CLIENTS",
    "OWN_CLIENT",
    "OTHER_CLIENTS",
}


def _sort_key(stream_size: int):
    # (category, size): fixed members first ascending, then variable members
    # ascending by header-size magnitude. Stable sort keeps def order on ties.
    if stream_size >= 0:
        return (0, stream_size)
    return (1, -stream_size)


def client_properties(entity: EntityDef) -> List[PropertyDef]:
    """The CLIENT-flagged properties in server (sorted) order."""
    client_props = [p for p in entity.properties if p.flags in CLIENT_FLAGS]
    sized = []
    for prop in client_props:
        resolved = resolve(prop.type_ref)
        sized.append((_sort_key(resolved.stream_size), prop))
    sized.sort(key=lambda pair: pair[0])
    return [prop for _, prop in sized]


def client_property_ids(entity: EntityDef) -> Dict[str, int]:
    """Property name -> client-server property id (sorted order)."""
    return {prop.name: index
            for index, prop in enumerate(client_properties(entity))}


def _method_stream_size(method: MethodDef) -> int:
    """The MethodDescription::streamSize(true) sort key.

    A variable arg list (or any exposed method needing a sub-message id)
    sorts as the negative variable-length header size, -1 by default
    (Mercury::DEFAULT_VARIABLE_LENGTH_HEADER_SIZE == 1).
    """
    total = 0
    variable = False
    for arg in method.args:
        resolved = resolve(arg.type_ref)
        if resolved.variable:
            variable = True
        else:
            total += resolved.wire_size
    return -1 if variable else total


def exposed_methods(entity: EntityDef) -> List[MethodDef]:
    """Every exposed method in the server's pre-sort order.

    The server parses the def families in the order BaseMethods, CellMethods,
    ClientMethods (entity_description.cpp:559-572) and appends each exposed
    method to one joint ``exposedMethods_`` list
    (entity_method_descriptions.cpp:95-98); every ClientMethod is exposed.
    A method name shared across families with the same signature collapses to
    one ``internalMethods_`` entry (and one exposed entry), so this list is
    deduped by name - first occurrence wins, matching the server's parse
    order.
    """
    by_name = {}
    ordered: List[MethodDef] = []
    for method in (entity.base_methods + entity.cell_methods +
                   entity.client_methods):
        if not (method.exposed or method.is_client_method):
            continue
        if method.name in by_name:
            continue
        by_name[method.name] = method
        ordered.append(method)
    return ordered


def joint_exposed_ids(entity: EntityDef) -> Dict[str, int]:
    """Exposed method name -> exposed id over the whole entity.

    ``EntityMethodDescriptions::setExposedMsgIDs`` stable-sorts the joint
    ``exposedMethods_`` list (all families) and the sorted position IS the
    exposed msg id the wire carries - both for the client->server dispatch of
    exposed base/cell calls and for the server->client delivery of client
    methods (Witness::sendToClient forwards ``description.exposedMsgID()``).
    """
    sized = [(_sort_key(_method_stream_size(method)), method)
             for method in exposed_methods(entity)]
    sized.sort(key=lambda pair: pair[0])
    return {method.name: index
            for index, (_, method) in enumerate(sized)}


def client_method_ids(entity: EntityDef) -> Dict[str, int]:
    """Client method name -> exposed method id (the joint id space)."""
    joint = joint_exposed_ids(entity)
    return {method.name: joint[method.name]
            for method in entity.client_methods}


def exposed_method_ids(methods: List[MethodDef],
        entity: EntityDef) -> Dict[str, int]:
    """One family's exposed method name -> the joint exposed id."""
    joint = joint_exposed_ids(entity)
    return {method.name: joint[method.name]
            for method in methods
            if (method.exposed or method.is_client_method)
            and method.name in joint}
