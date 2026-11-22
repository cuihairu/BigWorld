"""TypeScript emitter for the CL-2 plugin runtime.

Each entity emits one ``<Entity>Base.ts`` file mirroring the C# output:

* ``<Entity>Base extends Entity`` (from the CL-2 ``entity.ts``) with the
  property/method id constants, typed properties, ``readClientData``, the
  ``onXxx`` hooks and ``invokeClientMethod``.
* ``EntityCall<Entity>Base extends EntityCall`` with the exposed base/cell
  send stubs.

Id numbering is shared with ``emit_cs`` (via ``bwgen.ids``); see the module
doc there.
"""

from __future__ import annotations

from typing import List

from .ids import client_method_ids, client_properties, exposed_method_ids
from .model import EntityDef, MethodDef
from .naming import to_camel, to_pascal
from .typemap import ResolvedType, resolve

TS = "typescript"


def _arg_name(index: int) -> str:
    return "value%d" % index


def _prop_const(name: str) -> str:
    return "propertyId%s" % to_pascal(name)


def _method_const(name: str) -> str:
    return "methodId%s" % to_pascal(name)


def _read_expr(resolved: ResolvedType, reader: str) -> str:
    if resolved.base == "BLOB":
        # BigWorld blob: packed-int length + raw bytes (appendBlob).
        return "%s.readBytesRaw(%s.readPackedInt())" % (reader, reader)
    if resolved.rw is not None:
        return "%s.%s()" % (reader, resolved.rw[2])
    if resolved.base in ("VECTOR2", "VECTOR3", "VECTOR4"):
        n = {"VECTOR2": 2, "VECTOR3": 3, "VECTOR4": 4}[resolved.base]
        return "[%s]" % ", ".join(["%s.readFloat()" % reader] * n)
    return None


def _ts_type(resolved: ResolvedType) -> str:
    if resolved.base == "ARRAY":
        return "%s[]" % resolved.element.ts
    return resolved.ts


def _emit_read_arg_stmts(resolved: ResolvedType, reader: str, arg_name: str,
        arg_index: int, lines: List[str], indent: str) -> None:
    """Declare and read one method argument into ``arg_name``."""
    if resolved.base == "ARRAY":
        elem = resolved.element
        count = "_count%d" % arg_index
        lines.append("%sconst %s = %s.readPackedInt();" % (indent, count, reader))
        lines.append("%sconst %s: %s[] = new Array(%s);" % (
            indent, arg_name, elem.ts, count))
        lines.append("%sfor (let i = 0; i < %s; ++i) %s[i] = %s;" % (
            indent, count, arg_name, _read_expr(elem, reader)))
        return
    lines.append("%sconst %s: %s = %s;" % (indent, arg_name,
        _ts_type(resolved), _read_expr(resolved, reader)))


def _emit_read_stmts(prop, lines: List[str], indent: str, reader: str) -> None:
    resolved = resolve(prop.type_ref)
    field = to_camel(prop.name)
    if resolved.base == "ARRAY":
        lines.append("%s{ const count = %s.readPackedInt();" % (indent, reader))
        lines.append("%s  const arr: %s[] = new Array(count);" % (
            indent, resolved.element.ts))
        lines.append("%s  for (let i = 0; i < count; ++i) {" % indent)
        lines.append("%s    arr[i] = %s;" % (indent, _read_expr(resolved.element, reader)))
        lines.append("%s  }" % indent)
        lines.append("%s  this.%s = arr;" % (indent, field))
        lines.append("%s}" % indent)
        return
    lines.append("%sthis.%s = %s;" % (indent, field, _read_expr(resolved, reader)))


