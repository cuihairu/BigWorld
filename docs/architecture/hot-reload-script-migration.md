# Script Hot Reload and Migration

> **English** | [中文](hot-reload-script-migration.md)

<div class="arch-hero">

BigWorld's `reloadScript` is not a modern, safe production hot-update system. It
is a high-risk runtime migration mechanism: a new Python interpreter loads new
scripts and EntityDefs, then the old interpreter switches back and migrates
types, mailboxes, and existing entity objects.

</div>

## Key Takeaways

The source is explicit: `CellApp::reloadScript` should be used carefully and must
never be used in production:
cellapp.cpp.

The actual flow is:

1. Create a new Python interpreter.
2. Switch to the new interpreter.
3. Load EntityType / EntityDef / UDO definitions.
4. Switch back to the old interpreter.
5. Destroy the new interpreter.
6. Migrate EntityTypes.
7. Migrate UDOs and mailboxes.
8. Iterate live Base or Cell entities and replace `__class__`.
9. Invoke script `onMigrate`.
10. Clean temporary reload state.

Key source code:

- `Script::createInterpreter()` creates the new interpreter: script.cpp.
- `Script::swapInterpreter()` switches interpreters: script.cpp.
- BaseApp-side `reloadScript()`: script_bigworld.cpp.
- CellApp-side `reloadScript()`: cellapp.cpp.
- `Entity::migrate()` replaces `__class__` and calls `onMigrate`: entity.cpp.
- `Base::migrate()` has comparable migration logic: base.cpp.

## Why a New Interpreter

Importing new scripts directly into the old interpreter has several problems:

- old modules remain in `sys.modules`;
- the current Python call stack may still execute old code;
- failed EntityType initialization must be reversible;
- new scripts and EntityDefs must be proven loadable before switching.

The comment on `Script::createInterpreter()` explains that it creates a new
interpreter for single-threaded applications and can be selected with
`swapInterpreter()`:
script.cpp.

After creating the interpreter, it:

- copies the current interpreter's `sys.path`;
- merges init-time modules;
- switches back to the original interpreter.

Source: script.cpp.

## BaseApp Hot-Reload Chain

BaseApp's `BigWorld.reloadScript()` starts here:
script_bigworld.cpp.

Flow:

<MermaidDiagram title="BaseApp reloadScript">
flowchart TD
  A[BigWorld.reloadScript] --> B[Script createInterpreter]
  B --> C[swap to new interpreter]
  C --> D{full reload?}
  D -- yes --> E[EntityType init isReload]
  D -- no --> F[EntityType reloadScript]
  E --> G{isOK}
  F --> G
  G -- yes --> H[swap back old interpreter]
  H --> I[destroy new interpreter]
  I --> J[EntityType migrate]
  J --> K[ServerEntityMailBox migrateMailBoxes]
  K --> L[iterate Bases migrate]
  L --> M[cleanupAfterReload]
  G -- no --> N[swap back old interpreter]
  N --> O[try recover old scripts if partial reload]
  O --> P[cleanup and destroy new interpreter]
</MermaidDiagram>

On success, BaseApp iterates `BaseApp::instance().bases()` and calls `migrate()`
on every Base:
script_bigworld.cpp.

On failure, a non-full reload attempts `EntityType::reloadScript(true)` to
recover the old scripts:
script_bigworld.cpp.

## CellApp Hot-Reload Chain

CellApp adds UDO migration:

- create and switch to a new interpreter;
- call `EntityType::init(true)` or `EntityType::reloadScript()`;
- call `UserDataObjectType::load()`;
- switch back to the old interpreter;
- destroy the new interpreter;
- call `EntityType::migrate()`;
- call `UserDataObjectType::migrate()`;
- call `ServerEntityMailBox::migrateMailBoxes()`;
- iterate `Entity::population()` and migrate each entity;
- call `EntityType::cleanupAfterReload()`.

Source: cellapp.cpp.

CellApp reload therefore combines spatial entities, ghosts, UDOs, and mailboxes.
Its risk is far higher than an ordinary script reload.

## Entity Migration

The core logic of `Entity::migrate()` is:

1. Look up the new `EntityType` by the old type name.
2. Check that old and new type IDs agree.
3. Update `pEntityType_`.
4. Call `PyObject_SetAttrString(this, "__class__", newPyType)`.
5. Invoke script `onMigrate(self)`.

Source: entity.cpp.

If `__class__` cannot be set, migration fails and the old type is restored:
entity.cpp.

