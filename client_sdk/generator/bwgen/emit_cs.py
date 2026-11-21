"""C# emitter for the CL-1 (Unity / netstandard2.1) runtime.

Each entity emits one ``<Entity>Base.cs`` file holding two classes:

* ``<Entity>Base`` - property id constants, typed properties, the
  ``ReadClientData`` initial-snapshot decoder, the server-to-client
  ``OnXxx`` hooks plus the ``InvokeClientMethod`` dispatcher.
* ``EntityCall<Entity>Base`` - the client-to-server call stubs for the
  exposed base/cell methods (kbcmd legacy name:
  ``EntityBaseEntityCall_<Entity>Base``).

The generated code references only ``BigWorld.ClientSdk.Protocol`` and
``BigWorld.ClientSdk.Streams`` (CL-1). See the module doc in ``ids.py`` for
why the id constants are the size-sorted order.
"""

from __future__ import annotations

from typing import List

from .ids import client_method_ids, client_properties, exposed_method_ids
from .model import EntityDef, MethodDef, PropertyDef
from .naming import to_pascal
from .typemap import ResolvedType, resolve

NAMESPACE = "BigWorld.ClientSdk.Generated"


def _arg_name(index: int) -> str:
    return "value%d" % index


def _method_id_const(name: str) -> str:
    return "MethodId%s" % to_pascal(name)


def _property_id_const(name: str) -> str:
    return "PropertyId%s" % to_pascal(name)


def _read_expr(resolved: ResolvedType, reader: str) -> str:
    """A C# expression that reads one value of the type."""
    if resolved.base == "BLOB":
        # BigWorld blob: packed-int length + raw bytes (appendBlob).
        return "%s.ReadBytesRaw(%s.ReadPackedInt())" % (reader, reader)
    rw = resolved.rw
    if rw is not None:
        return "%s.%s()" % (reader, rw[0])
    if resolved.base in ("VECTOR2", "VECTOR3", "VECTOR4"):
        n = {"VECTOR2": 2, "VECTOR3": 3, "VECTOR4": 4}[resolved.base]
        return "new[] { %s }" % ", ".join(
            ["%s.ReadFloat()" % reader] * n)
    if resolved.base == "ARRAY":
        return None   # statement form only
    return None


def _emit_read_arg_stmts(resolved: ResolvedType, reader: str, arg_name: str,
        arg_index: int, lines: List[str], indent: str) -> None:
    """Declare and read one method argument into ``arg_name``.

    ARRAY is statement-form (packed length then a loop) because it has no
    single-expression reader; everything else is one declaration.
    """
    if resolved.base == "ARRAY":
        elem = resolved.element
        count = "_count%d" % arg_index
        lines.append("%sint %s = %s.ReadPackedInt();" % (indent, count, reader))
        lines.append("%s%s[] %s = new %s[%s];" % (
            indent, elem.cs, arg_name, elem.cs, count))
        lines.append("%sfor (int i = 0; i < %s; ++i)" % (indent, count))
        lines.append("%s{" % indent)
        lines.append("%s\t%s[i] = %s;" % (indent, arg_name,
            _read_expr(elem, reader)))
        lines.append("%s}" % indent)
        return
    lines.append("%s%s %s = %s;" % (indent, resolved.cs, arg_name,
        _read_expr(resolved, reader)))


def _emit_read_property(prop: PropertyDef, lines: List[str], indent: str,
        reader: str) -> None:
    """Append the statements reading one property into its field."""
    resolved = resolve(prop.type_ref)
    field = to_pascal(prop.name)
    if resolved.base == "ARRAY":
        # ARRAY<T>: packed length + element stream.
        elem = resolved.element
        lines.append("%sint count = %s.ReadPackedInt();" % (indent, reader))
        lines.append("%svar arr = new %s[count];" % (
            indent, elem.cs if elem is not None else "object"))
        lines.append("%sfor (int i = 0; i < count; ++i)" % indent)
        lines.append("%s{" % indent)
        lines.append("%s\tarr[i] = %s;" % (indent, _read_expr(elem, reader)))
        lines.append("%s}" % indent)
        lines.append("%s%s = arr;" % (indent, field))
        return
    expr = _read_expr(resolved, reader)
    lines.append("%s%s = %s;" % (indent, field, expr))


def _emit_vector_write(writer: str, arg: str, count: int,
        lines: List[str], indent: str) -> None:
    for i in range(count):
        lines.append("%s%s.WriteFloat(%s[%d]);" % (indent, writer, arg, i))