def _emit_call_args(method: MethodDef, writer: str, lines: List[str],
        indent: str) -> None:
    for index, arg in enumerate(method.args):
        resolved = resolve(arg.type_ref)
        arg_name = _arg_name(index)
        if resolved.base == "ARRAY":
            lines.append("%s%s.writePackedInt(%s.length);" % (indent, writer, arg_name))
            lines.append("%sfor (const item of %s) {" % (indent, arg_name))
            lines.append("%s  %s;" % (indent, _write_expr(resolved.element, writer, "item")))
            lines.append("%s}" % indent)
            continue
        if resolved.base == "BLOB":
            # BigWorld blob: packed-int length + raw bytes (appendBlob).
            lines.append("%s%s.writePackedInt(%s.length);" % (indent, writer, arg_name))
            lines.append("%s%s.writeBlob(%s);" % (indent, writer, arg_name))
            continue
        lines.append("%s%s;" % (indent, _write_expr(resolved, writer, arg_name)))


def _write_expr(resolved: ResolvedType, writer: str, value: str) -> str:
    if resolved.rw is not None:
        return "%s.%s(%s)" % (writer, resolved.rw[3], value)
    if resolved.base == "BLOB":
        return "%s.writeBlob(%s)" % (writer, value)
    if resolved.base in ("VECTOR2", "VECTOR3", "VECTOR4"):
        n = {"VECTOR2": 2, "VECTOR3": 3, "VECTOR4": 4}[resolved.base]
        return "; ".join("%s.writeFloat(%s[%d])" % (writer, value, i)
                         for i in range(n))
    return "// TODO(batch2): unsupported arg type '%s'" % resolved.base


def _emit_entity_class(entity: EntityDef, prop_ids, method_ids,
        client_props, lines: List[str]) -> None:
    name = entity.name
    cls = "%sBase" % name
    lines.append("")
    lines.append("/** Generated base class for the %s entity (%s.def)." % (name, name))
    lines.append(" *  Subclass %s : %s (kbcmd naming). */" % (name, cls))
    lines.append("export abstract class %s extends Entity {" % cls)
    indent = "  "

    lines.append("%sstatic readonly className = \"%s\";" % (indent, name))
    lines.append("")

    if client_props:
        lines.append("%s/** Client-server property ids (server order). */" % indent)
        for prop in client_props:
            lines.append("%sstatic readonly %s = %d;" % (indent,
                _prop_const(prop.name), prop_ids[prop.name]))
        lines.append("")

    if method_ids:
        lines.append("%s/** Exposed client method ids. */" % indent)
        for method in entity.client_methods:
            lines.append("%sstatic readonly %s = %d;" % (indent,
                _method_const(method.name), method_ids[method.name]))
        lines.append("")

    for prop in client_props:
        resolved = resolve(prop.type_ref)
        if not resolved.supported:
            lines.append("%s// TODO(batch2): '%s' (%s) has no TS mapping yet - "
                         "no property field emitted." % (indent,
                             prop.name, resolved.base))
            continue
        default = ' = ""' if resolved.ts == "string" else (
            " = []" if resolved.ts.endswith("[]") else "")
        # strictPropertyInitialization: fields without an initializer get a
        # definite-assignment assertion (they are set in readClientData).
        bang = "!" if not default else ""
        lines.append("%s%s%s: %s%s;" % (indent, to_camel(prop.name), bang,
            _ts_type(resolved), default))
    if client_props:
        lines.append("")

    lines.append("%s/** Read the initial client data (full snapshot, batch-one scope). */" % indent)
    lines.append("%sreadClientData(reader: BwReader): void {" % indent)
    if client_props:
        for prop in client_props:
            resolved = resolve(prop.type_ref)
            if not resolved.supported:
                lines.append("%s  // TODO(batch2): unsupported property type "
                             "'%s' at index %d - decode stops here." % (
                    indent, resolved.base, prop_ids[prop.name]))
                break
            _emit_read_stmts(prop, lines, indent + "  ", "reader")
    lines.append("%s}" % indent)
    lines.append("")

    lines.append("%s/** Server -> client method hooks. */" % indent)
    for method in entity.client_methods:
        resolved_args = [resolve(a.type_ref) for a in method.args]
        supported = all(r.supported for r in resolved_args)
        if supported:
            args = ", ".join("%s: %s" % (_arg_name(i), _ts_type(r))
                             for i, r in enumerate(resolved_args))
            lines.append("%son%s(%s): void {}" % (indent,
                to_pascal(method.name), args))
        else:
            lines.append("%sonRaw%s(reader: BwReader): void {}" % (
                indent, to_pascal(method.name)))
    if entity.client_methods:
        lines.append("")

    lines.append("%s/** Dispatch one server-to-client method by exposed id. */" % indent)
    lines.append("%sinvokeClientMethod(methodId: number, reader: BwReader): void {" % indent)
    lines.append("%s  switch (methodId) {" % indent)
    case_label = indent + "    "
    case_body = indent + "      "
    for method in entity.client_methods:
        resolved_args = [resolve(a.type_ref) for a in method.args]
        supported = all(r.supported for r in resolved_args)
        # The id constants are static members, so they need a class qualifier.
        lines.append("%scase %s.%s:" % (case_label, cls, _method_const(method.name)))
        lines.append("%s{" % case_label)
        if supported:
            for i, r in enumerate(resolved_args):
                _emit_read_arg_stmts(r, "reader", _arg_name(i), i, lines,
                    case_body)
            lines.append("%sthis.on%s(%s);" % (case_body,
                to_pascal(method.name),
                ", ".join(_arg_name(i) for i in range(len(resolved_args)))))
        else:
            lines.append("%s// TODO(batch2): arg types not decoded" % case_body)
            lines.append("%sthis.onRaw%s(reader);" % (
                case_body, to_pascal(method.name)))
        lines.append("%sbreak;" % case_body)
        lines.append("%s}" % case_label)
    lines.append("%sdefault:" % case_label)
    lines.append("%s{" % case_label)
    lines.append("%sbreak;" % case_body)
    lines.append("%s}" % case_label)
    lines.append("%s  }" % indent)
    lines.append("%s}" % indent)
    lines.append("}")
    lines.append("")


