# Migration and Coverage Status


<div class="arch-hero">

This page is the **single status dashboard** for the Python 3.13 migration
and the coverage-strengthening work: where things stand now, each module's
coverage, and which surfaces are registered as unreachable. The complete
batch-by-batch process, code evidence, and pitfall record live in the
[full Python 3.13 migration ledger](/python-313-migration); this page only
answers "what is the state right now".

</div>

## Current state

<div class="decision-table">

| Item | Status | Notes |
|------|------|------|
| Embedded interpreter | ✅ Python 2.7 → **3.13.16** | After the 3.13.15 migration, on 2026-10-12 the same-series security patch moved it to 3.13.16 (full-chain build + smoke + full unit-test revalidation) |
| Full unit-test gate | ✅ 21 modules / 1505 cases all green | Measured 2026-10-15 (batch 36: the `CellApp::reloadScript` sequence extracted to header-inline `ReloadScript::planReload` and the `EntityMessageHandler::handleMessage` disposition ladder to `classifyEntityMessage`, cellapp_test 12→27; the fantasydemo placeholder asset tree + generator script + load smoke, entitydef_test 375→380; batch 35 cellapp entity-message EntityID/SpaceID correlation wiring, cellapp_test 8→12; batch 34 `entitydef/method_description.cpp` pure-helper family direct driving, entitydef_test 370→375; batch 33 hot-reload rollback-chain supersede semantics, entitydef_test 366→370; batch 32 entity_description stream/accessor/exposed message numbers, entitydef_test 359→366; batch 31 entity_description MD5 family, entitydef_test 348→359; batch 30 `network/machine_guard.cpp` direct driving, network_test 229→256; batch 29 `entitydef/entity_description.cpp` direct driving, entitydef_test 318→348; 1397 after the log correlation-field cross-process carrier leg) |
| Coverage-strengthening work | ✅ batches 1–32 complete (batches 33–35 are semantic-evidence lanes) | Batch 35 observability lane: cellapp entity-message EntityID/SpaceID correlation wiring landed (`EntityMessageLogScope` header-inline RAII carrier + wiring at the outer edge of the 4-argument `EntityMessageHandler::handleMessage` overload, adding entity and space dimensions on top of channelID; spaceID uses an `isDestroyed()` guard to avoid dereferencing pSpace_ after destroy; the call site is not compiled into cellapp_test so it is untestable, wrapper semantics directly driven by the new `test_entity_message_log_scope.cpp` with 4 cases, cellapp_test 8→12). Batch 34: `entitydef/method_description.cpp` pure-helper family 70% → **72%** (precise instrumentation 329/466→337/466; +5 cases in the existing TU driving extractSourceEntityID/convertKeywordArgs/areValidReturnValues/createDictFromTuple; pitfall: `ScriptObject::operator==` is pointer identity, value assertions go through c_str/asLong). Batch 33: hot-reload rollback chain, direct driving of `EntityMethodDescriptions::supersede`/`EntityDescription::supersede` semantics (entitydef_test 366→370; after renaming with an `old_` prefix, find-by-old-name breaks while index/exposed dispatch works, repeated supersede stacks prefixes, and the MD5 drifts after supersede) plus the F7 failure mode recorded (after rollback the old description stays superseded, by-name lookup breaks while msgID dispatch works, and nothing in the tree un-supersedes). Batch 32: `entitydef/entity_description.cpp` 70% → **89%** (precise instrumentation 468/663→591/663; +7 cases in the existing TU driving the stream-write visitor family / read side / accessors / setExposedMsgIDs / checkMethods; pitfall: clientServerProperties_ is stably sorted by stream size, NUM_PASSES=4). Batch 31: `entitydef/entity_description.cpp` 64% → **70%** (precise instrumentation 424/663→468/663; +11 cases driving the MD5 family — the two digest surfaces of addToMD5/addPersistentPropertiesToMD5, the schema-version primitive called for by the L36 hot-reload design; pitfall: `BWResource::openSection` checks DataSectionCensus first, so re-parsing after rewriting a .def must first `purgeAll` to evict). Batch 30: `network/machine_guard.cpp` 28% → **87%** (precise instrumentation 668/764; new `test_machine_guard_messages.cpp` with 27 cases; the TESTING.md table value 553/774 28% is the 09-28 full standard) — all 16+1 MGM message classes with full write/create round trips plus c_str direct driving, ProcessMessage forward-compatible skipping of extra blocks and under-declaration errors, six-argument bitwise matches, the ReplyHandler 16-type dispatch matrix + unknown-type default arm + all 16 base-class defaults landing on onUnhandledMsg; residual 96 lines: UDP socket literals (sendto/three sendAndRecv/sendAndRecvFromEndpointAddr) + the typeStr UNKNOWN default arm + the seq ticker wraparound arm. Batch 29: `entitydef/entity_description.cpp` 0% → **64%** (precise instrumentation 424/663; new `test_entity_description.cpp` with 30 cases; the old TESTING.md restricted judgment "needs the full EntityType/EntityDef XML loader stack" overturned — parse depends only on the BWResource singleton and a fixture .def tree). Batch 28 double close: `resmgr/bwresource.cpp` (precise standard 322/746 43% → 464/746 **62%**, 17 new cases) and `entitydef/method_description.cpp` (precise standard 153/466 32% → 329/466 **70%**, 33 new cases); platform behavior and source-level hazards registered but not fixed; the top-gap list is in TESTING.md |
| Next-batch top candidate | ⏳ residual 129 lines of `entitydef/method_description.cpp` | The pure-helper family is already directly driven (batch 34, 70%→72%); the remainder — PyDeferredResponse/handleException/network send paths — keeps the batch-28 restricted classification (needs a live channel/two-way call context). The residual 72 lines of `entitydef/entity_description.cpp` … |

