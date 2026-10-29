# Hot-Reload Productionization Design


<div class="arch-hero">

`BigWorld.reloadScript` is currently a development-time tool (the source
comment says verbatim "never in a production environment"). Based on a
line-by-line audit of the reload chain, this document gives the five-piece
design needed to productionize it: versioned schema, a dual-version
compatibility window, dry-run, failure rollback, and a cross-process
consistency barrier, together with a phased landing path and test carriers.

</div>

## Audit of the current chain

The facts below are evidenced against the dev-branch source of 2026-10-15.

### Call chain

| Step | Code location | Behavior |
|------|----------|------|
| Entry | `server/cellapp/cellapp.cpp:2974` | `BigWorld.reloadScript(fullReload=True)`; takes effect only in this cellapp; cross-component behavior is the caller's responsibility |
| 1. New interpreter | `cellapp.cpp:2329-2330` | `Script::createInterpreter` + `swapInterpreter` — reload proceeds in the new interpreter while the old one stays alive on the call stack |
| 2. Load | `cellapp.cpp:2332-2340` | fullReload → `EntityType::init(true)` re-parses entity defs into `s_newTypes_` and prints the MD5 digest; otherwise `EntityType::reloadScript()` only re-imports classes; then `UserDataObjectType::load` |
| 3. Swap back | `cellapp.cpp:2350-2351` | Swap back to the old interpreter and destroy the new one (the new module table has already been copied into `s_pNewModules_`) |
| 4. Migrate | `cellapp.cpp:2353-2362` | `EntityType::migrate` (module window) → `UserDataObjectType::migrate` → `ServerEntityMailBox::migrateMailBoxes` → per-entity `Entity::migrate()` (wrapped in `MF_VERIFY`) |
| 5. Finalize | `cellapp.cpp:2364` | `EntityType::cleanupAfterReload(isFullReload)` executes in the same tick as migration |
| Failure path | `cellapp.cpp:2368-2385` | Swap back to the old interpreter; for non-full reload, `EntityType::reloadScript(true)` retries the old scripts (relies on sys.modules being untouched); cleanup; destroy the new interpreter; return false |

### Key semantic bodies

- **Module window**: the `isFullReload=false` branch of `EntityType::migrate`
  has been extracted to `EntityType::swapInModules` (`entity_type.ipp`),
  assertable in cellapp unit tests since 2026-10-15.
- **Entity class swap**: `Entity::migrate`
  (`server/cellapp/entity.cpp:4245`) — type-id guard (missing type or id
  mismatch → judged obsolete, return false) → swap `pEntityType_` and hang
  the old type via `EntityType::old()` → `PyObject_SetAttrString(__class__)`
  (failure → restore `pEntityType_`, return false) → call the script-side
  `onMigrate`.
- **Dual-version hook**: `EntityType::old()` +
  `MethodDescription::supersede(CELL)` keep old method signatures resolvable
  inside the window.

## Failure-mode list

| # | Mode | Evidence | Consequence |
|---|------|------|------|
| F1 | In release, entity-migration failure is silently swallowed | `MF_VERIFY` expands to `(exp)` under `_RELEASE` (`cstdmf/debug.hpp:428`); the return value at `cellapp.cpp:2361` is consumed by nobody | The entity stays on its old class while `sys.modules`/`s_curTypes_` are already the new version — a half-migrated state; a dev build instead aborts mid-reload |
| F2 | An `onMigrate` script exception does not count as failure | At `entity.cpp:4297` `Script::call(..., okIfFnNull=true)` only prints the exception; `migrate()` still returns true | Script-level half-migration with no failure count |
| F3 | The dual-version window has zero length | `cleanupAfterReload(isFullReload)` is called in the same tick as migration (`cellapp.cpp:2364`) and immediately breaks the link with `old(NULL)` | The `supersede` mechanism is effectively valid only inside the migration loop; in-flight old-signature calls at the boundary have no grace period |
| F4 | No cross-process consistency | `cellapp.cpp:2944-2946` documents explicitly that "the user is responsible for calling on all server components"; the digest is computed (`entity_type.cpp:280-287`) but only printed, never compared or propagated | When cellapp has migrated but baseapp/dbmgr still use old defs, cross-process type-id drift |
| F5 | Custom data-type instances are not migrated | `cellapp.cpp:2951-2960` requires the user to manually repoint `__class__` per instance | Depends on the caller's scripts being correct; no engine-side guarantee |
| F6 | The batch-migration-complete hook is dead code | `Entity::migratedAll` (declared `entity.hpp:284`, defined `entity.cpp:4308`) and `Base::migratedAll` (`base.cpp:2837`) had no caller anywhere in the tree | `onMigratedAll` never fires. **Closed 2026-10-27 (batch 52)**: both migrate loops now call `migratedAll()` after the batch (`cellapp.cpp` `MIGRATED_ALL` plan action; `baseapp/script_bigworld.cpp`), and the ordering is pinned by the `ReloadScript` plan tests |
| F7 | After rollback the old description is still superseded | At `entity.cpp:4267` `pNewType->old(pOldType)` runs before the `__class__` attempt (`entity.cpp:4275`); at `entity_type.cpp:168-185` the first call unconditionally `supersede(CELL)`s the old description (all cell methods are renamed with an `old_` prefix and the name map is rebuilt); on `__class__` failure `entity.cpp:4287` rolls back `pEntityType_ = pOldType`, but nothing in the tree un-supersedes (batch 33 measured: `find("cellTick")`→NULL, `find("old_cellTick")` reachable, `internalMethod`/`exposedMethod` index dispatch unaffected, digest drifts with the renames) | A "successfully" rolled-back entity loses all by-name mailbox method lookups (`mailbox.cpp:122/240` `findMethod` returns NULL → script-side error), while exposed dispatch by msgID still works — a half-broken entity; the rollback design in §4 must also un-supersede (or compensate with window-period re-resolution), and the schema digest must be captured before supersede |

