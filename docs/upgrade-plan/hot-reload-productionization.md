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
| Entry | `server/cellapp/cellapp.cpp:2377` | `BigWorld.reloadScript(fullReload=True)`; takes effect only in this cellapp; cross-component behavior is the caller's responsibility |
| 1. New interpreter | `cellapp.cpp:2329-2330` | `Script::createInterpreter` + `swapInterpreter` — reload proceeds in the new interpreter while the old one stays alive on the call stack |
| 2. Load | `cellapp.cpp:2332-2340` | fullReload → `EntityType::init(true)` re-parses entity defs into `s_newTypes_` and prints the MD5 digest; otherwise `EntityType::reloadScript()` only re-imports classes; then `UserDataObjectType::load` |
| 3. Swap back | `cellapp.cpp:2350-2351` | Swap back to the old interpreter and destroy the new one (the new module table has already been copied into `s_pNewModules_`) |
| 4. Migrate | `cellapp.cpp:2353-2362` | `EntityType::migrate` (module window) → `UserDataObjectType::migrate` → `ServerEntityMailBox::migrateMailBoxes` → per-entity `Entity::migrate()` (wrapped in `MF_VERIFY`) |
| 5. Finalize | `cellapp.cpp:2473` | `EntityType::cleanupAfterReload(isFullReload)` executes in the same tick as migration |
| Failure path | `cellapp.cpp:2368-2385` | Swap back to the old interpreter; for non-full reload, `EntityType::reloadScript(true)` retries the old scripts (relies on sys.modules being untouched); cleanup; destroy the new interpreter; return false |

### Key semantic bodies

- **Module window**: the `isFullReload=false` branch of `EntityType::migrate`
  has been extracted to `EntityType::swapInModules` (`entity_type.ipp`),
  assertable in cellapp unit tests since 2026-10-15.
