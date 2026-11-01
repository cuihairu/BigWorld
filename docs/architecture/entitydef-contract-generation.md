# EntityDef Contract and Protocol Generation

> **English** | [中文](entitydef-contract-generation.md)

<div class="arch-hero">

EntityDef is not an ordinary configuration file. It merges `entities.xml`, entity
`.def` files, component `.def` files, script distribution, property flags,
method signatures and network message ranges into a runtime protocol contract.
The serialisation chapter explains "how to write into a stream"; this chapter
explains "why these fields, methods and numbers exist".

</div>

## Conclusion First

BigWorld's entity protocol contract is produced by runtime parsing rather than
by an offline IDL compiler:

1. `scripts/entities.xml` decides the set and the order of entity types.
2. Each entity's `scripts/entity_defs/<Entity>.def` decides properties, methods,
   parent definitions, distribution and AOI metadata.
3. `EntityDescriptionMap` parses all entities and assigns `EntityTypeID`,
   `clientIndex`, method exposed ids and client-server property indexes.
4. `EntityDescription` cuts the same definition into Base, Cell, Client,
   Persistent, Ghosted and other views.
5. `process_defs` reuses the same parsing logic and exports the result as Python
   objects for external script generation tooling or documentation.

Source entries:

- `EntityDescriptionMap::parse()` at
  entity_description_map.cpp.
- `EntityDescriptionMap::parseInternal()` at
  entity_description_map.cpp.
- `EntityDescription::parse()` at
  entity_description.cpp.
- `EntityDescription::parseInterface()` at
  entity_description.cpp.
- `EntityDescription::parseProperties()` at
  entity_description.cpp.
- `EntityMethodDescriptions::init()` at
  entity_method_descriptions.cpp.
- The main entry of `process_defs` at
  main.cpp.

## Contract Inputs

The input of EntityDef is not a single file:

- `scripts/entities.xml`: the entity type manifest, supporting `ClientServerEntities`
  and `ServerOnlyEntities` grouping.
- `scripts/services.xml`: service definitions, which only follow server-side
  semantics.
- `scripts/entity_defs/*.def`: entity definitions.
- `scripts/entity_defs/components/*.def`: component definitions, reusable by
  entities.
- `scripts/entity_defs/interfaces/*.def`: interface definitions, implementable by
  entities or services.
- Python files under `scripts/client`, `scripts/base`, `scripts/cell`,
  `scripts/service`: in legacy parsing and distribution decisions they affect
  whether an entity has a corresponding script.

This explains why the EntityDef migration cannot scan only `.def` files. Whether
an entity can exist on Client, Base or Cell may be declared explicitly by the
`.def`'s `Distribution`, or inferred from whether the script file exists. The
relevant decision is in `HasScriptOrTagDistributionDecider`, see
entity_description.cpp.

## The Parse Call Chain

A typical parse chain looks like this:

<MermaidDiagram title="EntityDef parse chain">
flowchart TD
  A[scripts/entities.xml] --> B[EntityDescriptionMap::parse]
  B --> C[parseInternal]
  C --> D[EntityDescription::parse]
  D --> E[Parent recursive parse]
  D --> F[Distribution / ClientName]
  D --> G[parseInterface]
  G --> H[parseMethods]
  G --> I[parseComponents]
  I --> J[parseComponent]
  J --> K[component Properties / Methods]
  D --> L[parseProperties]
  B --> M[setExposedMessageIDs]
  B --> N[addToMD5]
</MermaidDiagram>

A few key points:

- `EntityDescriptionMap::parse()` prefers reading `ClientServerEntities` and
  `ServerOnlyEntities`, and falls back to legacy parsing when they are absent,
  see
  entity_description_map.cpp.
- `EntityDescription::parse()` handles `Parent` first, and the parent definition
  is parsed first, see
  entity_description.cpp.
- `parseInterface()` reads persistence, LoD, Volatile, AOI, methods, temporary
  properties and components, see
  entity_description.cpp.
- `parseComponents()` opens the component `.def` and merges the component's
  properties and methods into the entity definition, see
  entity_description.cpp.