def _emit_call_args(method: MethodDef, writer: str, lines: List[str],
        indent: str) -> None:
    """Append the writes for a method's argument list."""
    for index, arg in enumerate(method.args):
        resolved = resolve(arg.type_ref)
        arg_name = _arg_name(index)
        if resolved.base == "ARRAY":
            # ARRAY<T>: packed length then the element stream.
            elem = resolved.element
            lines.append("%s%s.WritePackedInt(%s.Length);" % (
                indent, writer, arg_name))
            lines.append("%sfor (int i = 0; i < %s.Length; ++i)" % (
                indent, arg_name))
            lines.append("%s{" % indent)
            inner = indent + "    "
            elem_name = "item%d" % index
            lines.append("%svar %s = %s[i];" % (inner, elem_name, arg_name))
            _emit_value_write(elem, writer, elem_name, lines, inner)
            lines.append("%s}" % indent)
            continue
        _emit_value_write(resolved, writer, arg_name, lines, indent)


def _emit_value_write(resolved: ResolvedType, writer: str, value: str,
        lines: List[str], indent: str) -> None:
    """Append the write statements for one value of a resolved type."""
    if resolved.base == "BLOB":
        # BigWorld blob: packed-int length + raw bytes (appendBlob).
        lines.append("%s%s.WritePackedInt(%s.Length);" % (indent, writer, value))
        lines.append("%s%s.WriteBlob(%s);" % (indent, writer, value))
        return
    rw = resolved.rw
    if rw is not None:
        lines.append("%s%s.%s(%s);" % (indent, writer, rw[1], value))
    elif resolved.base in ("VECTOR2", "VECTOR3", "VECTOR4"):
        count = {"VECTOR2": 2, "VECTOR3": 3, "VECTOR4": 4}[resolved.base]
        _emit_vector_write(writer, value, count, lines, indent)
    else:
        lines.append("%s// TODO(batch2): unsupported arg type '%s'" % (
            indent, resolved.base))


def _emit_property_doc(prop: PropertyDef, indent: str) -> List[str]:
    resolved = resolve(prop.type_ref)
    lines = ["%s/// <summary>%s (%s)%s</summary>" % (
        indent, to_pascal(prop.name), resolved.base,
        ", client-visible" if prop.flags in ("BASE_AND_CLIENT",
            "CELL_AND_CLIENT", "ALL_CLIENTS", "OWN_CLIENT", "OTHER_CLIENTS")
        else "")]
    if not resolved.supported:
        lines.append("%s/// <remarks>Unsupported type '%s' (%s) - batch-one "
                     "boundary, see README.md.</remarks>" % (
            indent, resolved.base, resolved.note))
    return lines


