# Documentation–Code Consistency Audit

> [中文](/zh/)

Audit date: 2026-10-13. Audit scope: README.md, all pages under docs/**, the VitePress site config, and the public GitHub facade (description/homepageUrl/topics).

**Three buckets**: A = docs ahead of code (the doc claims something the source does not have — dangerous); B = code ahead of docs (the source has it, the doc does not mention it); C = description mismatch (name/signature/default/path/behavior inconsistent).

**Six-step procedure**: enumerate the doc surface → extract claims → gather file:line evidence claim by claim (before asserting "not implemented", first confirm the search paths exist) → classify → fix docs to match the code (no code changes) → close the loop in a table.

## Overview

| Bucket | Count |
|---|---|
| A docs ahead of code | 9 |
| B code ahead of docs | 5 |
| C description mismatch | 62 |
| Total | 76 |

Severity: P0 = 3 (invented APIs, directly causing integration failure); P1 = 32 (wrong path/signature/state, misleading implementers); P2 = 41 (line drift, wording, counts).

## P0 list

| ID | Page | Claim | Source evidence | Bucket | Fix |
|---|---|---|---|---|---|
| API-1 | docs/getting-started/api-reference.md (whole page) | the page's API signatures do not match the source or are invented | see API-2..8 item by item | A | whole page rewritten item by item against the source (2026-10-13) |
| API-3 | same page | `NetworkInterface::addHandler(MessageID, MessageHandler*)`, `Bundle::writeInt32/writeString/writeBlob/finish` exist | a grep over the whole lib/ tree finds none of these symbols; registration binds an InputMessageHandler in the InterfaceElement constructor (network/interface_element.hpp:90-92); writes go through BinaryOStream's streaming interface | A | rewritten (2026-10-13) |
| API-4 | same page | `EntityDescription::numProperties/findProperty/getDescription("Avatar")/createEntity()` exist | entitydef/entity_description.hpp has none of them; the real ones are clientServerPropertyCount/clientServerProperty/findProperty (inherited from base_user_data_object_description.hpp:55) | A | rewritten (2026-10-13) |

## P1 list

| ID | Page | Claim | Source evidence (file:line) | Bucket | Fix |
|---|---|---|---|---|---|
| API-2 | api-reference.md | `NetworkInterface(dispatcher, addr)` | network_interface.hpp:65-67: the second parameter is NetworkInterfaceType, not an address | C | fixed |
| API-5 | api-reference.md | `DataType::defaultValue()` returns PyObject* | data_type.hpp:87: actually `getDefaultValue(DataSink&)`, pure virtual | C | fixed |
| API-6 | api-reference.md | `ScriptObject::getAttr/setAttr` | py_script_object.hpp:286/:398: actually getAttribute/setAttribute templates (error handler required) | C | fixed |
| API-7 | api-reference.md | `IDatabase::getEntity/putEntity/delEntity` return Entity* synchronously | idatabase.hpp:109/:221/:231: all are void + completion callbacks | C | fixed |
| API-8 | api-reference.md | `Entity : public ScriptObject`, `Proxy : public Entity`, `getPlayerData()` | client/entity.hpp:97 (:BWEntity), server/baseapp/proxy.hpp:74 (:Base), no getPlayerData | C | fixed |
| FAC-1 | GitHub About | description was `"BigWorld "` (trailing space, no role statement) | measured with `gh repo view` | C | replaced with a role statement (mentions C++23/Python 3.13.16/vcpkg) |
| RD-1 | README.md:22 | "1220 test cases all green" | 2026-10-13 gate measurement: 1302 (21 modules) | C | changed to 1302 |
| RD-2 | README.md:22 | "reinforcement batches 1–23 done" | the ledger has reached batch 27 | C | changed to 1–27 |
| RD-3 | README.md:25/:27 | the same migration paragraph repeated twice | visible to the naked eye | C | deduplicated |
| RD-4 | README.md:20 (implied) / todo.md | "architecture research doc set: 27 chapters" | docs/architecture/ actually has 28 chapters | C | todo.md changed to 28 |
| BF-1 | build-from-source.md:68 | a `make everything` rule exists | no `everything` rule in build/make/*.mak (only a comment at Makefile:5) | C | annotated "no such rule" |
| UP-1 | upgrade-plan/python-c-api-guide.md | the migration needs a `bw_python_compat.hpp` compatibility layer | no such file anywhere under programming; the migration is complete, no compat layer, direct replacement | A | status banner added |
| UP-2 | upgrade-plan/build-system-modernization.md | "GCC 4.8.x current / recommend 11+", "target C++17/20" | platform_el7.mak:6 is already c++23; ci.yml:47-51 gcc-14 | B | baseline table updated, linked to cpp23-migration |
| UP-3 | vcpkg-migration.md:110/:20 | Windows config references `third_party/sqlite` | no third_party/sqlite under build/cmake/; sqlite goes through lib/sqlite (BWConfiguration_server.cmake:71) | C | sqlite removed |
| UP-4 | upgrade-plan/third-party-deps.md | "remaining rows are still the pre-migration inventory" + OpenSSL 1.0.x EOL pending upgrade | vcpkg.json:5-17; measured OpenSSL 3.6.4/curl 8.22.0/sqlite3 3.53.4 | C | status and version table updated |
| UP-5 | build-system-modernization.md:17/:148 | Docker path `build/docker/Dockerfile` | no build/ at repo root; actually programming/bigworld/build/docker/Dockerfile | C | path completed |
| AR-1 | architecture/network-io-model.md:54-75 | `EPoller::registerFileDescriptor` + EPOLL_CTL_ADD, all fake code | event_poller.cpp:1059 is actually EPoller::doRegister (with MOD/ADD/DEL) | C | code block rewritten against doRegister |
| AR-2 | architecture/memory-lifecycle.md:155 | SmartPointer at smartpointer.hpp:117 | actually :610 | C | changed |
| AR-8 | architecture/serialization-entitydef.md:59-102 | `addBlob` returns int with a fabricated inline implementation; `transfer/writeStringLength` signatures | binary_stream.hpp:40-48: addBlob is a void pure declaration; writeStringLength internally is writePackedInt(int) | C | rewritten against the source |
| AR-9 | architecture/serialization-entitydef.md:183 | MemoryOStream has two base classes, default size=0, members buffer_/size_/ | memory_stream.hpp:27-29 three base classes (+SafeAllocatable), :31 default 64, :62-67 pBegin_/pCurr_/pEnd_/pRead_ | C | rewritten |
| AR-10 | architecture/testing-time-control.md:74-92 | `addTimerCommon` signature and body all fake | event_dispatcher.cpp:264-281: returns TimerHandle, TimeStamp::fromSeconds | C | rewritten against the source |
| AR-11 | architecture/serialization-entitydef.md:398 | `EntityDescription::createFromStream()` | no such function anywhere in the repo; the real ones are readStreamToDict (entity_description.cpp:1567, the stream-reading branch :1584) + DataDescription::createFromStream (data_description.hpp:101) | C | changed |
| AN-1 | analysis/modules.md:21/:54 | `py_entitydef.cpp` exists; path `server/dbappmgr/dbappmgr_interface.hpp` | py_entitydef does not exist anywhere; the interface is in lib/db/dbappmgr_interface.hpp | A | changed to py_component.cpp + the lib/db path |
| AN-2 | analysis/source-code-guide.md:227 | `PrimitiveMapping`/`FixedDictMapping` exist | none under lib/db_storage_mysql/mappings/; actually `NumMapping<T>`/StringMapping/ClassMapping/BlobMapping/StringLikeMapping/UserTypeMapping | A | changed to the real class names |
| AN-3 | analysis/source-code-guide.md:132 | reading chain starts at `scripts/entities.xml` | programming/bigworld/scripts/ does not exist; actually examples/client_integration/python/simple/res/scripts/entities.xml | C | changed |
| AN-4 | analysis/source-code-guide.md:160 | `MethodDescription::createFromStream()` | method_description.hpp:114/:118 is actually createArgsFromStream | C | changed |
| AN-6 | analysis/overview.md:71 | 24 third_party top-level directories | measured 22 directories (23 entries counting Makefile.rules) | C | changed to 22 |
| PS-1 | getting-started/project-structure.md:16 | `.spec-workflow/` at the top level | no such directory at the repo root | A | removed |
| PS-2 | project-structure.md:161 | docs/ contains `migration/` | no docs/migration; the migration docs live at the docs root and in upgrade-plan/ | A | changed to upgrade-plan/ + 3 root-level docs |
| PS-3 | references/original-docs.md:5-7 | 3 PDF links readable online | docs/pdf/ exists but the build output has no pdf/ (VitePress only copies public/) | A | copied to docs/public/pdf/ |
| PS-9 | TESTING.md:36 | the exclusion registration comment is in the `cstdmf Makefile.rules` | actually lib/cstdmf/unit_test/Makefile.rules:55-59 | C | path changed |
| PS-10 | TESTING.md:36 | `test_blend_transform` is in `scene/unit_test` | actually lib/math/unit_test/test_blend_transform.cpp | C | changed |

## P2 list (line drift / wording / counts)

| ID | Page | Claim | Source evidence (file:line) | Bucket | Fix |
|---|---|---|---|---|---|
| AR-3 | hot-reload-script-migration.md:29 | Script::swapInterpreter at script.cpp:644 | actually :798 | C | changed |
| AR-4 | hot-reload-script-migration.md:28 | Script::createInterpreter at script.cpp:593 | actually :747 | C | changed |
| AR-5 | memory-lifecycle.md:26 | ConstSmartPointer at smartpointer.hpp:307 | actually :343 | C | changed |
| AR-6 | memory-lifecycle.md:24 | ReferenceCount at :31, SafeReferenceCount at :157 | actually :50, :176 | C | changed |
| AR-7 | communication-rpc.md:234/:300 | PyEntityMailBox at mailbox_base.hpp:35 | actually :55 | C | changed |
| AR-12 | threading-background-tasks.md:29 | Script::initThread at script.cpp:661 | actually :815 | C | changed |
| AR-13 | threading-background-tasks.md:161 | Script::init at script.cpp:334 | actually :411 | C | changed |
| AR-14 | threading-background-tasks.md:161 | releaseLock/acquireLock at script.cpp:748 | actually :935/:918 | C | changed |
| AR-15 | observability-watcher-profiler-logs.md:79 | WatcherNub::attachTo at watcher_nub.cpp:212 | actually :256 | C | changed |
| AR-16 | observability:327 | DebugFilter::handleMessage at debug_filter.cpp:138 | actually :189 | C | changed |
| AR-17 | observability:328 | addMessageCallback at logger_message_forwarder.cpp:165 | actually :254 | C | changed |
| AR-18 | observability:329 | FindLoggerHandler at :96 | actually :163 | C | changed |
| AR-19 | observability:363 | LoggerMessageHeader defined at .cpp:67 | actually logger_message_forwarder.hpp:114 | C | changed |
| AR-20 | observability:364 | LoggerComponentMessage at .cpp:84 | actually logger_message_forwarder.hpp:131 | C | changed |
| AR-21 | observability:336 | filter items at logger.cpp:42 | actually :164-178 | C | changed |
| AR-22 | observability:337 | self-reporting watcher at logger.cpp:141 | actually :164 | C | changed |
| AR-23 | observability:344 | LogStorage::addLogMessage at log_storage.cpp:43 | actually :71 | C | changed |
| AR-24 | observability:365 | LogStorage::resolveUID at :107 | actually :159 | C | changed |
| AR-25 | observability:350 | LogStorageMongoDB::addLogMessage at mongodb/log_storage.cpp:220 | actually :268 | C | changed |
| AR-26 | serialization-entitydef.md:104-136 | error() virtual, readPackedInt not inline, operator>> a member | binary_stream.hpp:78/:90/:309 etc.: error() not virtual, readPackedInt inline, operator>> a free function | C | changed |
| UP-6 | build-system-modernization.md:131 | "two workflows" | actually 3 (including deploy-docs.yml) | C | changed |
| UP-7 | build-system-modernization.md:136 | "every step ships a failure-log artifact" | only one aggregate artifact (ci.yml:90-100) | C | changed |
| UP-8 | third-party-deps.md:49 | P0 "Python 2.7 → 3.12" | completed at 3.13.16 (patchlevel.h) | C | changed |
| UP-9 | third-party-deps.md:94 | the mysqlclient recommendation pairs a mysql.connector example | the two packages have different APIs | C | comment added to pick one |
| UP-10 | third-party-deps.md:131 | compatibility matrix column header Python 3.12 | actually 3.13 | C | changed |
| UP-11 | packaging-and-daily-build.md:86 | test_py_traceback hardcodes /tmp/opencode | now /tmp/pyscript_py_traceback_fifo.py | C | current path added |
| UP-12 | vcpkg-migration.md:46 | .gitignore:37 excludes vcpkg | actually :41 | C | changed |
| UP-13 | vcpkg-migration.md:42 | dependency list misses bzip2 | vcpkg.json:16 has bzip2 | C | added |
| UP-14 | vcpkg-migration.md:124 | compile-option enumeration misses -Wextra | cmake/samples/cstdmf/CMakeLists.txt:60 has -Wextra | C | added |
| UP-15 | cpp23-migration.md:13 | platform_el7.mak:4 = c++11 labeled "currently in effect" | actually :6 c++23 (checked off in §6.3) | C | marked as a pre-switch snapshot |
| UP-16 | cpp23-migration.md:30 | ~/.local/bin/g++ broken symlink | that path does not exist | C | marked as a machine-specific condition |
| UP-17 | python-upgrade-plan.md | phase 1 planned a compat wrapper layer | none was built; point-by-point replacement instead | C | banner records it |
| PS-4 | project-structure.md:17 | top-level logo.png | actually logo.svg | C | changed |
| PS-5 | project-structure.md:119 | third_party has 24 | actually 22 | C | changed |
| PS-6 | project-structure.md:128 | jsoncpp is vendored | provided by vcpkg (third_party_json.mak:6) | C | changed |
| BF-2 | build-from-source.md:122 | the Docker comment attributed to the parallel-build warning | actually build/make/Makefile:13; Dockerfile:48 is a different sentence | C | split into two sources |
| TD-1 | todo.md | batches 24–27 acceptance narrative hung under the "design only" section | completed (per-batch hashes available) | B | moved into the A section with one-line evidence |
| TD-2 | todo.md | the "README still says 1113 test cases" entry itself is stale | the entry was written in an earlier batch | C | updated |
| PS-7 | project-structure.md | the docs/ tree omits upgrade-plan/ and pdf/ | the 6 upgrade-plan docs live in upgrade-plan/ | B | added to the tree |
| PS-8 | project-structure.md | the top-level tree omits game/, scripts/, TESTING.md, todo.md | all exist | B | added to the tree |
| PS-11 | .vitepress/config.mjs | cpp23-migration.md has no site entry | unreferenced anywhere in docs | B | sidebar entry added |

## Fix-principle execution record

- All fixes were doc-side, code was not touched (only docs/, TESTING.md, README.md, docs/.vitepress/config.mjs, the docs/public/pdf/ copy, and the GitHub About description).
- No case of "changing code to fit the docs".
- This repo already has a VitePress doc site; none was created.
- After the fixes, `npm run docs:build` passes (the output includes the 3 PDFs and the cpp23-migration entry).

## Follow-up suggestions (not fixed in this round)

- The remaining 17 architecture/* chapters have not been spot-checked item by item (this round covered 10 chapters and 122 citations); the root cause of the drift concentrates in the line-number shifts caused by the Python 3.13 port (ee940e96/871a6589/41fafefc); subsequent batches should roll a re-review through chapter by chapter.
- api-reference.md has been rewritten to a "verified against the source" standard; when new API chapters are added, keep the file:line annotation habit.
