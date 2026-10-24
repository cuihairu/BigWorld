# Persistence and the DB Thread Model



<div class="arch-hero">

BigWorld persistence is not business code writing SQL directly. BaseApp
coordinates Base/Cell data, backup, and archiving. DBApp exposes the `IDatabase`
abstraction, and the MySQL backend moves blocking database operations onto
background tasks.

</div>

## Conclusion

The write path is:

<div class="flow-strip">
  <span class="flow-node">Base tick</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">archive/backup</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">Base.writeToDB</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">Cell writeToDBRequest</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">DBApp writeEntity</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">IDatabase</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">DB background task</span>
</div>

Key sources:

- BaseApp ticks call `backup()` and `archive()`:
  baseapp.cpp.
- Base archiving:
  base.cpp.
- Cell data request:
  base.cpp.
- Archiver:
  archiver.cpp.
- DBApp `IDatabase` pass-through:
  dbapp.hpp.
- MySQL background work:
  mysql_database.cpp.

## BaseApp Responsibilities

BaseApp is not merely a DBApp proxy. It decides archive and backup timing, calls
`onPreArchive` and `onWriteToDB`, requests Cell data, handles the secondary DB,
controls DBApp Channel backpressure, and coordinates backup/offload while
retiring.

Persistence is therefore coupled to runtime state governance.

## Automatic Archiving

`Base::autoArchive()` respects `shouldAutoArchive_`; `NEXT_ONLY` disables the
next automatic archive after success. `Base::archive()` skips entities with
pending Cell destruction, invokes `onPreArchive`, returns if the script cancels
or the Base has been destroyed, and otherwise calls
`writeToDB(WRITE_BASE_CELL_DATA)`.

## Cell Data Requests

If a Base has a Cell entity but lacks Cell data during `writeToDB()`, it sends
`CellAppInterface::writeToDBRequest` to the Cell. The reply contains Cell data,
which is merged with Base data before being sent to DBApp.

This reflects the architectural split: Base coordinates persistence while Cell
holds spatial and local entity data.

<MermaidDiagram title="Cell data request">
sequenceDiagram
    participant Base as Base Entity
    participant Bundle as Mercury Bundle
    participant CellApp as CellApp
    participant Cell as Cell Entity

    Base->>Base: writeToDB() needs Cell data
    Base->>Bundle: startRequest(writeToDBRequest)
    Bundle->>Bundle: write EntityID
    Base->>CellApp: send request
    CellApp->>Cell: collect persistence data
    Cell->>Bundle: write Cell data
    CellApp->>Base: reply with Cell data
    Base->>Base: merge Base and Cell data
    Base->>CellApp: send complete data to DBApp
</MermaidDiagram>

## DBApp and MySQL Background Tasks

DBApp receives the merged write stream. `WriteEntityHandler::writeEntity()`
decides whether the operation is an initial insert, ordinary update, or logoff.
Only `WRITE_BASE_CELL_DATA` passes the stream to the database layer.

The MySQL backend creates a `PutEntityTask`, and the task performs insert or
update outside the main game thread. `EntityTypeMapping::visit()` uses
`BASE_DATA | CELL_DATA | ONLY_PERSISTENT_DATA` to traverse persistent properties
and generate SQL parameters.

The database never inspects Python attribute objects directly. It consumes a
persistence stream ordered by EntityDef.

## MySQL Mapping

`PropertyMapping` abstracts stream-to-database and database-to-stream conversion.
Its factory selects an implementation by `DataType`:

- Numeric, string, vector, and similar types usually map to columns.
- `CLASS` and `FIXED_DICT` use composite mappings.
- `ARRAY` and `TUPLE` may use sequence child tables or blob storage.
- `USER_TYPE` uses user-defined mappings; binding failures stop DBApp so the
  code does not silently rewrite table structure.

Runtime metadata is separate from business properties. `bigworldLogOns` stores
the mailbox associated with each database ID, and autoload flags are updated
independently. A persisted entity therefore includes both EntityDef data and
engine runtime metadata.

## How This Differs from an ORM

<div class="decision-table">

| Dimension | BigWorld | Typical ORM |
| --- | --- | --- |
| Persistence unit | EntityDef-driven persistent stream | In-memory object/session |
| Write trigger | Archive, writeToDB, logoff | Transaction flush/save |
| Update granularity | Rebuild persistent view | Often dirty fields/objects |
| Mapping source | `.def` and PropertyMapping | Class annotations/models |
| Complex types | Columns, child tables, or blobs | ORM-specific |
| Runtime semantics | AOI, Proxy, Base/Cell, login metadata | Usually out of scope |
</div>


BigWorld borrows object-mapping ideas, but it is first a distributed entity
engine persistence backend, not a general-purpose ORM.

## Secondary DB

BaseApp can write a secondary SQLite database when a secondary DB is configured,
a complete database ID exists, and the operation is not autoload/delete/primary
or explicit-DBID handling. The entity is written with game time; explicit writes
commit the secondary DB.

`Archiver::tickSecondaryDB()` ticks and commits based on
`maxCommitPeriodInTicks`.

## Backpressure and Consistency

The Archiver spreads archive work over time and can skip an archive when the
DBApp Channel window indicates pressure. BaseApp retirement integrates backup
and offload so data is not lost while ownership is changing. This makes the DB
path sensitive to network backpressure, DB latency, and lifecycle state.

## Verification Priorities

- Archive timing, script hooks, Cell data request, and complete stream generation
  must run in the expected order.
- Initial insert, ordinary update, and logoff must dispatch to distinct DB
  operations.
- Background tasks must not perform blocking MySQL work on the game tick thread.
- EntityDef property mappings must round-trip stream data and database state.
- Complex types must preserve column/child-table/blob semantics.
- `bigworldLogOns` and autoload metadata must remain consistent with Base
  mailbox state.
- Secondary DB commit period and GameTime metadata must be observable.
- DBApp Channel pressure must prevent unsafe archive bursts.

## Scope

This page explains the persistence and DB threading model. Database schema
migration, backup recovery, and deployment operations belong to their separate
operational chapters.