## Design

### 1. Versioned schema

Use the existing `entityDescriptionMap` MD5 digest as the schema version
number (do not invent a new versioning scheme):

- change the digest from "printed only" to "queryable": mount a (read-only)
  cellapp watcher for the barrier and manual verification;
- `BigWorld.reloadScript` gains an optional `expectedDigest` parameter: when
  supplied it is compared against this load's result; a mismatch is judged a
  failure directly (existing failure path, zero migration).

### 2. Dual-version compatibility window

- split `cleanupAfterReload` into two phases: the migration tick only clears
  `s_pNewModules_`; the `pOldSelf_` link break becomes deferred — a tick hook
  executes it after a configured window (seconds or tick count; default 0
  preserves current behavior).
- Inside the window the `EntityType::old()` chain is valid; note the measured
  F7 boundary: after supersede, by-name lookups are already broken (only the
  `old_`-prefixed names remain), so the window preserves only
  **index/msgID dispatch** — by-name re-resolution needs a separate design.
  On window expiry the link is broken uniformly and the residual count is
  alarmed.

### 3. dry-run

- reload gains a mode bit: execute only steps 1–2 (init/reloadScript in the
  new interpreter + UDO load + full-type import), then destroy the new
  interpreter directly without any migrate.
- It is essentially reuse of the existing failure path's "zero-touch"
  property: the old interpreter is never swapped out, so risk is zero; output
  is "the set of types that loaded successfully + digest + the list of types
  expected to become invalid".
- This is the cheap probe preceding F1/F4: dry-run all components first; only
  reload for real once digests agree and no types become invalid.

### 4. Failure rollback

- The per-entity `Entity::migrate` loop becomes **collect-decide**: failures
  (both F1 failure classes plus the F2 script exceptions) are recorded and
  the loop continues or stops immediately according to a policy bit in the
  reload request; the return value no longer goes through `MF_VERIFY`.
- Rollback of already-migrated entities = the inverse of `Entity::migrate`
  (swap `pEntityType_` and `__class__` back to the old type, still reachable
  through the `old()` chain). Note F7: the inverse must also revert the
  `supersede` renames (otherwise by-name lookups stay broken); the
  versioned-schema digest capture point must likewise be before supersede.
- The default decision is "stop immediately + roll back", preserving the
  L34 acceptance criterion "no half-migrated state on failure".

### 5. Cross-process consistency barrier

In two phases:

- **P1 (no new RPC)**: dry-run + the `expectedDigest` parameter place the
  barrier on the caller — a cross-component orchestration script dry-runs all
  components first, collects digests, and only reloads each component for
  real once they agree. The engine provides only primitives.
- **P2 (engine side)**: cellappmgr broadcasts the reload request, collects
  each component's ack (digest + readiness bit), then issues a unified
  go-ahead; this needs cellappmgr/control-plane changes and is a separate
  project.

## Phased landing path

| Phase | Content | Test carrier | Status |
|----|------|----------|------|
| P0 | Module-window semantics testable (`swapInModules` extraction) | cellapp/unit_test (landed, 2 cases) | Done 2026-10-15 |
| P1 | dry-run mode + queryable digest + `expectedDigest` | The prepare phase must run `EntityType::init` (BWResource + entity defs + script import), unreachable in the current cellapp/unit_test harness — the same carrier gap as P2; needs the C-tier item first or cluster materials | Todo (carrier gap) |
| P2 | Failure collect-decide + entity-level rollback | Decision core + outcome-class seam landed 2026-10-27 (`entity_migration_plan.hpp`; `Entity::migrate(FailureClass*)`; 7 cases in cellapp_test); all four inverse primitives landed 2026-10-29 (`EntityDescription::unsupersede`, 3 cases in entitydef_test); attribution decision modeled 2026-10-29 (`planRollback`, 3 more cases in cellapp_test) | In progress: engine wiring + rollback execution pending — the inverse primitives are all expressible and the attribution decision is modeled; what remains is the execution carrier (see below) |
| P3 | Dual-version compatibility window | Needs a cellapp tick-level carrier | Todo |
| P4 | Engine-side barrier (the P2 period) | Needs the cluster black-box chain (C tier) | Todo |

