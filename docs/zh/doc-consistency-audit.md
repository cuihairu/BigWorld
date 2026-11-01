# 文档-代码一致性审计

](/> [English)) | **中文**

审计日期：2026-10-13。审计范围：README.md、docs/** 全部页面、VitePress 站点配置、GitHub 公开门面（description/homepageUrl/topics）。

**三类分桶**：A = 文档超前于码（文档主张源码不存在的东西，危险）；B = 码超前于文档（源码有、文档未提）；C = 描述不符（名称/签名/默认值/路径/行为不一致）。

**六步口径**：枚举文档面 → 提取主张 → 逐条 file:line 取证（断言「没有实现」前先确认搜索路径存在）→ 分类 → 以码为准修复（不改代码）→ 落表闭环。

## 总览

| 桶 | 数量 |
|---|---|
| A 文档超前于码 | 9 |
| B 码超前于文档 | 5 |
| C 描述不符 | 62 |
| 合计 | 76 |

严重度：P0 = 3（虚构 API，会直接导致集成失败）；P1 = 32（路径/签名/状态错误，误导实现）；P2 = 41（行号漂移、措辞、计数）。

## P0 清单

| ID | 页 | 主张 | 源码证据 | 类别 | 修法 |
|---|---|---|---|---|---|
| API-1 | docs/getting-started/api-reference.md（全页） | 整页 API 签名与源码不符或虚构 | 见 API-2..8 逐条 | A | 已按源码逐条重写全页（2026-10-13） |
| API-3 | 同上 | `NetworkInterface::addHandler(MessageID, MessageHandler*)`、`Bundle::writeInt32/writeString/writeBlob/finish` 存在 | lib/ 全树 grep 无这些符号；注册机制为 InterfaceElement 构造绑定 InputMessageHandler（network/interface_element.hpp:90-92）；写入走 BinaryOStream 流式接口 | A | 已重写（2026-10-13） |
| API-4 | 同上 | `EntityDescription::numProperties/findProperty/getDescription("Avatar")/createEntity()` 存在 | entitydef/entity_description.hpp 全无；真实为 clientServerPropertyCount/clientServerProperty/findProperty（继承自 base_user_data_object_description.hpp:55） | A | 已重写（2026-10-13） |

## P1 清单

| ID | 页 | 主张 | 源码证据(file:line) | 类别 | 修法 |
|---|---|---|---|---|---|
| API-2 | api-reference.md | `NetworkInterface(dispatcher, addr)` | network_interface.hpp:65-67：第二参数为 NetworkInterfaceType，非地址 | C | 已修 |
| API-5 | api-reference.md | `DataType::defaultValue()` 返回 PyObject* | data_type.hpp:87：实为 `getDefaultValue(DataSink&)` 纯虚 | C | 已修 |
| API-6 | api-reference.md | `ScriptObject::getAttr/setAttr` | py_script_object.hpp:286/:398：实为 getAttribute/setAttribute 模板（需错误处理器） | C | 已修 |
| API-7 | api-reference.md | `IDatabase::getEntity/putEntity/delEntity` 同步返回 Entity* | idatabase.hpp:109/:221/:231：均为 void + 完成回调 | C | 已修 |
| API-8 | api-reference.md | `Entity : public ScriptObject`、`Proxy : public Entity`、`getPlayerData()` | client/entity.hpp:97（:BWEntity）、server/baseapp/proxy.hpp:74（:Base）、无 getPlayerData | C | 已修 |
| FAC-1 | GitHub About | description 为 `"BigWorld "`（尾随空格、无角色描述） | `gh repo view` 实测 | C | 已修为角色陈述（含 C++23/Python 3.13.16/vcpkg） |
| RD-1 | README.md:22 | 「1220 个用例全绿」 | 2026-10-13 门禁实测 1302（21 模块） | C | 已改为 1302 |
| RD-2 | README.md:22 | 「补强批次 1–23 完成」 | 台账已到批次 27 | C | 已改为 1–27 |
| RD-3 | README.md:25/:27 | 同一段迁移介绍重复两遍 | 肉眼可见 | C | 已去重 |
| RD-4 | README.md:20（隐含）/ todo.md | 「架构研究文档集 27 篇」 | docs/architecture/ 实为 28 章 | C | todo.md 已改 28 |
| BF-1 | build-from-source.md:68 | `make everything` 有规则 | build/make/*.mak 无 everything 规则（仅 Makefile:5 注释） | C | 已标注「无对应规则」 |
| UP-1 | upgrade-plan/python-c-api-guide.md | 迁移需创建 `bw_python_compat.hpp` 兼容层 | find programming 无此文件；迁移已完成、无兼容层、直接替换 | A | 已加状态横幅 |
| UP-2 | upgrade-plan/build-system-modernization.md | 「GCC 4.8.x 当前 / 推荐 11+」「目标 C++17/20」 | platform_el7.mak:6 已 c++23；ci.yml:47-51 gcc-14 | B | 已更新基线表并链接 cpp23-migration |
| UP-3 | vcpkg-migration.md:110/:20 | Windows 配置引用 `third_party/sqlite` | build/cmake/ 无 third_party/sqlite；sqlite 走 lib/sqlite（BWConfiguration_server.cmake:71） | C | 已删 sqlite |
| UP-4 | upgrade-plan/third-party-deps.md | 「其余行仍为迁移启动前盘点」+ OpenSSL 1.0.x EOL 待升 | vcpkg.json:5-17；实测 OpenSSL 3.6.4/curl 8.22.0/sqlite3 3.53.4 | C | 已更新状态与版本表 |
| UP-5 | build-system-modernization.md:17/:148 | Docker 路径 `build/docker/Dockerfile` | 仓库根无 build/；实为 programming/bigworld/build/docker/Dockerfile | C | 已补全路径 |
| AR-1 | architecture/network-io-model.md:54-75 | `EPoller::registerFileDescriptor` + EPOLL_CTL_ADD 全假代码 | event_poller.cpp:1059 实为 EPoller::doRegister（含 MOD/ADD/DEL） | C | 已按 doRegister 重写代码块 |
| AR-2 | architecture/memory-lifecycle.md:155 | SmartPointer 在 smartpointer.hpp:117 | 实为 :610 | C | 已改 |
| AR-8 | architecture/serialization-entitydef.md:59-102 | `addBlob` 返回 int、带伪造内联实现；`transfer/writeStringLength` 签名 | binary_stream.hpp:40-48：addBlob 为 void 纯声明、writeStringLength 内部即 writePackedInt(int) | C | 已按源码重写 |
| AR-9 | architecture/serialization-entitydef.md:183 | MemoryOStream 两个基类、默认 size=0、成员 buffer_/size_/ | memory_stream.hpp:27-29 三基类（+SafeAllocatable）、:31 默认 64、:62-67 pBegin_/pCurr_/pEnd_/pRead_ | C | 已重写 |
| AR-10 | architecture/testing-time-control.md:74-92 | `addTimerCommon` 签名与函数体全假 | event_dispatcher.cpp:264-281：TimerHandle 返回、TimeStamp::fromSeconds | C | 已按源码重写 |
| AR-11 | architecture/serialization-entitydef.md:398 | `EntityDescription::createFromStream()` | 全仓无此函数；真实为 readStreamToDict（entity_description.cpp:1567，读流分支 :1584）+ DataDescription::createFromStream（data_description.hpp:101） | C | 已改 |
| AN-1 | analysis/modules.md:21/:54 | `py_entitydef.cpp` 存在；`server/dbappmgr/dbappmgr_interface.hpp` 路径 | py_entitydef 全树无；接口在 lib/db/dbappmgr_interface.hpp | A | 已改 py_component.cpp + lib/db 路径 |
| AN-2 | analysis/source-code-guide.md:227 | `PrimitiveMapping`/`FixedDictMapping` 存在 | lib/db_storage_mysql/mappings/ 无；真实为 `NumMapping<T>`/StringMapping/ClassMapping/BlobMapping/StringLikeMapping/UserTypeMapping | A | 已改为真实类名 |
| AN-3 | analysis/source-code-guide.md:132 | 阅读链起点 `scripts/entities.xml` | programming/bigworld/scripts/ 不存在；实为 examples/client_integration/python/simple/res/scripts/entities.xml | C | 已改 |
| AN-4 | analysis/source-code-guide.md:160 | `MethodDescription::createFromStream()` | method_description.hpp:114/:118 实为 createArgsFromStream | C | 已改 |
| AN-6 | analysis/overview.md:71 | third_party 一级目录 24 个 | 实测 22 个目录（23 条目含 Makefile.rules） | C | 已改 22 |
| PS-1 | getting-started/project-structure.md:16 | 顶层有 `.spec-workflow/` | 仓库根无此目录 | A | 已删 |
| PS-2 | project-structure.md:161 | `docs/` 下有 `migration/` | 无 docs/migration；迁移文档在 docs 根与 upgrade-plan/ | A | 已改为 upgrade-plan/ + 根级 3 篇 |
| PS-3 | references/original-docs.md:5-7 | 3 个 PDF 链接可在线读取 | docs/pdf/ 存在但构建产物无 pdf/（VitePress 只拷贝 public/） | A | 已拷贝到 docs/public/pdf/ |
| PS-9 | TESTING.md:36 | 排除登记注释在 `cstdmf Makefile.rules` | 实为 lib/cstdmf/unit_test/Makefile.rules:55-59 | C | 已改路径 |
| PS-10 | TESTING.md:36 | `test_blend_transform` 在 `scene/unit_test` | 实为 lib/math/unit_test/test_blend_transform.cpp | C | 已改 |

## P2 清单（行号漂移/措辞/计数）

| ID | 页 | 主张 | 源码证据(file:line) | 类别 | 修法 |
|---|---|---|---|---|---|
| AR-3 | hot-reload-script-migration.md:29 | Script::swapInterpreter 在 script.cpp:644 | 实为 :798 | C | 已改 |
| AR-4 | hot-reload-script-migration.md:28 | Script::createInterpreter 在 script.cpp:593 | 实为 :747 | C | 已改 |
| AR-5 | memory-lifecycle.md:26 | ConstSmartPointer 在 smartpointer.hpp:307 | 实为 :343 | C | 已改 |
| AR-6 | memory-lifecycle.md:24 | ReferenceCount 在 :31、SafeReferenceCount 在 :157 | 实为 :50、:176 | C | 已改 |
| AR-7 | communication-rpc.md:234/:300 | PyEntityMailBox 在 mailbox_base.hpp:35 | 实为 :55 | C | 已改 |
| AR-12 | threading-background-tasks.md:29 | Script::initThread 在 script.cpp:661 | 实为 :815 | C | 已改 |
| AR-13 | threading-background-tasks.md:161 | Script::init 在 script.cpp:334 | 实为 :411 | C | 已改 |
| AR-14 | threading-background-tasks.md:161 | releaseLock/acquireLock 在 script.cpp:748 | 实为 :935/:918 | C | 已改 |
| AR-15 | observability-watcher-profiler-logs.md:79 | WatcherNub::attachTo 在 watcher_nub.cpp:212 | 实为 :256 | C | 已改 |
| AR-16 | observability:327 | DebugFilter::handleMessage 在 debug_filter.cpp:138 | 实为 :189 | C | 已改 |
| AR-17 | observability:328 | addMessageCallback 在 logger_message_forwarder.cpp:165 | 实为 :254 | C | 已改 |
| AR-18 | observability:329 | FindLoggerHandler 在 :96 | 实为 :163 | C | 已改 |
| AR-19 | observability:363 | LoggerMessageHeader 定义在 .cpp:67 | 实为 logger_message_forwarder.hpp:114 | C | 已改 |
| AR-20 | observability:364 | LoggerComponentMessage 在 .cpp:84 | 实为 logger_message_forwarder.hpp:131 | C | 已改 |
| AR-21 | observability:336 | 过滤项在 logger.cpp:42 | 实为 :164-178 | C | 已改 |
| AR-22 | observability:337 | 自曝 watcher 在 logger.cpp:141 | 实为 :164 | C | 已改 |
| AR-23 | observability:344 | LogStorage::addLogMessage 在 log_storage.cpp:43 | 实为 :71 | C | 已改 |
| AR-24 | observability:365 | LogStorage::resolveUID 在 :107 | 实为 :159 | C | 已改 |
| AR-25 | observability:350 | LogStorageMongoDB::addLogMessage 在 mongodb/log_storage.cpp:220 | 实为 :268 | C | 已改 |
| AR-26 | serialization-entitydef.md:104-136 | error() 虚函数、readPackedInt 非 inline、operator>> 为成员 | binary_stream.hpp:78/:90/:309 等：error() 非虚、readPackedInt 为 inline、operator>> 为自由函数 | C | 已改 |
| UP-6 | build-system-modernization.md:131 | 「两条 workflow」 | 实为 3 条（含 deploy-docs.yml） | C | 已改 |
| UP-7 | build-system-modernization.md:136 | 「每个步骤带失败日志 artifact」 | 仅一个聚合 artifact（ci.yml:90-100） | C | 已改 |
| UP-8 | third-party-deps.md:49 | P0「Python 2.7 → 3.12」 | 已完成 3.13.16（patchlevel.h） | C | 已改 |
| UP-9 | third-party-deps.md:94 | mysqlclient 建议配 mysql.connector 示例 | 两包 API 不同 | C | 已加注释二选一 |
| UP-10 | third-party-deps.md:131 | 兼容性矩阵列头 Python 3.12 | 实为 3.13 | C | 已改 |
| UP-11 | packaging-and-daily-build.md:86 | test_py_traceback 硬编码 /tmp/opencode | 现为 /tmp/pyscript_py_traceback_fifo.py | C | 已补当前路径 |
| UP-12 | vcpkg-migration.md:46 | .gitignore:37 排除 vcpkg | 实为 :41 | C | 已改 |
| UP-13 | vcpkg-migration.md:42 | 依赖清单漏 bzip2 | vcpkg.json:16 有 bzip2 | C | 已补 |
| UP-14 | vcpkg-migration.md:124 | 编译选项枚举漏 -Wextra | cmake/samples/cstdmf/CMakeLists.txt:60 有 -Wextra | C | 已补 |
| UP-15 | cpp23-migration.md:13 | platform_el7.mak:4 = c++11 标为「当前生效」 | 实为 :6 c++23（§6.3 已勾选） | C | 已标切档前快照 |
| UP-16 | cpp23-migration.md:30 | ~/.local/bin/g++ 损坏符号链接 | 该路径不存在 | C | 已标注机器特定现象 |
| UP-17 | python-upgrade-plan.md | 阶段 1 计划建兼容包装层 | 实际未建、逐点替换 | C | 横幅已补记 |
| PS-4 | project-structure.md:17 | 顶层 logo.png | 实为 logo.svg | C | 已改 |
| PS-5 | project-structure.md:119 | third_party 24 个 | 实为 22 | C | 已改 |
| PS-6 | project-structure.md:128 | jsoncpp 为 vendored | vcpkg 提供（third_party_json.mak:6） | C | 已改 |
| BF-2 | build-from-source.md:122 | Docker 注释归属并行构建警告 | 实为 build/make/Makefile:13；Dockerfile:48 是另一句 | C | 已拆双来源 |
| TD-1 | todo.md | 批次 24–27 验收叙述挂「只有设计」段 | 已完成（各批 hash 可查） | B | 已移入 A 段一行证据 |
| TD-2 | todo.md | 「README 仍写 1113 用例」条目本身过期 | 该条目写于更早批次 | C | 已更新 |
| PS-7 | project-structure.md | docs/ 树遗漏 upgrade-plan/ 与 pdf/ | 升级计划 6 篇在 upgrade-plan/ | B | 已补入树 |
| PS-8 | project-structure.md | 顶层树遗漏 game/、scripts/、TESTING.md、todo.md | 均存在 | B | 已补入树 |
| PS-11 | .vitepress/config.mjs | cpp23-migration.md 无站点入口 | 全 docs 无引用 | B | 已加 sidebar 条目 |

## 修复原则执行记录

- 全部修复以码为准，未改任何源码（仅 docs/、TESTING.md、README.md、docs/.vitepress/config.mjs、docs/public/pdf/ 拷贝、GitHub About description）。
- 无「为迁就文档改代码」项。
- 本仓已有 VitePress 文档站，未新建。
- 修复后 `npm run docs:build` 构建通过（产物含 3 份 PDF 与 cpp23-migration 入口）。

## 后续建议（非本轮修复）

- architecture/* 剩余 17 章未逐条抽查（本轮覆盖 10 章 122 条引证）；腐坏根因集中于 Python 3.13 移植（ee940e96/871a6589/41fafefc）导致的行号漂移，建议后续批次按章滚动复审。
- api-reference.md 已重写为「按源码核对」口径；后续新增 API 章节时保持 file:line 标注习惯。