- **Entity class swap**: `Entity::migrate`
  (`server/cellapp/entity.cpp:4258`) — type-id guard (missing type or id
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
| F1 | In release, entity-migration failure is silently swallowed | `MF_VERIFY` expands to `(exp)` under `_RELEASE` (`cstdmf/debug.hpp:447`); the return value at `cellapp.cpp:2481` is consumed by nobody | The entity stays on its old class while `sys.modules`/`s_curTypes_` are already the new version — a half-migrated state; a dev build instead aborts mid-reload |
| F2 | An `onMigrate` script exception does not count as failure | At `entity.cpp:4321` `Script::call(..., okIfFnNull=true)` only prints the exception; `migrate()` still returns true | Script-level half-migration with no failure count |
| F3 | The dual-version window has zero length | `cleanupAfterReload(isFullReload)` is called in the same tick as migration (`cellapp.cpp:2473`) and immediately breaks the link with `old(NULL)` | The `supersede` mechanism is effectively valid only inside the migration loop; in-flight old-signature calls at the boundary have no grace period |
| F4 | No cross-process consistency | `cellapp.cpp:3049-3050` documents explicitly that "the user is responsible for making sure that reloadScript is called on all server components"; the digest is computed (`entity_type.cpp:289-295`) but only printed, never compared or propagated | When cellapp has migrated but baseapp/dbmgr still use old defs, cross-process type-id drift |
| F5 | Custom data-type instances are not migrated | `cellapp.cpp:2951-2960` requires the user to manually repoint `__class__` per instance | Depends on the caller's scripts being correct; no engine-side guarantee |
| F6 | The batch-migration-complete hook is dead code | `Entity::migratedAll` (declared `entity.hpp:299`, defined `entity.cpp:4347`) and `Base::migratedAll` (`base.cpp:2837`) had no caller anywhere in the tree | `onMigratedAll` never fires. **Closed 2026-10-27 (batch 52)**: both migrate loops now call `migratedAll()` after the batch (`cellapp.cpp` `MIGRATED_ALL` plan action; `baseapp/script_bigworld.cpp`), and the ordering is pinned by the `ReloadScript` plan tests |
| F7 | After rollback the old description is still superseded | At `entity.cpp:4283` `pNewType->old(pOldType)` runs before the `__class__` attempt (`entity.cpp:4300`); at `entity_type.cpp:191` the first call unconditionally `supersede(CELL)`s the old description (all cell methods are renamed with an `old_` prefix and the name map is rebuilt); on `__class__` failure `entity.cpp:4308` rolls back `pEntityType_ = pOldType`, but nothing in the tree un-supersedes (batch 33 measured: `find("cellTick")`→NULL, `find("old_cellTick")` reachable, `internalMethod`/`exposedMethod` index dispatch unaffected, digest drifts with the renames) | A "successfully" rolled-back entity loses all by-name mailbox method lookups (`mailbox.cpp:116/201` `findMethod` returns NULL → script-side error), while exposed dispatch by msgID still works — a half-broken entity; the rollback design in §4 must also un-supersede (or compensate with window-period re-resolution), and the schema digest must be captured before supersede |

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
| P1 | dry-run mode + queryable digest + `expectedDigest` | Queryable digest + `expectedDigest` landed 2026-11-21 (batch 56: `EntityType::schemaDigestQuote`, `BigWorld.schemaDigest`, `BigWorld.reloadScript(isFullReload, expectedDigest)`, `ReloadScript::digestMatches`; 4 cases in `test_reload_script_plan.cpp` + the digest dimension swept through the outcome matrix); the dry-run mode bit landed the same day (batch 58, see below). Only the cross-component orchestration still needs the prepare phase running `EntityType::init` (BWResource + entity defs + script import), unreachable in the current cellapp/unit_test harness; needs the C-tier item first or cluster materials | In progress: barrier parameter + queryable digest + dry-run mode done; cross-component orchestration pending (carrier gap) |
| P2 | Failure collect-decide + entity-level rollback | Decision core + outcome-class seam landed 2026-10-27 (`entity_migration_plan.hpp`; `Entity::migrate(FailureClass*)`; 7 cases in cellapp_test); all four inverse primitives landed 2026-10-29 (`EntityDescription::unsupersede`, 3 cases in entitydef_test); attribution decision modeled 2026-10-29 (`planRollback`, 3 more cases in cellapp_test); engine wiring landed 2026-11-12 (`EntityMigration::rollout` driven by `CellApp::reloadScript`, `Entity::rollbackMigration` + `EntityType::unsupersedeOldDescription`, 8 cases in `test_migration_rollout.cpp` + the touched-counter cases in `test_entity_migration_plan.cpp`) | Engine wiring done — failure stops the pass and rolls the touched entities back in reverse order; remaining: versioned-schema digest, dual-version window, dry-run, cross-process barrier (P1/P3/P4) |
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
  reload failure `EntityType::init(true)` (`entity_type.cpp:266`) has already
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
  the stop is sticky, rollback is needed only when something was touched —
  the touched counter, not the migrated counter, decides; the CR-P2-20 case
  of a half-migrated first failure is in the rollback set). The same
  extraction records why the migrate loop still `MF_VERIFY`s instead
  of stopping and rolling back (until batch 55 wired it; see below).
  Batch 51 recorded that the inverse was not
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
  `rollbackAttributionComplete()` is true. The remaining carrier gap closed
  with batch 55's `rollout()` (below).