> The last cell above continues in the ledger; the current page keeps only the
> headline state.

</div>

## Module coverage

<div class="decision-table">

| Module | Coverage | Interpretation |
|------|--------|------|
| script | 92% | The script abstraction layer; the highest-coverage module (main target from batch 1) |
| math | 68% | Geometry and math utilities |
| pyscript | 68% | Embedded-interpreter encapsulation; closed in six consecutive batches 4–9 |
| network | 66% | The Mercury network layer; main attack area of batches 10–18 |
| resmgr | 58% | Resource manager |
| cstdmf | **54%** | Infrastructure (debug, memory, streams, the watcher family; batch 22 class-by-class judgment 51% → 54%) |
| entitydef | 43% | Type contracts; heavy loader-machine dependency |
| terrain | 34% | Terrain (includes a gcovr record anomaly; see ledger batch 18) |
| connection | 24% | Server connection surface; `server_connection.cpp` (990 lines, 0%) stays registered untestable and anchors the floor |
| moo | 13% | Client rendering support; server unit tests never touch it |
| physics2 | **76%** | Client physics; batch 20 closed `quad_tree.ipp` via missed-list activation (61% → 76%) |
| server | 13% | Server skeleton; needs full app assembly |
| chunk | 3% | Client chunk system; needs a ChunkSpace loader machine |

</div>