def _emit_entity_class(entity: EntityDef, prop_ids, method_ids,
        client_props, lines: List[str]) -> None:
    name = entity.name
    cls = "%sBase" % name
    lines.append("")
    lines.append("    /// <summary>")
    lines.append("    /// Generated base class for the %s entity (%s.def). "
                 "User code" % (name, name))
    lines.append("    /// subclasses %s : %s (kbcmd naming)." % (name, cls))
    lines.append("    /// </summary>")
    lines.append("    public abstract class %s" % cls)
    lines.append("    {")
    indent = "        "

    lines.append("%s/// <summary>Entity id this instance represents.</summary>" % indent)
    lines.append("%spublic int Id { get; }" % indent)
    lines.append("%s/// <summary>Entity type name.</summary>" % indent)
    lines.append("%spublic string ClassName => \"%s\";" % (indent, name))
    lines.append("")
    lines.append("%s/// <summary>Remote-call handles (kbcmd legacy name: "
                 "EntityBaseEntityCall_%sBase). The host wires them once "
                 "the entity is live.</summary>" % (indent, name))
    lines.append("%spublic EntityCall%sBase? Base { get; private set; }" % (indent, name))
    lines.append("%spublic EntityCall%sBase? Cell { get; private set; }" % (indent, name))
    lines.append("")
    lines.append("%spublic %s(int id)" % (indent, cls))
    lines.append("%s{" % indent)
    lines.append("%s\tId = id;" % indent)
    lines.append("%s}" % indent)
    lines.append("")

    if client_props:
        lines.append("%s// ---- client-server property ids (server order) ----" % indent)
        lines.append("")
        for prop in client_props:
            lines.append("%spublic const int %s = %d;" % (indent,
                _property_id_const(prop.name), prop_ids[prop.name]))
        lines.append("")

    if method_ids:
        lines.append("%s// ---- exposed client method ids ----" % indent)
        lines.append("")
        for method in entity.client_methods:
            lines.append("%spublic const int %s = %d;" % (indent,
                _method_id_const(method.name), method_ids[method.name]))
        lines.append("")

    for prop in client_props:
        resolved = resolve(prop.type_ref)
        lines.extend(_emit_property_doc(prop, indent))
        if not resolved.supported:
            lines.append("%s// TODO(batch2): '%s' (%s) has no C# mapping yet - "
                         "no property field emitted." % (indent,
                             prop.name, resolved.base))
        else:
            default = " = \"\";" if resolved.cs == "string" else (
                " = default;" if resolved.cs.endswith("[]") else "")
            lines.append("%spublic %s %s { get; set; }%s" % (indent,
                resolved.cs, to_pascal(prop.name), default))
        lines.append("")

    # ReadClientData
    lines.append("%s/// <summary>" % indent)
    lines.append("%s/// Read the initial client data of the createEntity "
                 "message: the" % indent)
    lines.append("%s/// CLIENT-flagged properties in clientServerProperties_ "
                 "order (full" % indent)
    lines.append("%s/// snapshot, batch-one scope).</summary>" % indent)
    lines.append("%spublic virtual void ReadClientData(BwReader reader)" % indent)
    lines.append("%s{" % indent)
    if client_props:
        stopped = False
        for prop in client_props:
            resolved = resolve(prop.type_ref)
            if not resolved.supported:
                lines.append("%s\t// TODO(batch2): unsupported property type "
                             "'%s' (%s) at index %d - initial data decode "
                             "stops here." % (indent, resolved.base,
                             resolved.note, prop_ids[prop.name]))
                stopped = True
                break
            _emit_read_property(prop, lines, indent + "\t", "reader")
        if not stopped:
            lines.append("%s\t// All client properties decoded." % indent)
    lines.append("%s}" % indent)
    lines.append("")

    # Client method hooks + dispatch
    lines.append("%s// ---- server -> client method hooks ----" % indent)
    lines.append("")
    for method in entity.client_methods:
        resolved_args = [resolve(a.type_ref) for a in method.args]
        supported = all(r.supported for r in resolved_args)
        if supported:
            args = ", ".join("%s %s" % (r.cs, _arg_name(i))
                             for i, r in enumerate(resolved_args))
            lines.append("%s/// <summary>%s.%s (client method id %d).</summary>" % (
                indent, name, method.name, method_ids[method.name]))
            lines.append("%spublic virtual void On%s(%s)" % (indent,
                to_pascal(method.name), args))
            lines.append("%s{" % indent)
            lines.append("%s}" % indent)
        else:
            lines.append("%s/// <summary>%s.%s - unsupported arg type, raw "
                         "bytes delivered instead.</summary>" % (
                indent, name, method.name))
            lines.append("%spublic virtual void OnRaw%s(BwReader reader)" % (
                indent, to_pascal(method.name)))
            lines.append("%s{" % indent)
            lines.append("%s}" % indent)
        lines.append("")

    lines.append("%s/// <summary>Dispatch one server-to-client method "
                 "message by exposed id.</summary>" % indent)
    lines.append("%sprotected virtual void OnUnknownClientMethod(int methodId, "
                 "BwReader reader)" % indent)
    lines.append("%s{" % indent)
    lines.append("%s}" % indent)
    lines.append("")
    lines.append("%spublic void InvokeClientMethod(int methodId, BwReader reader)" % indent)
    lines.append("%s{" % indent)
    lines.append("%s    switch (methodId)" % indent)
    lines.append("%s    {" % indent)
    case_indent = indent + "        "
    body_indent = case_indent + "    "
    for method in entity.client_methods:
        lines.append("%scase %s:" % (case_indent,
            _method_id_const(method.name)))
        lines.append("%s{" % case_indent)
        resolved_args = [resolve(a.type_ref) for a in method.args]
        supported = all(r.supported for r in resolved_args)
        if supported:
            for i, r in enumerate(resolved_args):
                _emit_read_arg_stmts(r, "reader", _arg_name(i), i, lines,
                    body_indent)
            lines.append("%sOn%s(%s);" % (body_indent,
                to_pascal(method.name),
                ", ".join(_arg_name(i) for i in range(len(resolved_args)))))
        else:
            lines.append("%s// TODO(batch2): arg types not decoded" % body_indent)
            lines.append("%sOnRaw%s(reader);" % (body_indent, to_pascal(method.name)))
        lines.append("%sbreak;" % body_indent)
        lines.append("%s}" % case_indent)
    lines.append("%sdefault:" % case_indent)
    lines.append("%s{" % case_indent)
    lines.append("%s    OnUnknownClientMethod(methodId, reader);" % case_indent)
    lines.append("%s    break;" % case_indent)
    lines.append("%s}" % case_indent)
    lines.append("%s    }" % indent)
    lines.append("%s}" % indent)
    lines.append("")
    lines.append("    }")