def _emit_entity_call_class(entity: EntityDef, base_ids, cell_ids,
        lines: List[str]) -> None:
    name = entity.name
    cls = "EntityCall%sBase" % name
    lines.append("")
    lines.append("/** Generated remote-call handle for %s (exposed base/cell "
                 "send stubs)." % name)
    lines.append(" *  Assign to <code>entity.base</code> / <code>entity.cell"
                 "</code>. */")
    lines.append("export class %s extends EntityCall {" % cls)
    indent = "  "

    def emit_send(method: MethodDef, exposed_id: int, lines: List[str],
            indent: str) -> None:
        args = ", ".join("%s: %s" % (_arg_name(i), _ts_type(resolve(a.type_ref)))
                         for i, a in enumerate(method.args))
        lines.append("%s/** %s.%s (exposed id %d)%s */" % (indent,
            name, method.name, exposed_id, "" if args else " (no args)"))
        lines.append("%ssend%s(%s): void {" % (indent,
            to_pascal(method.name), args))
        lines.append("%s  const call = this.newCall();" % indent)
        lines.append("%s  call.writer.writeUInt8(%d);" % (indent, exposed_id))
        _emit_call_args(method, "call.writer", lines, indent + "  ")
        lines.append("%s  this.sendCall(call);" % indent)
        lines.append("%s}" % indent)
        lines.append("")

    for m in entity.base_methods:
        if m.exposed:
            emit_send(m, base_ids[m.name], lines, indent)
    for m in entity.cell_methods:
        if m.exposed:
            emit_send(m, cell_ids[m.name], lines, indent)

    lines.append("}")
    lines.append("")


def emit_entity(entity: EntityDef) -> str:
    """The full generated ``<Entity>Base.ts`` text."""
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
    lines.append("//     --out <dir> --lang ts")
    lines.append("// ---------------------------------------------------------------------------")
    lines.append("import { BwReader } from \"../bwReader.js\";")
    lines.append("import { BwWriter } from \"../bwStream.js\";")
    lines.append("import { Entity, EntityCall } from \"../entity.js\";")
    lines.append("")

    _emit_entity_class(entity, prop_ids, method_ids, client_props, lines)
    _emit_entity_call_class(entity, base_ids, cell_ids, lines)

    return "\n".join(lines).replace("\t", "    ")