## Acceptance mapping (corresponds to todo L34)

- "Migration path tests cover `Entity::migrate` semantic preservation" → P2;
  carrier gap: `Entity::migrate` needs an Entity instance plus script types,
  and the current cellapp/unit_test only compiles in low-coupling TUs (same
  source as the C-tier "server-side reload migration semantic test carrier").
- "No half-migrated state on failure" → a load-time failure today is naturally
  free of half-migration (the failure path never triggers migrate);
  migration-time half-migration is closed by P2's collect-decide + rollback.
  **Since batch 36 (2026-10-15) this property has a test carrier**:
  `ReloadScript::planReload` (`reload_script_plan.hpp`) turns
  `CellApp::reloadScript`'s action sequence into data; the failure tail
  asserts no `MIGRATE_*` appears, and the success tail asserts the scratch
  interpreter is destroyed before migrate with cleanup last (7 cases,
  cellapp_test). The same extraction truthfully reports **F8** — on full
  reload failure `EntityType::init(true)` (`entity_type.cpp:262`) has already
  called `DataType::clearStaticsForReload()` to clear `s_singletonMap_`, and
  the failure path does not rebuild it; `planReload` flags this with the
  `dataTypesWiped` field. Migration-time half-migration and F8 share the P2
  carrier gap.
- **Since batch 51 (2026-10-27) the P2 decision core has a test carrier**:
  `EntityMigration::classify` (`entity_migration_plan.hpp`) names the three
  ways `Entity::migrate` can fail — GUARD_OBSOLETE touches nothing,
  CLASS_SWAP_REJECTED restores `pEntityType_`, and SCRIPT_EXCEPTION is still
  reported as success — and is exactly what the new
  `Entity::migrate(FailureClass*)` overload reports; `EntityMigration::advance`
  carries the collect-decide policy (STOP_ON_FAILURE / CONTINUE_ON_FAILURE,
  the stop is sticky, rollback is needed only when something migrated first).
  The same extraction records why the migrate loop still `MF_VERIFY`s instead
  of stopping and rolling back. Batch 51 recorded that the inverse was not
  fully expressible, which had two parts wrong (corrected in batch 53): the
  old-type link IS clearable through the public setter `old(NULL)` (the early
  return in `EntityType::old` fires only when `pOldSelf_` and the argument are
  both non-NULL, so a NULL argument always clears), and the supersede rename
  DOES have an inverse — `EntityDescription::unsupersede` strips exactly one
  leading `old_` prefix per call, pairing with `supersede()`'s one-per-call
  stacking, and it is pinned by entitydef_test. So all four inverse primitives
  are expressible (`rollbackInverseComplete()` is true). Batch 53's ledger had
  also recorded attribution — who runs the per-pair type-level steps — as the
  remaining blocker; batch 54 corrected that overstatement: the decision is
  decidable and now modeled as `planRollback` (see below), so
  `rollbackAttributionComplete()` is true. What keeps the loop unwired is the
  execution carrier, not a missing decision.
- **Since batch 53 (2026-10-29) the supersede rename has its inverse**: `EntityDescription::unsupersede(component)` / `EntityMethodDescriptions::unsupersede()` strip exactly one leading `old_` prefix per call and rebuild the name map, the exact per-call pairing with `supersede()` (which adds one prefix per call, pinned by `EntityDescription_supersedeTwiceStacksPrefixes`). Three cases in entitydef_test pin it: one supersede + one unsupersede restores the original names to the same objects and the exposed dispatch name (base/client lists untouched), a doubled supersede needs a doubled unsupersede, and an unsupersede on a never-superseded description changes nothing (the strip only fires on a real prefix, so a miscounted strip cannot invent renames). This is the mechanism F7's rollback needs for REVERT_SUPERSEDE_RENAME; the strip's attribution is settled by batch 54's `planRollback` below.
- **Since batch 54 (2026-10-29) the rollback attribution decision is modeled**: `EntityMigration::planRollback` (`entity_migration_plan.hpp`) decides, for one touched entity, which rollback steps run — the entity-level restores only when the entity fully migrated (a rejected class swap already restored `pEntityType_` itself; a raised onMigrate hook reports success yet fully migrated), and the pair-level strip and link-clear only for the first touched entity of a renamed pair in rollback order, so no entity has to "own" a rename it did not cause. `touchedEntity` pins the rollback-set boundary: a guard-rejected entity never reaches the `old()` call, so it touches neither the entity nor its pair. This corrects batch 53's overstatement that attribution was the blocker; the boundary that actually keeps the migrate loop `MF_VERIFY`ing is the execution carrier — the loop and its rollback call Entity methods on live entities, unreachable in cellapp/unit_test (the C-tier gap).