There is also a TODO in the source: iterate properties and migrate them, but the
current implementation only calls `onMigrate`:
entity.cpp.

That is the central risk: changed property structures are not automatically
migrated. Business scripts must handle compatibility themselves.

## Mailbox Migration

Reload also calls `ServerEntityMailBox::migrateMailBoxes()`:

- BaseApp side: script_bigworld.cpp.
- CellApp side: cellapp.cpp.

Mailbox types are bound to scripts and entity types. Replacing a Python class is
not enough; remote references must migrate to the new type metadata as well.

## Why It Is Not Production-Safe

The source warning has structural causes:

- Live objects run on the old Python call stack; the old interpreter cannot simply be discarded.
- Dynamic `__class__` replacement has strict constraints with Python type layout and C-extension objects.
- EntityDef field changes can break network streams, persistence, default values, and property access.
- `onMigrate` is only a script hook, so migration quality depends on business code.
- Handlers, timers, and network messages may continue entering during migration.
- Partial failure only receives best-effort recovery, not transactional rollback.
- BaseApp/CellApp/DBApp/client protocol versions must remain consistent across processes.

BigWorld script reload is therefore appropriate for development and debugging,
not production zero-downtime upgrades.

## Complete Hot-Reload Call Chains

### CellApp hot reload

Source entry: cellapp.cpp:2322

<div class="flow-strip">
  <span class="flow-node">CellApp::reloadScript()</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">Script::createInterpreter()</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">load new scripts</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">Script::swapInterpreter()</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">iterate all Entities</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">Entity::migrate()</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">migrate mailboxes</span>
</div>

### BaseApp hot reload

Source entry: script_bigworld.cpp:1509

<div class="flow-strip">
  <span class="flow-node">BigWorld.reloadScript()</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">Script::createInterpreter()</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">load new scripts</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">Script::swapInterpreter()</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">iterate bases_</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">Base::migrate()</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">migrate mailboxes</span>
</div>

### Entity migration

Source entry: entity.cpp:4245

<div class="flow-strip">
  <span class="flow-node">Entity::migrate()</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">get new EntityType</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">check type-ID consistency</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">update pEntityType_</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">PyObject_SetAttrString("__class__")</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">invoke onMigrate()</span>
</div>

### Failure recovery

Source entry: script_bigworld.cpp:1525

<div class="flow-strip">
  <span class="flow-node">migration failure</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">check full-reload flag</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">EntityType::reloadScript(true)</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">recover old scripts</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">attempt old-type recovery</span>
</div>

## Relationship to Serialization

The most dangerous hot-update changes are not function-body edits. They are type
contract changes:

- properties added, removed, or retyped;
- method parameters changed;
- Base/Cell/Client data domains changed;
- UDO types changed;
- mailbox method tables changed;
- persistent fields changed.

Such changes penetrate `EntityDescription`, `DataDescription`, `MethodArgs`, and
Mercury bundles. Read this chapter together with
[Serialization and EntityDef](/architecture/serialization-entitydef).

## Source Trade-Offs

The mechanism exists because runtime scripts and entity objects are tightly
coupled:

- iteration on a large MMO script suite is expensive; development needs fast reload.
- Python permits some dynamic type replacement.
- EntityDef and scripts are tightly coupled, so an engine-level migration entry was needed.
- In a multi-process architecture, reloading one App's scripts is faster than restarting the whole cluster.

The source boundaries are equally clear:

- no versioned schema;
- no dual-version protocol compatibility window;
- no transactional rollback;
- no dry run before migration;
- no cross-process consistency barrier.

## Source-Code Verification Focus

Hot-reload tests should cover the real boundaries of script reload and entity
migration:

- `CellApp::reloadScript()` must not corrupt object state during entity migration, destruction, or callback suppression.
- `Entity::migrate()` should preserve EntityID, mailbox, Base/Cell relationships, and persistent properties.
- `onMigrate` failure must have a clear error path and must not leave a half-migrated entity.
- Incompatible EntityDef property, method-parameter, data-domain, or UDO changes should be explicitly rejected.
- Multi-process reload should verify digest consistency across BaseApp, CellApp, DBApp, and clients.
- Method calls, property synchronization, database writes, and offload streams after reload must continue using the same EntityDef interpretation.

## Chapter Boundary

This chapter explains script hot reload. The next chapter analyzes testing and
controlled time: hot reload, reliable UDP, timers, resends, and Cell migration
all need reproducible time and fault injection.