- Services are added to the EntityDescriptionMap separately through
  `parseServices()`, see
  entity_description_map.cpp.

This is not a simple XML-to-struct mapping; it is multi-source composition,
inheritance, component merging and distribution inference.

## Type IDs and Client Index

After all entities are parsed, `EntityDescriptionMap::parse()` assigns
`EntityTypeID` by the `vector_` order:

- `desc.index(i)` is the server-side entity type id.
- `map_[desc.name()] = desc.index()` builds the name-to-id mapping.
- For client-visible entities, `clientIndex` defaults to the current ordinal id.

Source: entity_description_map.cpp.

This means the order in `entities.xml` has protocol meaning. Reordering entity
types arbitrarily is not mere tidying - it can change entity type ids and the
client/server shared contract.

`ClientName` is a legacy compatibility feature: a server entity may declare a
different client type name. After parsing, it verifies that the alias has the
same number of client methods and client-server properties, otherwise it errors
out. Source:
entity_description_map.cpp.
The source comment explicitly marks the feature as deprecated, so its use should
not be extended further.

## The Property Contract

Property parsing is done jointly by `DataDescription::parse()` and
`EntityDescription::parseProperties()`.

`DataDescription` handles the semantics of a single property:

- `Type`: constructs a `DataType`.
- `Flags`: maps to `CELL_PRIVATE`, `CELL_PUBLIC`, `OTHER_CLIENTS`, `OWN_CLIENT`,
  `BASE`, `BASE_AND_CLIENT`, `CELL_PUBLIC_AND_OWN`, `ALL_CLIENTS`,
  `EDITOR_ONLY`.
- `Persistent`: whether it enters persistence semantics.
- `Identifier` / `Indexed`: database identity and index semantics.
- `Default`: where the default value comes from.

The relevant parsing is at
data_description.cpp.

`EntityDescription::parseProperties()` handles entity-level numbering and
constraints:

- Ordinary properties get an `index`.
- Client-server properties get a `clientServerFullIndex`.
- Overriding an existing property reuses the original index.
- A client-server property containing a `PYTHON` member warns about a potential
  security risk.
- A client-server property containing a `MAILBOX` member is an error, because it
  cannot be sent to the client.
- `OTHER_CLIENTS` properties are allocated an event stamp and a detail level.

Source: entity_description.cpp.

What matters about a property is not "what the field name is" but which data
domains it enters. A property may exist only on Cell, or it may flow into Base,
Own Client, Other Clients, Ghost, Persistent or a database index path. Every
later creation, migration, sync, save and hot update depends on these flags.

## `.def` and the Database Contract

The database questions you raised all ultimately start at the `.def`.

BigWorld does not have a database model first and then map it onto entities. It
parses the `.def` first, and the DB layer builds its mapping from the persistent
subset. In other words, here the `.def` acts more like a "protocol +
persistence contract".

### Entity-level Tags

At entity level there are at least two switches that relate directly to the
database:

- `Persistent`: whether the entity type may be persisted, see
  entity_description.cpp.
- `ExplicitDatabaseID`: whether an explicit DBID may be given, see
  entity_description.cpp.

There is also a hard constraint on the DBApp side:

- `EntityDefs::isValidEntityType()` only accepts persistent entity types, see
  db_entitydefs.hpp.

This means:

- Without entity-level `Persistent`, the whole type never enters the DB
  contract.
- Even if a later property is marked `Persistent`, it means nothing without
  entity-level persistence capability.

### Property-level Tags

The tags in `DataDescription::parse()` that relate directly to the database are:

- `Persistent`
- `Identifier`
- `Indexed`
- `Indexed/Unique`
- `DatabaseLength`

The parse entry is at
data_description.cpp.

The source semantics can be summarised directly:

<div class="decision-table">