def _emit_entity_call_class(entity: EntityDef, base_ids, cell_ids,
        lines: List[str]) -> None:
    name = entity.name
    cls = "EntityCall%sBase" % name
    lines.append("")
    lines.append("    /// <summary>")
    lines.append("    /// Generated remote-call handle for %s: the exposed "
                 "base/cell methods" % name)
    lines.append("    /// as send stubs. The host supplies the connection, the "
                 "message specs for the")
    lines.append("    /// the two call kinds, and the entity id.")
    lines.append("    /// </summary>")
    lines.append("    public sealed class %s" % cls)
    lines.append("    {")
    indent = "        "

    lines.append("%spublic enum CallKind { Cell, Base }" % indent)
    lines.append("")
    lines.append("%sprivate readonly ClientConnection _connection;" % indent)
    lines.append("%sprivate readonly int _entityId;" % indent)
    lines.append("%sprivate readonly MessageSpec _cellMessage;" % indent)
    lines.append("%sprivate readonly MessageSpec _baseMessage;" % indent)
    lines.append("%sprivate readonly CallKind _kind;" % indent)
    lines.append("")
    lines.append("%spublic %s(ClientConnection connection, int entityId," % (indent, cls))
    lines.append("%s\tMessageSpec cellMessage, MessageSpec baseMessage, "
                 "CallKind kind)" % indent)
    lines.append("%s{" % indent)
    lines.append("%s\t_connection = connection ?? throw new "
                 "System.ArgumentNullException(nameof(connection));" % indent)
    lines.append("%s\t_entityId = entityId;" % indent)
    lines.append("%s\t_cellMessage = cellMessage;" % indent)
    lines.append("%s\t_baseMessage = baseMessage;" % indent)
    lines.append("%s\t_kind = kind;" % indent)
    lines.append("%s}" % indent)
    lines.append("")
    lines.append("%spublic int EntityId => _entityId;" % indent)
    lines.append("%spublic bool IsBase => _kind == CallKind.Base;" % indent)
    lines.append("")
    lines.append("%sprivate MessageSpec Message => _kind == CallKind.Base "
                 "? _baseMessage : _cellMessage;" % indent)
    lines.append("")

    def emit_send(method: MethodDef, exposed_id: int, lines: List[str],
            indent: str) -> None:
        args = ", ".join("%s %s" % (resolve(a.type_ref).cs, _arg_name(i))
                         for i, a in enumerate(method.args))
        lines.append("%s/// <summary>%s.%s (exposed id %d)%s</summary>" % (
            indent, name, method.name, exposed_id,
            ", no args" if not method.args else ""))
        lines.append("%spublic void Send%s(%s)" % (indent,
            to_pascal(method.name), args))
        lines.append("%s{" % indent)
        lines.append("%s\t_connection.SendMessage(Message, writer =>" % indent)
        lines.append("%s\t{" % indent)
        lines.append("%s\t\twriter.WriteInt32(_entityId);" % indent)
        lines.append("%s\t\twriter.WriteUInt8((byte)%d);" % (indent, exposed_id))
        _emit_call_args(method, "writer", lines, indent + "\t\t")
        lines.append("%s\t});" % indent)
        lines.append("%s}" % indent)
        lines.append("")

    if base_ids or cell_ids:
        lines.append("%s// ---- exposed base / cell method stubs ----" % indent)
        lines.append("")
        for m in entity.base_methods:
            if m.exposed:
                emit_send(m, base_ids[m.name], lines, indent)
        for m in entity.cell_methods:
            if m.exposed:
                emit_send(m, cell_ids[m.name], lines, indent)

    lines.append("    }")


def emit_entity(entity: EntityDef) -> str:
    """The full generated ``<Entity>Base.cs`` text."""
    name = entity.name
    prop_ids = {}
    for index, prop in enumerate(client_properties(entity)):
        prop_ids[prop.name] = index
    method_ids = client_method_ids(entity)
    base_ids = exposed_method_ids(entity.base_methods, entity)
    cell_ids = exposed_method_ids(entity.cell_methods, entity)
    client_props = client_properties(entity)

    lines: List[str] = []
    lines.append("// ---------------------------------------------------------------------------")
    lines.append("// Generated by client_sdk/generator/bwgen.py from %s.def - do not edit." % name)
    lines.append("// Regenerate: python3 client_sdk/generator/bwgen.py <res/scripts-dir> \\")
    lines.append("//     --out <dir> --lang cs")
    lines.append("// ---------------------------------------------------------------------------")
    lines.append("using System;")
    lines.append("using BigWorld.ClientSdk.Connection;")
    lines.append("using BigWorld.ClientSdk.Protocol;")
    lines.append("using BigWorld.ClientSdk.Streams;")
    lines.append("")
    lines.append("namespace %s" % NAMESPACE)
    lines.append("{")

    _emit_entity_class(entity, prop_ids, method_ids, client_props, lines)
    _emit_entity_call_class(entity, base_ids, cell_ids, lines)

    lines.append("}")
    lines.append("")
    return "\n".join(lines).replace("\t", "    ")
