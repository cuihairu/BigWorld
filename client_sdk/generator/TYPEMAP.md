# TYPEMAP — def type to wire/runtime mapping

The mapping the generator applies, with the server source each row mirrors.
Sizes are `DataType::streamSize()` semantics
(`programming/bigworld/lib/entitydef/data_types/*.cpp`): fixed types return
their byte width, variable types return -1.

## Simple types

| Def type | Wire | C# | TS | Server source |
| --- | --- | --- | --- | --- |
| INT8 | 1 fixed | `sbyte` | `number` | `integer_types.hpp` |
| INT16 | 2 fixed | `short` | `number` | 〃 |
| INT32 | 4 fixed | `int` | `number` | 〃 |
| INT64 | 8 fixed | `long` | `bigint` | 〃 |
| UINT8 | 1 fixed | `byte` | `number` | 〃 |
| UINT16 | 2 fixed | `ushort` | `number` | 〃 |
| UINT32 | 4 fixed | `uint` | `number` | 〃 |
| UINT64 | 8 fixed | `ulong` | `bigint` | 〃 |
| FLOAT | 4 fixed | `float` | `number` | `data_types` |
| DOUBLE | 8 fixed | `double` | `number` | 〃 |
| STRING | variable | `string` | `string` | `string_data_type.cpp` |
| UNICODE | variable | `string` | `string` | `unicode_data_type.cpp` |
| BLOB | variable | `byte[]` | `Uint8Array` | `blob_data_type.cpp` |
| VECTOR2 | 8 fixed (2×f32) | `float[]` | `number[]` | `vector_data_type.cpp` |
| VECTOR3 | 12 fixed (3×f32) | `float[]` | `number[]` | 〃 |
| VECTOR4 | 16 fixed (4×f32) | `float[]` | `number[]` | 〃 |

Wire encoding notes:

* **STRING/UNICODE** — packed-int length prefix + UTF-8 bytes
  (`BinaryOStream::appendString`). Both runtimes expose this as one call
  (`ReadString`/`WriteString`, `readString`/`writeString`).
* **BLOB** — packed-int length prefix + raw bytes. The runtimes' raw
  byte helpers do **not** prefix, so the generator emits the length write/read
  explicitly (`WritePackedInt(len); WriteBlob(bytes)` /
  `ReadBytesRaw(ReadPackedInt())`).
* **VECTOR*** — N consecutive little-endian floats; emitted as an array
  literal read / N scalar writes.

## Composite types

| Def type | Wire | C# | TS | Notes |
| --- | --- | --- | --- | --- |
| `ARRAY<T>` (`ARRAY <of> T` or `ARRAY <Element> T`) | variable | `T[]` | `T[]` | packed-int element count then elements in stream order |
| `ARRAY<ARRAY<...>>` | — | — | — | not supported (batch one) |

## Unsupported (batch one)

`USER_TYPE`, `FIXED_DICT`, `PY_TUPLE`, `PY_DICT`, `MAILBOX`, `MAILBOX_BASE`,
`UDO_REF`, `TWO_VECTOR2`, and any unknown type name:

* the member's **id slot is still reserved** — numbering stays aligned with
  the server even though the payload is opaque;
* a property gets no generated field and the initial-snapshot decode stops at
  that index (TODO comment in the output);
* a client method falls back to `OnRawXxx(BwReader reader)` (C#) or a
  skipped decode (TS);
* an exposed base/cell method still gets a `SendXxx` stub if its **args** are
  supported; unsupported args emit a TODO and no stub body writes.

## ids — the wire-facing numbering

Two id families are generated, both replicating the server's stable sort:

* **Exposed method ids** (`MethodIdXxx`): there is exactly **one** id space
  per entity, shared by client methods (server→client) and exposed base/cell
  methods (client→server). `EntityMethodDescriptions::init` appends every
  exposed method — all ClientMethods, plus base/cell methods carrying
  `<Exposed/>` — to one joint `exposedMethods_` list
  (entity_method_descriptions.cpp:95-98), and `setExposedMsgIDs`
  stable-sorts that joint list with `ExposedMethodsSortHelper`
  (entity_method_descriptions.cpp:202). The sorted position **is** the exposed
  msg id: `Witness::sendToClient` forwards `description.exposedMsgID()` to the
  client, and `exposedMethodFromMsgID` dispatches exposed cell calls the same
  way. The comparator is `(category, size)`:

  * fixed-size messages first, **ascending byte size**;
  * then variable-size messages, ascending by the magnitude of their
    (negative) stream size — the variable-length header size, which defaults
    to `Mercury::DEFAULT_VARIABLE_LENGTH_HEADER_SIZE == 1`
    (`interface_element.hpp:75`), so every plain variable method sorts -1.

  A method's size is the sum of its argument sizes (`MethodDescription::
  streamSize(true)`); any variable arg makes the whole message variable.

  The pre-sort (tie-break) order follows the server's parse order: BaseMethods,
  CellMethods, then ClientMethods, def-file order within each
  (entity_description.cpp:559-572). A name shared across families with the
  same signature collapses into one entry (one id), like the server's
  `internalMethods_` map.

* **Client-server property ids** (`PropertyIdXxx`): `clientServerProperties_`
  holds the CLIENT-flagged properties and `allocateClientServerFullIndexes`
  sorts with the same fixed-ascending/variable-ascending rule
  (`entity_description.cpp`), so the per-entity property update stream indexes
  follow that order. The initial snapshot (createEntity) streams them in this
  order too.

Both sorts are **stable** — members that compare equal keep their def-file
order. Worked example (golden tree, ClientAvatar):

| Member | Size | Sort key | Id |
| --- | --- | --- | --- |
| prop3 INT8 | 1 fixed | (0, 1) | 0 |
| prop2 INT16 | 2 fixed | (0, 2) | 1 |
| prop1 INT32 | 4 fixed | (0, 4) | 2 |
| playerName STRING | variable | (1, 1) | 3 |
| prop5 USER_TYPE | variable (unsupported) | (1, 1) | 4 |

Exposed methods (joint id space): base `logOff()` = 0 bytes fixed → id 0;
client `cellTick(INT32)` = 4 fixed → id 1; client `fromCell(STRING)`
variable → id 2. So the client dispatches `cellTick` on 1 / `fromCell` on 2,
and `sendLogOff` writes exposed id 0.

## What the send stubs write

For an exposed base/cell method the client→server body is

```
[entityId: int32 LE][exposed method id: uint8][arg0][arg1]...
```

The entity id prefix and exposed id are written by the generated
`EntityCall<Entity>Base` stubs; the args follow in def order using the
per-type writes above. This matches the server's
`Entity::onRemoteMethodCall` / `CellApp` dispatch, where the first int32
selects the entity and the following uint8 selects the exposed method.