File-level highlights: `entitydef/entity_description.cpp` 70% → **89%**
(precise-instrumentation standard 468/663 → 591/663 instance lines, batch
32; 7 cases added to the existing TU directly driving four blocks:
AddToStreamMapVisitor in addDictionaryToStream (missing key falls to the
default without error, a wrong-typed value replaced by the default then
fails overall, null mapping rejected up front), AddToStreamAttributeVisitor
in addAttributesToStream (missing property fails immediately, NULL object
rejected up front) plus the addToStream helper (NUM_PASSES=4, per-pass
dataSizes and total stream length reconciled exactly: owner(BASE)→pass0,
nick(ALL_CLIENTS)→pass2, hp(CELL_PUBLIC)→pass3, pass1 base+client
contributes 0 properties), readStreamToSection (stream → new XML section
round trip), readTaggedClientStreamToDict (count u8 + per-item
[clientServerFullIndex u8 + property stream data]),
pIdentifier/isTempProperty/clientServerProperty(Count) (an Identifier
property must also be Persistent:true), setExposedMsgIDs (the single
exposed method takes the range's first slot msgID=10, unexposed methods
read the -1 sentinel as 255), checkMethods (both warnOnMissing arms);
**measured pitfalls**: clientServerProperties_ is stably sorted by stream
size via allocateClientServerFullIndexes (fixed-length ascending first,
variable-length ascending after; the n in clientServerProperty(n) is not
the def order, and so is the tagged-stream index), MemoryOStream uses
addBlob with no append, XMLSection asString/asInt are protected and must
go through DataSectionPtr's readString/readInt, ScriptString values via
c_str; residual 72 lines are parse error/warning arms and the watcher
surface, see ledger §7.32). `entitydef/entity_description.cpp` 64% → **70%**
(precise standard 424/663 → 468/663, batch 31; 11 cases added to the
existing TU `test_entity_description.cpp` directly driving the **MD5
family** — both digest surfaces of addToMD5 and
addPersistentPropertiesToMD5, precisely the schema-version primitive
specified by the L36 hot-reload design: determinism (re-parsing the same
bytes gives equal digests), CELL_PUBLIC properties contribute zero to
both digests (even replacing the type UINT16→FLOAT32 does not move
them), client-server properties enter only the full digest
(clientServerFullIndex + DataDescription::addToMD5), BASE+Persistent
properties enter only the persistent digest, non-persistent properties
contribute zero to the persistent digest (same digest as an empty def),
unexposed base methods contribute zero (same digest as no base methods —
the exposed counter increments only for selected methods, unexposed ones
do not take slots), `<Exposed/>` on a base/cell method immediately
enters the digest (flags_ change + digest inclusion), client methods have
no exposure gate (ClientMethods with `<Exposed/>` is always rejected; a
method enters the digest as soon as it exists), exposed-method order
sensitivity (legacyExposedIndex counts by position; swapping two exposed
methods changes the digest), client-server property order sensitivity
(clientServerFullIndex numbers in def order; swapping two ALL_CLIENTS
properties changes the digest while the persistent digest stays put);
**measured pitfall**: `BWResource::openSection` checks `DataSectionCensus`
before touching the filesystem (bwresource.cpp:614-625), so after
rewriting a .def at the same path, re-parsing **must** first evict with
`BWResource::instance().purgeAll()` or it reads the old section; the
`CHECK` macro expands to `result_`/`m_name` members, so free helper
functions can only use `MF_VERIFY`; `MD5::Digest`'s default constructor
does not initialize bytes and must go through `Digest(MD5&)` or
`operator=`; residual 195 lines registered: parseProperties
coverage/override arms, LoDLevels/Volatile/Implements/Components parsing,
addSectionToStream stream-write side, supersede migration surface,
watcher surface, etc., see ledger §7.31). `network/machine_guard.cpp`
28% → **87%** (precise standard 668/764 instance lines, batch 30; new
`test_machine_guard_messages.cpp` with 27 cases directly driving the MGM
message codec surface; pitfall — CppUnitLite2
CHECK_EQUAL(char*,char*) compares pointers and must be strcmp-ified,
precise instrumentation requires repacking libnetwork.a + explicit
--coverage linking, gcda lands at the compile-time -o path; residual 96
lines registered: 83 lines of sendto/three sendAndRecv/
sendAndRecvFromEndpointAddr UDP socket literals, 12 lines of the
typeStr UNKNOWN default arm, 1 line of the seq ticker wraparound arm,
see ledger §7.30). `entitydef/entity_description.cpp` 0% → **64%**
(precise standard 424/663 instance lines, batch 29; 30 new cases driving
parse minimal form / scalar-property flag bits and Persistent / missing
.def failure, the BaseMethods/CellMethods/ClientMethods three lists and
`<Exposed/>` (ClientMethods with Exposed always rejected), recursive
Parent inheritance and missing-parent failure, Volatile priority square
and yaw≥pitch≥roll validation, LoDLevels tag matching detailLevel, the
three Distribution decider bits and the deprecated ClientName warning,
ClientMethods rejected when no Client script exists, TempProperty…).

> The per-file historical detail above is a condensation; the complete
> per-case narrative and all measured pitfalls are in ledger §7.29–§7.36.

## Batch timeline

<div class="decision-table">