| Tag | Meaning | Key constraint |
| --- | --- | --- |
| `Persistent` | The property enters the persistent subset | Without it, it never enters `ONLY_PERSISTENT_DATA` |
| `Identifier` | Uses the property as the entity's business identity | Derived as an index automatically, unique by default |
| `Indexed` | Builds an index for a persistent property | A non-persistent property cannot be indexed |
| `Indexed/Unique` | Controls whether the index is unique | Only takes effect when indexed |
| `DatabaseLength` | Affects the DB length policy for string/composite types | Not every type uses it equivalently |

</div>

Several hard rules here are easy to miss:

- `Identifier` turns on `DATA_ID` and requires an index by default, see
  data_description.cpp.
- `Indexed` is only allowed on persistent properties, otherwise it errors out
  directly, see
  data_description.cpp.
- `Indexed/Unique` decides between `DATABASE_INDEXING_UNIQUE` and
  `DATABASE_INDEXING_NON_UNIQUE`, see
  data_description.cpp.

### Identifier Is Not a Primary Key for Any Type

The DB side puts extra restrictions on Identifier - not every `Persistent`
property can be promoted to the entity identity:

- `EntityDefs::findIdentifier()` explicitly supports only one Identifier.
- It requires the property type to be `STRING`, `UNICODE_STRING` or `BLOB`.

Source: db_entitydefs.cpp
and db_entitydefs.cpp.

This differs from many ORMs, where any scalar can be a unique key by default.
BigWorld converges here inside the engine, which makes lookup / cache / DBID
binding logic uniform.

### `ONLY_PERSISTENT_DATA` Is the Real DB View Slice

Earlier it was said that "EntityDescription cuts out different views"; for the
database the crucial one is `ONLY_PERSISTENT_DATA`.

There are three layers of evidence:

- `EntityDescription::addToStream()` checks `ONLY_PERSISTENT_DATA` while walking
  properties and only lets persistent properties into the output, see
  entity_description.cpp.
- `Base::addToStream()` explicitly passes `ONLY_PERSISTENT_DATA` when writing
  Base data, see
  base.cpp.
- `EntityTypeMapping::visit()` also only visits
  `BASE_DATA | CELL_DATA | ONLY_PERSISTENT_DATA` when creating tables and writing
  rows, see
  entity_type_mapping.cpp.

This shows the database schema, the database writes and the database digest all
use the same persistent view rather than three separate rule sets.

### `DatabaseLength`, Complex Types and Table Structure

Once written, a `.def` does not necessarily land as an ordinary column -
complex types go through a mapping decision:

- `PropertyMapping::create()` picks a mapping implementation by `DataType`, see
  property_mapping.cpp.
- `ARRAY` / `TUPLE` may use `SequenceMapping` in non-indexed cases, or
  `BlobbedSequenceMapping` when `dbLen() > 0`, see
  property_mapping.cpp.
- `CLASS` / `FIXED_DICT` use the class-type mapping, see
  property_mapping.cpp.
- String-like properties check whether `DatabaseLength` exceeds the limit, see
  string_like_mapping.cpp.

So `DatabaseLength` is not a "UI hint parameter"; it genuinely affects the
underlying mapping strategy and column definition.

### Why a `.def` Change Often Means a DB Migration

From the source, BigWorld has explicit signatures and synchronisation logic for
DB contract changes:

- `EntityDefs::init()` computes the full defs digest and the persistent
  properties digest, see
  db_entitydefs.cpp.
- MySQL initialisation uses the persistent properties digest for a compatibility
  check, see
  mysql_database.cpp.
- `TableSynchroniser` performs table synchronisation through the `sync_db` path,
  see
  table_synchroniser.cpp.

Therefore none of the following `.def` changes is merely "a script field tweak":

- Adding or removing a `Persistent` property.
- Changing `Identifier`.
- Changing `Indexed` / `Indexed/Unique`.
- Adjusting a complex type's structure.
- Adjusting `DatabaseLength`.

All of them can change the persistent digest, the DB schema, or how existing data
is interpreted.

## The Method Contract

Methods are split into three groups by their target side:

- `ClientMethods`: the server calls the client.
- `BaseMethods`: an external caller or Cell calls Base.
- `CellMethods`: an external caller or Base calls Cell.

`EntityDescription::parseMethods()` parses along those three groups, while
services use `Methods` as their server-side method set. Source:
entity_description.cpp.

