"""def type -> wire size / C# / TS mapping.

The wire sizes mirror the server's ``DataType::streamSize()`` implementations
(``programming/bigworld/lib/entitydef/data_types/*.cpp``): fixed types return
their byte width, variable types return -1. ``DataDescription::streamSize``
and ``MethodDescription::streamSize`` fold a variable type to
``-varLenHeaderSize`` which defaults to
``Mercury::DEFAULT_VARIABLE_LENGTH_HEADER_SIZE == 1`` (interface_element.hpp:75),
so a variable member's sort key is -1 for the def trees this generator
targets (no ``VariableLengthHeaderSize`` override).
"""

from __future__ import annotations

from typing import Optional

from .model import TypeRef

# base def type name -> (fixed wire size or None for variable, C# type, TS type)
_SIMPLE_TYPES = {
    "INT8":   (1, "sbyte", "number"),
    "INT16":  (2, "short", "number"),
    "INT32":  (4, "int", "number"),
    "INT64":  (8, "long", "bigint"),
    "UINT8":  (1, "byte", "number"),
    "UINT16": (2, "ushort", "number"),
    "UINT32": (4, "uint", "number"),
    "UINT64": (8, "ulong", "bigint"),
    "FLOAT":  (4, "float", "number"),
    "DOUBLE": (8, "double", "number"),
    "STRING":  (None, "string", "string"),
    "UNICODE": (None, "string", "string"),
    "BLOB":    (None, "byte[]", "Uint8Array"),
    "VECTOR2": (8, "float[]", "number[]"),
    "VECTOR3": (12, "float[]", "number[]"),
    "VECTOR4": (16, "float[]", "number[]"),
}

# Types known to the engine but deliberately out of batch-one scope. They
# still reserve their id slots; see README.md "Batch-one boundary".
_KNOWN_UNSUPPORTED = {
    "USER_TYPE",
    "FIXED_DICT",
    "PY_TUPLE",
    "PY_DICT",
    "MAILBOX",
    "MAILBOX_BASE",
    "UDO_REF",
    "TWO_VECTOR2",
}

# The read/write call names on the CL-1 BwReader/BwWriter and CL-2
# BwReader/BwWriter pair, per base type.
_RW_NAMES = {
    "INT8":   ("ReadInt8", "WriteInt8", "readInt8", "writeInt8"),
    "INT16":  ("ReadInt16", "WriteInt16", "readInt16", "writeInt16"),
    "INT32":  ("ReadInt32", "WriteInt32", "readInt32", "writeInt32"),
    "INT64":  ("ReadInt64", "WriteInt64", "readInt64", "writeInt64"),
    "UINT8":  ("ReadUInt8", "WriteUInt8", "readUInt8", "writeUInt8"),
    "UINT16": ("ReadUInt16", "WriteUInt16", "readUInt16", "writeUInt16"),
    "UINT32": ("ReadUInt32", "WriteUInt32", "readUInt32", "writeUInt32"),
    "UINT64": ("ReadUInt64", "WriteUInt64", "readUInt64", "writeUInt64"),
    "FLOAT":  ("ReadFloat", "WriteFloat", "readFloat", "writeFloat"),
    "DOUBLE": ("ReadDouble", "WriteDouble", "readDouble", "writeDouble"),
    "STRING":  ("ReadString", "WriteString", "readString", "writeString"),
    "UNICODE": ("ReadString", "WriteString", "readString", "writeString"),
    "BLOB":    ("ReadBytesRaw", "WriteBlob", "readBytesRaw", "writeBlob"),
}


class ResolvedType:
    """A :class:`TypeRef` with its wire and language facts attached."""

    def __init__(self, base: str, wire_size: Optional[int], cs: Optional[str],
            ts: Optional[str], supported: bool, element: Optional["ResolvedType"],
            note: str = ""):
        self.base = base
        self.wire_size = wire_size      # None for variable-length types
        self.cs = cs
        self.ts = ts
        self.supported = supported
        self.element = element
        self.note = note

    @property
    def variable(self) -> bool:
        return self.wire_size is None

    @property
    def stream_size(self) -> int:
        """The DataDescription::streamSize sort key: byte width, or -1 for
        a variable-length member (DEFAULT_VARIABLE_LENGTH_HEADER_SIZE=1)."""
        return -1 if self.wire_size is None else self.wire_size

    @property
    def rw(self):
        """(C# read, C# write, TS read, TS write) call names, or None."""
        return _RW_NAMES.get(self.base)


def resolve(type_ref: TypeRef) -> ResolvedType:
    """Map a parsed TypeRef to its wire size and language types."""
    base = type_ref.base

    if base == "ARRAY":
        if type_ref.element is None:
            return ResolvedType("ARRAY", None, None, None, False, None,
                "ARRAY without an element type cannot be mapped")
        element = resolve(type_ref.element)
        if not element.supported:
            return ResolvedType("ARRAY", None, None, None, False, element,
                "ARRAY of unsupported element type '%s'" % element.base)
        return ResolvedType("ARRAY", None,
            element.cs + "[]", element.ts + "[]", True, element)

    if base in _SIMPLE_TYPES:
        size, cs, ts = _SIMPLE_TYPES[base]
        return ResolvedType(base, size, cs, ts, True, None)

    note = ("known engine type, deferred to a later batch"
            if base in _KNOWN_UNSUPPORTED else "unknown def type")
    return ResolvedType(base, None, None, None, False, None, note)