| Batch | Date | Main target |
|------|------|--------|
| 1 | 2026-09-26 | lib/pyscript |
| 2–3 | 2026-09-26 | lib/connection |
| 4–9 | 2026-09-26 | lib/pyscript (residual surfaces / script_math / infrastructure / traceback printer / zero-coverage files / wrap-up) |
| 10–11 | 2026-09-26 | lib/connection (zero-coverage surface / filter-login surface) |
| 12–13 | 2026-09-27 | connection challenge/replay surface + zero coverage in cstdmf/math/resmgr/db; the replay lead disproved |
| 14 | 2026-09-27 | encryption_filter streaming encryption surface + testability judgment of the zero-coverage list |
| 15 | 2026-09-27 | file_stream zero coverage |
| 16 | 2026-09-27 | the whole tcp_bundle wire-format layer |
| 17 | 2026-09-27 | machine_guard MGM codec surface |
| 18 | 2026-09-28 | full-coverage measured baseline + event_poller poller surface |
| 19 | 2026-09-28 | bsp.cpp dead-gate activation (MF_SERVER guard removal + 14 cases) |
| 20 | 2026-09-30 | quad_tree.ipp activation (Makefile.rules missed-list fix + 22 cases + four dead functions registered) |
| 21 | 2026-09-30 | fixed_sized_allocator direct driving (platform-macro dead-test evidence + 12 cases, 0%→99%) |
| 22 | 2026-10-01 | watcher.hpp class-by-class testability judgment (39 cases: stream protocol/container family/dereference/factory/root global surface, 14%→22%) |
| 23 | 2026-10-12 | profiler.cpp close (the 13 failing cases of the red-gate commit fixed by root cause + CHECK(true) replaced with a counting hook for real assertions + orphan tests revived: scene +2 / network +3, 6%→94%) + Python 3.13.15→3.13.16 |
| 24 | 2026-10-13 | smartpointer.hpp direct-driven close (previously no dedicated tests; 29 cases, 84/91→117/118 in test-TU view) |
| 25 | 2026-10-13 | script_math.cpp close (precise-standard re-judgment + 10 cases, 92.3%→97.0%; one half of the ProviderStore matrix evidenced as Windows-only dead area) |
| 26 | 2026-10-13 | datasection.cpp direct-driven close (previously no dedicated tests; 21 cases, 19.3%→99.7%; the operator* fallback arm registered as a crash-on-arrival dead arm) |
| 27 | 2026-10-13 | pywatcher.cpp close (precise-standard re-judgment 312/717 → 670/717 93.4% + 22 cases; all 47 residual lines classified; three source-level hazards registered; the pending-exception false-green form evidenced) |
| 28 | 2026-10-13 | bwresource.cpp + method_description.cpp double close (precise standard 322/746 → 464/746 62% + 153/466 → 329/466 70%; 50 new cases; four platform behaviors pinned, five registered unfixed) |
| 29 | 2026-10-15 | entity_description.cpp direct driving (the restricted judgment overturned — parse needs only BWResource + a fixture .def tree; new test_entity_description.cpp with 30 cases, 0%→64% precise standard 424/663) |
| 30 | 2026-10-15 | machine_guard.cpp direct driving (the MGM codec surface does not depend on bwmachined — the 16+1 message classes carry their own wire format; new test_machine_guard_messages.cpp with 27 cases, 28%→87% precise standard 668/764) |
| 31 | 2026-10-15 | entity_description.cpp MD5 family direct driving (11 cases added to the existing TU, 348→359, 64%→70% precise standard 424/663→468/663; both digests' three partitions, exposure gating and counting, clientServerFullIndex/legacyExposedIndex order sensitivity fully asserted; re-parse after rewriting a .def must purgeAll first — DataSectionCensus precedes the filesystem) |
| 32 | 2026-10-15 | entity_description.cpp stream/accessor/exposed message-number direct driving (7 cases added to the existing TU, 359→366, 70%→89% precise standard 468/663→591/663; NUM_PASSES=4 per-pass dataSizes reconciled, clientServerProperties_ stably sorted by stream size, tagged client stream format, Identifier must be Persistent, the setExposedMsgIDs range first slot, both checkMethods arms) |

</div>

Case-count evolution: after batch 14: 1061 → batch 15: 1071 → batch 16:
1083 → batch 17: 1099 → batch 18: 1113 → batch 19: 1127 → batch 20: 1151 →
batch 21: 1163 → batch 22: 1202 → batch 23: **1220** (full 21-module gate
green; batch 23 net +18 = cstdmf profiler +13 + scene orphan revival +2
+ network orphan revival +3; false-green fixes were in-place assertion
rewrites with no case-count change) → batch 24: **1249** (cstdmf
smartpointer direct driving +29) → batch 25: **1259** (pyscript
script_math gaps +10) → batch 26: **1280** (resmgr datasection direct
driving +21) → batch 27: **1302** (pyscript pywatcher gaps +22) → batch 28:
**1352** (resmgr bwresource path surface +17 + entitydef
method_description gap direct driving +33; full 21-module gate green) →
after the 2026-10-14 fault-injection batch: **1355**
(network/unit_test/test_request_failure.cpp +3: hang-peer request timeout
REASON_TIMER_EXPIRED, channel-lost REASON_CHANNEL_LOST, positive-control
echo; the request-failure path's first assertion-level coverage —
previously handleException only logged) → after the 2026-10-15 log
correlation-field cross-process carrier leg: **1397** → batch 29: **1427**
(`entitydef/entity_description.cpp` direct driving +30, entitydef_test
318→348; full gate green) → batch 30: **1454** (`network/machine_guard.cpp`
direct driving +27, network_test 229→256) → batch 31: **1465**
(entity_description MD5 family +11, entitydef_test 348→359) → batch 32:
**1472** (stream/accessor/exposed message numbers +7, entitydef_test
359→366) → batch 33: **1476** (hot-reload rollback supersede semantics
+4, entitydef_test 366→370) → batch 34: **1481** (method_description
pure-helper family +5, entitydef_test 370→375) → batch 35: **1485**
(cellapp entity-message EntityID/SpaceID correlation wiring +4,
cellapp_test 8→12) → batch 36: **1505** (fantasydemo placeholder asset
tree + generator script + 5 smoke cases, entitydef_test 375→380;
`ReloadScript::planReload` hot-reload action sequence +7 and
`classifyEntityMessage` entity-message routing decision table +8,
cellapp_test 12→27; full 21-module gate green).

## Unreachable / restricted registration summary

The complete arm-by-arm judgments are in the ledger batches; here is the
conclusion summary for large 0%-coverage files:

- `connection/server_connection.cpp` (990 lines) — **stays untestable**: the real login pipeline + TCPChannel state machine (batch 14/16 standard).
- `connection/message_handlers.hpp` (538 lines) — **structural dead code**: a template handler table instantiated by server apps, never instantiated by unit tests.
- `moo/image.ipp` (589 lines) — restricted: client image composition surface.
- `chunk/chunk_space.cpp` (506 lines) — restricted: needs ChunkSpace + a chunk loader machine.
- `connection/replay_controller.cpp` (495 lines) — restricted: the recording/replay file pipeline (wire format tested in batch 12).
- `network/logger_endpoint.cpp` / `logger_message_forwarder.cpp` / `watcher_nub.cpp` — stay registered restricted (real message-logging pipeline, UDP bound to the standard machined port, batch 17).
- `terrain/terrain2/terrain_height_map2.cpp` (755 lines) — gcovr loses the file-name column for this record (empty-name entry); not judged arm by arm.
- The `physics2/quad_tree.ipp` dead-function family — **structural dead code** (batch 20, three-way evidence): `QuadTree::del` (calculateQTRange called without origin_), `QuadTree::testPoint` (calls a nonexistent `QuadTree…` — never instantiated; instantiating it is immediately a compile error. On the production side, chunk's `pChunkTree_` is a HullTree and uses ObstacleTree only via addToRoot.

Closed (removed from the register): the `entitydef/entity_description.cpp`
MD5 family and stream/accessor/exposed-message-number surfaces (64% →
**70%** → **89%** precise standard 424/663→468/663→591/663, closed by
batch 31+32 direct driving — both addToMD5/addPersistentPropertiesToMD5
digest surfaces fully asserted: three partitions per digest, exposure
gating and counting, clientServerFullIndex/legacyExposedIndex order
sensitivity; the residual 195 lines are parseProperties coverage/override
arms, LoDLevels/Volatile/Implements/Components parsing, the stream-write
side, and the supersede migration surface, see ledger §7.31;
`moo/image.ipp`'s 589 lines stay restricted — under the el7 mixed server
build lib/moo compiles only node.o/png.o and image.ipp has no
instantiation); `network/machine_guard.cpp` (28% → **87%** precise
standard 668/764, closed by batch 30 direct driving; new
`test_machine_guard_messages.cpp` with 27 cases asserting the full MGM
16+1 message-class wire round trips; residual 96 lines — 83 lines of UDP
socket literals (sendto/three sendAndRecv/sendAndRecvFromEndpointAddr,
needs a live socket + a bound PORT_MACHINED=20018 loopback, an
environment dependency so registered untested) + 12 lines of the
typeStr UNKNOWN default arm + 1 line of the seq ticker wraparound arm,
see ledger §7.30); `pyscript/pywatcher.cpp` (43.5% → **93.4%** precise
standard, 312/717 → 670/717 instance lines, closed by batch 27 direct
driving; new `test_pywatcher_gaps.cpp` with 22 cases; the residual 47
lines are all classified (the putPyObjectOnStream failure arm is
structural dead code because its sole caller finishSetFromStream ignores
the return value, Py3-migration dead arms, unreachable sequence branches,
arms needing a live hook/post-fini window); three source-level hazards
registered unfixed and the pending-exception poisoned false-green form
evidenced, see ledger §7.27); `resmgr/datasection.cpp` (19.3% → **99.7%**
precise standard, 169/876 → 873/876 instance lines, closed by batch 26
direct driving; new `test_datasection.cpp` with 21 cases; the only
residual 3 lines are the iterator operator* empty-child fallback arm —
a structural dead arm that crashes on arrival via the base-class
childSectionName `openChild(index)->sectionName()` NULL dereference in
the same scenario, gdb SIGSEGV evidenced, see ledger §7.26);
`pyscript/script_math.cpp` (92.3% → **97.0%** precise standard,
1000/1083 → 1050/1083 instance lines, directly driven after batch 25
re-judgment; new `test_script_math_gaps.cpp` with 10 cases; residual 33
lines all classified — 13 lines of Windows-only structural dead area
(one half of the ProviderStore matrix — MatrixAnimation, the only
autoTick MatrixProvider subclass, is wholly `#if defined( _WIN32 )`,
zero symbols in the archive), 3 lines of the safeSetVector dead arm, 8
lines of dead pyGet_x/y/z/w members, 9 lines of phantom macro-expansion
lines, see ledger §7.25); `cstdmf/smartpointer.hpp` (84/91 → **117/118**
in test-TU view, closed by batch 24 direct driving; the sole residual
`:121` is the structural dead empty base-destructor body, see ledger
§7.24); `cstdmf/watcher.hpp` (14% → **22%**, batch 22 class-by-class
testability judgment; 39 cases incorporated including 3 added after
parallel-session review — SafeWatcher cases' non-NULL base under direct
DataWatcher reference-member semantics makes `&value + &value` a
non-canonical address that must SIGSEGV, fixed then incorporated; the
rootWatcher/fini dead arm, the `_XBOX360` branch, and the uint64
extractor's missing-else defect registered, see ledger §7.22);
`cstdmf/fixed_sized_allocator.cpp` (289 lines, 0% → **99%**, batch 21
platform-macro dead-test direct driving; root cause: both existing
test_allocate.cpp case groups are guarded by
`ENABLE_FIXED_SIZED_POOL_ALLOCATOR`, always 0 in the el7 server build —
the third instance of "has tests yet 0%"; residual 3 lines are the
findPool sorting-shift structural dead arm; the
getNumPoolItemsForSize miss reading `allocSizes_[-1]` out-of-bounds
hazard registered, see ledger §7.21); `physics2/quad_tree.ipp` (0% →
**98%** in test-TU view, batch 20 missed-list activation; residual 16
lines are assertion/log failure arms + the dead-function family above);
`physics2/bsp.cpp` (778 lines, 0% → **91%**, batch 19 dead-gate
activation; residual 67 lines are construction-validation/load-error
arms, registered segment by segment in ledger §7.19). The full
top-gap list is in the TODO section of TESTING.md.

## Standards and boundaries

- Coverage is a **gcovr 8.6 instrumented full measured run** value
  (`user_shouldBuildCodeCoverage=1`, all 21 module tests run to
  completion, 498 .gcda aggregated), not an estimate. Prerequisite for
  the three embedded-Python modules: after tree reconstruction run an
  explicit `make python_install` (the `bw-run-all-unit-tests` chain does
  not trigger installation; ledger §7.19). The batch-21 file-level
  `fixed_sized_allocator.cpp` number uses the **precise instrumentation
  standard** (rebuild only the cstdmf TUs with -fprofile-arcs; for an
  ordinary .cpp single TU the denominator matches the full run and is
  precisely comparable; template-header denominators scale with the
  instrumented TU set, so the module table keeps the batch-18/20
  full-instrumentation values; ledger §7.21).
- Some batches' gate wording in the ledger is an equivalent standard:
  this repository has no ctest and no standalone coverage-gate script;
  the equivalent standard = `bw-run-all-unit-tests` 21 modules all green
  + successful gcovr aggregation.
- Coverage numbers keep moving with later batches; this page updates in
  sync with the ledger; on conflict the ledger wins.

Further reading: [Python upgrade roadmap](/upgrade-plan/python-upgrade-plan)
(the original planning decision record) ·
[Python C API migration](/upgrade-plan/python-c-api-guide) ·
[third-party dependency upgrades](/upgrade-plan/third-party-deps) ·
[full ledger](/python-313-migration)