`EntityMethodDescriptions::init()` assigns an `internalIndex` to each method and
adds exposed methods to `exposedMethods_`. Client methods are treated as fully
exposed, see
entity_method_descriptions.cpp.

The method contract contains at least:

- The method name.
- The parameter type sequence.
- The return value type sequence.
- Whether it is exposed.
- `internalIndex`.
- `exposedIndex`.
- The corresponding Mercury message id and sub-message id.
- Stream size.

These fields are not just debugging information. On a remote method call, both
sides locate the same method definition through the method number and the
argument stream.

## Exposed Message ID

BigWorld does not statically allocate a globally unique message number for every
entity method; it uses the combination "message range + exposed index + a
sub-message id when needed".

The chain is:

1. `EntityDescriptionMap::parse()` receives the message ranges of
   `ClientInterface` and `BaseAppExtInterface`.
2. `setExposedMessageIDs()` first counts the maximum number of exposed methods and
   client-server properties over all entities.
3. Each `EntityDescription` calls `setExposedMsgIDs()`.
4. Each `MethodDescription` obtains its message id and sub-message id through
   `ExposedMethodMessageRange::msgIDFromExposedID()`.

Source: entity_description_map.cpp
and method_description.cpp.

This is a protocol compression design that saves message id space. The price is
that protocol interpretation depends on the same EntityDef, the same ordering of
exposed methods and the same message ranges - standalone packet captures or
cross-language reimplementation become harder.

## Digest Semantics

At the end of `EntityDescriptionMap::parse()` the digest is computed, see
entity_description_map.cpp.

The input of the client-related digest is not all entity definitions but the
client types:

- `EntityDescriptionMap::addToMD5()` skips server-only types, see
  entity_description_map.cpp.
- `EntityDescription::addToMD5()` includes the entity name, client-server
  properties, client methods, exposed Base methods and exposed Cell methods, see
  entity_description.cpp.
- `DataDescription::addToMD5()` includes the property name, flags, default value
  and data type, see
  data_description.cpp.
- `MethodDescription::addToMD5()` includes the method member descriptions,
  flags, arguments and the legacy exposed index, see
  method_description.cpp.

The DB side additionally has a persistent properties digest used for database
compatibility checks. `EntityDescriptionMap::addPersistentPropertiesToMD5()`
only processes persistent entities and persistent properties, see
entity_description_map.cpp.

So a digest is not a generic file hash. It is a compatibility signature trimmed
to the runtime protocol semantics.

## The Role of process_defs

`tools/process_defs` is the exporter of the EntityDef contract. Its source header
says it parses `.def` files, generates Python objects describing all entity
types, then calls a Python callback module, `ProcessDefs.process` by default.
See main.cpp.

The structures it exports include:

- `entityTypes`: the entity type tuple.
- `constants.digest`: the EntityDef digest.
- `constants.maxExposedClientMethodCount`.
- `constants.maxExposedBaseMethodCount`.
- `constants.maxExposedCellMethodCount`.
- `constants.maxClientServerPropertyCount`.

Source: main.cpp.

Each entity description contains:

- `name`, `index`, `clientIndex`.
- `canBeOnClient`, `canBeOnBase`, `canBeOnCell`.
- `isService`, `isPersistent`.
- `clientMethods`, `baseMethods`, `cellMethods`.
- `allProperties`.
- `clientProperties`.
- `baseToClientProperties`.
- `cellToClientProperties`.

Source: main.cpp.

This shows BigWorld already has a "contract export boundary"; it simply outputs
a Python object callback. A downstream tool that needs another format should
read the same `EntityDescriptionMap` through this export chain instead of writing
a separate `.def` parser.

## Runtime Consumption Paths

Several runtime components independently parse the same EntityDef:

- BaseApp's `EntityType::init()` parses `entities.xml`, sets `s_digest_` and
  loads the Base scripts, see
  entity_type.cpp.
- CellApp's `EntityType::init()` parses the same definitions and loads the Cell
  scripts, see
  entity_type.cpp.