- **Since batch 53 (2026-10-29) the supersede rename has its inverse**: `EntityDescription::unsupersede(component)` / `EntityMethodDescriptions::unsupersede()` strip exactly one leading `old_` prefix per call and rebuild the name map, the exact per-call pairing with `supersede()` (which adds one prefix per call, pinned by `EntityDescription_supersedeTwiceStacksPrefixes`). Three cases in entitydef_test pin it: one supersede + one unsupersede restores the original names to the same objects and the exposed dispatch name (base/client lists untouched), a doubled supersede needs a doubled unsupersede, and an unsupersede on a never-superseded description changes nothing (the strip only fires on a real prefix, so a miscounted strip cannot invent renames). This is the mechanism F7's rollback needs for REVERT_SUPERSEDE_RENAME; the strip's attribution is settled by batch 54's `planRollback` below.
- **Since batch 54 (2026-10-29) the rollback attribution decision is modeled**: `EntityMigration::planRollback` (`entity_migration_plan.hpp`) decides, for one touched entity, which rollback steps run — the entity-level restores only when the entity fully migrated (a rejected class swap already restored `pEntityType_` itself; a raised onMigrate hook reports success yet fully migrated), and the pair-level strip and link-clear only for the first touched entity of a renamed pair in rollback order, so no entity has to "own" a rename it did not cause. `touchedEntity` pins the rollback-set boundary: a guard-rejected entity never reaches the `old()` call, so it touches neither the entity nor its pair. This corrects batch 53's overstatement that attribution was the blocker; what remained was the execution carrier, closed by batch 55 (below).
- **Since batch 55 (2026-11-12) the rollback engine is wired into the engine**: `EntityMigration::rollout` (`entity_migration_plan.hpp`) is the collect-decide pass as a template over the entity population and four operation functors (get/migrate/alive/rollback), so the loop logic — stop stickiness, reverse-order rollback, per-pair attribution, destroyed entities skipped — is pinned end to end on fake entities in `test_migration_rollout.cpp` (8 cases), while the real bindings stay three thin functors in `CellApp::reloadScript` (`cellapp.cpp`): `MIGRATE_ENTITIES` runs `rollout(Entity::population(), …)` under `STOP_ON_FAILURE`, `MIGRATED_ALL` is skipped after a stopped or failed pass, and the failure report names the first failure class and whether a rollback ran. `Entity::rollbackMigration(old, new, plan)` (`entity.cpp`) executes one entity's `RollbackPlan` — the `pEntityType_`/`__class__` restore, `EntityType::unsupersedeOldDescription()` (the `EntityDescription::unsupersede(CELL)` wrapper), and the `old(NULL)` link clear. Batch 55 also fixed CR-P2-20: `rollbackNeeded` keys on the touched counter, not the migrated counter (a half-migrated first failure is in the rollback set), pinned by `MigrationRollout_FirstEntityHookFailureRollsItselfBack` and the data-level `EntityMigrationPlan_FirstFailureTouchesRollsBack`. Residual exposure (unchanged, CR-P2-08): the live map is iterated in place like the adjacent `MIGRATED_ALL` loop, so a script hook destroying a neighbour mid-pass is narrowed by the destroyed-entity check in the rollback half but not closed for the migration half.
- **Since batch 56 (2026-11-21) the schema digest is queryable and the barrier parameter exists**: `EntityType::init` stores the `EntityDescriptionMap` MD5 it was already printing (`s_schemaDigestQuote_`, exposed as `EntityType::schemaDigestQuote()` and cleared by `clearStatics`), so the digest is captured while the parsed map is in hand and **before any migrate runs** — the F7 note that it must be captured before supersede. `BigWorld.schemaDigest()` returns it to script for manual verification, and `BigWorld.reloadScript(isFullReload, expectedDigest)` judges the expectation straight after the defs load: an empty expectation places no constraint (default behaviour unchanged), a mismatch is judged a failure on the spot, so the reload takes the failure path and migrates nothing — including skipping the UDO load, and counting as `dataTypesWiped` on a full reload because `EntityType::init(true)` had already cleared the registered data types (F8). The decision is pinned at the data layer as `ReloadScript::digestMatches` plus `PlanInputs::digestMatched` (default true), with the digest swept through `ReloadScriptPlan_AllOutcomeCombinationsFit` and the two failure-tail shapes asserted in `ReloadScriptPlan_DigestMismatchRunsNoMigrate`. What remains for P1 is the dry-run mode bit and the cross-component orchestration that consumes `schemaDigest()`.
- **Since batch 57 (2026-11-21) the digest is also mounted as a read-only watcher** (design section 1's second queryable surface, next to `BigWorld.schemaDigest()`): `CellApp::addWatchers` adds `schemaDigest` via the read-only function-watcher overload (`MF_WATCH( "schemaDigest", &schemaDigestForWatcher, … )`, `cellapp.cpp`), where the thin `schemaDigestForWatcher()` wrapper copies `EntityType::schemaDigestQuote()` to a by-value return so the overload's `FunctionWatcher` compiles (its `CONSTRUCTION_TYPE()` default cannot value-initialise a reference). Read-only mode comes from the NULL set-function, the same way `stats/stampsPerSecond` is watched. Nothing new is decided, so there is no plan-level case; the mount is compile-checked by the production `cellapp` build.
- **Since batch 58 (2026-11-21) the dry-run mode bit exists**: `PlanInputs::dryRun` (default false) makes `planReload` stop straight after the loads — swap back and destroy the scratch interpreter, then report. The probe deliberately takes none of the failure tail's steps: no `CLEANUP_AFTER_RELOAD` (that breaks the live `old()`/`supersede` window the running entities depend on, F3/F7), no `RECOVER_OLD_SCRIPTS` (nothing was imported into the old interpreter, so there is nothing to recover — the probe reuses the failure path's zero-touch property), and no migrate step. The digest barrier is honoured as it is for a real reload (a rejected digest skips the UDO load). The new truth the probe had to carry is that **a full dry run wipes the registered data types whether the parse succeeds or not** — `EntityType::init(true)` clears them before the parse result is known and nothing on the dry-run path reloads them — so `dataTypesWiped` now reports `isFullReload && (dryRun || !defsUsable)`, and the engine logs the follow-up requirement. `BigWorld.reloadScript(fullReload, expectedDigest, dryRun)` exposes it; the tail needs no new `Action`, and the cellapp plan loop already knows both steps. The digest and dry-run dimensions are swept together through the 32-combination matrix, and `ReloadScriptPlan_DryRunStopsAfterTheLoads` / `ReloadScriptPlan_DryRunHonoursTheDigestBarrier` pin the two probe shapes. What remains for P1 is the cross-component orchestration that consumes `schemaDigest()`, plus the dry-run output detail "which types would become invalid" (a new-vs-current type-set diff).
- **Since batch 59 (2026-11-21) the CR-P2-01 refcount leak is closed without the load-bearing-leak trade**: `PyFrame_GetCode` returns a strong reference that `pushStack`, `updateStackTop` and `PythonProfiler::handleStack` (`pyscript/script.cpp`) never DECREF'd — one leaked code object per stack event — but that leak was deliberately load-bearing: the borrowed `co_name`/`co_filename` pointers handed to `StackTracker` and `Profiler` stayed valid only because the code object outlived the entry, so a bare DECREF would have been a use-after-free. The fix makes every consumer own its string first: `StackItem` now carries fixed `char[128]` `name`/`file` buffers (`stack_tracker.hpp`, `push` memsets the whole item so the raw-byte callstack hash/memcmp in `memory_debug.cpp` stays deterministic, and the temp-string interning block there is dropped — content now lives in the item, strictly more correct than the old pointer-based dedup), the `Profiler` entry/display/hierarchy pools carry `char[96]` buffers (`ENTRY_NAME_SIZE`) with bounded `strncpy` and the four name-pointer identity compares that had relied on the leaked objects become content compares (`findChild`, `writeJsonEntries`, `processEntries`, `updateHierarchy`). The production `cellapp` build links and all 21 unit-test modules pass (the single `network_test` failure on this run was the known `sysctl` buffer-limit drift, re-verified green after the limits were restored).