- DBApp's `DBApp::initEntityDefs()` initialises `EntityDefs` and prints the
  expected digest, see
  dbapp.cpp.
- Client, Bots and Replay also hold EntityDef constants, for connecting,
  replaying and method/property decoding.

This creates an engineering constraint: an EntityDef change must be deployed in
sync across all participants, otherwise connecting, login, replay, persistence
and entity migration can all fail.

## The Boundary with the Serialisation Chapter

This chapter only explains contract generation and does not repeat the low-level
encoding:

- This chapter focuses on how a `.def` becomes property/method sets, numbers,
  digests and exported structures.
- [Serialisation and EntityDef](/architecture/serialization-entitydef) focuses on
  how `DataType`, `DataDescription`, `MethodArgs` write Python objects or entity
  properties into a `BinaryStream`.
- [Communication abstractions and RPC](/architecture/communication-rpc) focuses
  on how messages are dispatched through Mercury interfaces, Bundles and
  handlers.
- [Hot reload and script migration](/architecture/hot-reload-script-migration)
  focuses on how running entities migrate after definitions change.

Mixing these layers together easily leads to the mistaken belief that "changing a
field is just changing XML". The real blast radius includes at least the protocol
numbering, digests, client sync, DB schema, script type checking and migration
paths.

## Trade-offs in the Source

BigWorld's EntityDef contract design serves the reuse of one entity definition
across multiple runtime views:

- One definition drives client, Base, Cell, DB and scripts at the same time.
- The binary protocol is compact, which suits high-frequency entity sync.
- It is directly parseable at runtime, which makes the toolchain, the editor and
  the servers shareable.
- Digests quickly reject incompatible clients.
- Components, interfaces and parent definitions provide some reuse.

The source costs also concentrate on contract stability:

- Version evolution depends on order and numbering, so reordering fields is
  risky.
- The protocol schema is implicit in EntityDef, the Mercury message ranges and
  the runtime parsing code, so external tools must reuse `process_defs` or
  `EntityDescriptionMap`.
- Historical compatibility rules such as `ClientName` and script-existence
  inference add comprehension cost.
- A `.def` carries both the network contract and the database contract, so a
  single change has a wide blast radius.
- Multiple processes parse the same definitions independently, which demands
  strong deployment consistency.

## What Source-Verified Tests Should Cover

EntityDef contract tests should not only check that the XML parses; they should
also cover:

- Whether a change in entity order changes the type id and is caught by tests.
- Whether parent definition and component property overrides keep the expected
  index.
- Whether reordering exposed methods changes the digest.
- Whether adding, removing or changing client-server properties changes the
  digest.
- Whether a server-only property change leaves the client digest untouched.
- Whether a persistent property change affects the DB persistent properties
  digest.
- Whether the `process_defs` export matches the runtime `EntityDescriptionMap`.
- Whether an incompatible client login is rejected by the digest check.
- Whether changes to a `.def` property's `Persistent`, `Identifier`, `Indexed`
  and `DatabaseLength` trigger the expected persistent digest or DB schema
  change.
- Whether a `ClientName` alias with mismatched client method count or
  client-server property count must error out.

These tests turn "implicit protocol rules" into reviewable boundaries, which
avoids mistaking a `.def` change for an ordinary script change.

## Scope of This Chapter

This chapter confirms: BigWorld has a complete EntityDef contract generation
chain. When analysing serialisation, communication, persistence and hot reload
later, `EntityDescriptionMap`, `EntityDescription`, `DataDescription` and
`MethodDescription` should serve as the shared source chain.

## Related Documents

- Serialisation implementation, see
  [Serialisation and EntityDef](/architecture/serialization-entitydef).
- Communication and RPC, see
  [Communication abstractions and RPC](/architecture/communication-rpc).
- The persistence model, see
  [Persistence and the DB thread model](/architecture/persistence-db-model).
- The hot reload mechanism, see
  [Script hot reload and migration](/architecture/hot-reload-script-migration).
- The entity model, see
  [Entity model: Base/Cell/Client](/architecture/entity-model).