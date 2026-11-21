# todo.md — 需求账本

- 账期：patrol-20261102-R4（2026-11-02）
- 口径：已实现 = 代码/工作流已落地且有 commit 或运行证据；只有设计 = 有方案/登记但无承载物；完全没有 = 连设计都没有。
- 期次：P1 = 下批立即；P2 = 近期；P3 = 远期或条件触发。
- 每条需求一行；已完成项保留一条证据行（commit + 测试数），叙事细节见 docs/ 对应台账。

## A. 已实现（附证据）

- [x] Python 2.7 → 3.13.16 内嵌解释器迁移 — `3260bdd9` 起批次链，安全补丁 `2bdfc5aa`；台账 docs/python-313-migration.md
- [x] 第三方依赖 vcpkg manifest 化（openssl/curl/jsoncpp/zlib/libpng/sqlite3/bzip2） — `2bdfc5aa`
- [x] vcpkg manifest 钉 builtin-baseline — `1180ece6`
- [x] 单测门禁全绿（21 模块 / 1505 用例，2026-10-15 实测；批次 52 后 1521 用例） — `dc5cd043` 等；TESTING.md 与 docs/upgrade-plan/migration-status.md
- [x] 覆盖率补强批次 1–34 完成（语义证据 lane：supersede 改名语义、method_description 纯助手族） — 各批 hash 见迁移台账
- [x] 每日构建 workflow（make 全量 + 21 模块门禁 + tar.gz/deb/rpm，48 小时新鲜度） — `57c863c7`、`b365012e`
- [x] push/PR CI workflow（cstdmf CMake 样板构建 + ctest） — `7a4ff703`
- [x] Linux CMake 双轨样板（cstdmf 427 用例与 make 同口径绿） — `8cd23406`、`c6bfaf74`
- [x] libbwpython3.13 源码依赖缺口修复 — `9877dd65`
- [x] 架构研究文档集 28 篇 + VitePress 文档站 — 以 docs/ 现存内容为证
- [x] README 项目状态数字与门禁实测一致 — `9a9e923f`
- [x] 规划入口缺失（docs/implementation-plan.md 与 docs/adr/ 不存在） — `642e5cd5`（docs/adr/ 已建）

## B. 只有设计（有登记/方案，未落地）

- [x] 覆盖率批次 28：resmgr/bwresource.cpp 43%→62%（17 用例）+ entitydef/method_description.cpp 32%→70%（33 用例） — `e4872402`
- [x] 覆盖率批次 29：entitydef/entity_description.cpp 0%→64%（30 用例） — `9d9dc38b`
- [x] 覆盖率批次 30：network/machine_guard.cpp 28%→87%（27 用例） — 台账 §7.30
- [x] 覆盖率批次 31：entitydef/entity_description.cpp MD5 族 64%→70%（11 用例） — 台账 §7.31
- [x] 覆盖率批次 32：entitydef/entity_description.cpp 流写侧访客族 70%→89%（7 用例） — 台账 §7.32
- [x] 覆盖率批次 33（语义证据 lane）：entitydef supersede 族（entitydef_test 366→370，全套 1472→1476） — 台账 §7.33
- [x] 覆盖率批次 34：entitydef/method_description.cpp 纯助手族 70%→72%（5 用例，全套 1481） — 台账 §7.34
- [x] 覆盖率批次 35（观测面 lane）：cellapp 实体消息 EntityID/SpaceID 关联接线（cellapp_test 8→12，全套 1485） — 台账 §7.35
- [x] R2 third_party/python_modules Py2 生态包换 Py3 兼容版并重新启用（删 7 个 Py2 包，vendor 14 个 Py3 源；验收 import 冒烟全绿） — 台账与 scripts/python-modules-smoke-import.py
- [x] 组件 CMake 样板推广（剩余 entitydef；四样板齐：cstdmf 427 / resmgr 88 / network 210 / entitydef 318 用例） — vcpkg-migration §3.4/§3.5
- [x] 网络故障注入扩展（DBApp 卡死/慢查询注入、跨进程网络分区、clock jump、GIL 饥饿/后台线程长阻塞） — 台账与 TESTING.md
- [x] ADR 体系（docs/adr/ 索引 + 0001/0002/0003） — `642e5cd5`
- [ ] R3 Windows 构建（PCbuild/VS 工程迁移，MSVC 未验证）：验收前半已达成（cstdmf+math 双腿过 MSVC 门禁），三树删除与 cellapp_extension 宿主符号导出仍待用户决 — `182e7c55`
- [x] exe 打包（Windows 产物进入每日构建或独立 workflow，干净环境可复现） — `14ebd63a`→`31ce396c`→`68c2c1f1`→`98fd9b19`
- [x] 虚拟时钟注入（EventDispatcher/TimeQueue 可注入时钟且默认行为不变） — lib/network/unit_test/test_virtual_clock.cpp 4 用例
- [x] 观测面现代化（结构化日志字段/trace-correlation id、日志采样与动态级别、watcher 拆只读观测 vs 可写控制） — `33a06559`
- [ ] 热重载生产化（版本化 schema、双版本兼容窗口、dry-run、失败回滚、跨进程一致性屏障）：**失败回滚引擎已接线**（2026-11-12 `966b56c7`）—— `EntityMigration::rollout` collect-decide 全population 收集-决策（`entity_migration_plan.hpp:322`），`CellApp::reloadScript` 的 `MIGRATE_ENTITIES` 改走 rollout（STOP_ON_FAILURE + 逆序回滚 + 按(new,old)对归因），失败时跳过 `MIGRATED_ALL`（`cellapp.cpp:2529-2560`）；`Entity::rollbackMigration` 执行 `pEntityType_`/`__class__` 恢复 + `EntityType::unsupersedeOldDescription()` + `old(NULL)`（`entity.cpp:4368`）；CR-P2-20 修正 touched 计数；`test_migration_rollout.cpp` 8 用例 + `test_entity_migration_plan.cpp` touched 用例（cellapp_test 50 全绿，全套 21 模块绿）；生产 cellapp 二进制编译通过。**版本化 schema 的 digest 可查 + `expectedDigest` 屏障参数已落地**（2026-11-21，batch 56）—— `EntityType::init` 把原本只打印的 `EntityDescriptionMap` MD5 存入 `s_schemaDigestQuote_`（`EntityType::schemaDigestQuote()` 可查，`clearStatics` 清零；捕获点在 migrate 之前，满足 F7「必须在 supersede 之前」）；`BigWorld.schemaDigest()` 供手工核验；`BigWorld.reloadScript(isFullReload, expectedDigest)` 在 defs load 后立即判挡：不匹配即走失败路径、零迁移（同时跳过 UDO load，全量重载下按 F8 计入 dataTypesWiped）；决策核心 `ReloadScript::digestMatches` + `PlanInputs::digestMatched`（默认 true，行为不变）由 `test_reload_script_plan.cpp` 4 新用例 + 结果矩阵扫 digest 维钉住。剩余：双版本兼容窗口 / 跨进程编排（调用方脚本，原语已具备）；watcher 挂载已完成（2026-11-21，batch 57 `c3fceee4`：`CellApp::addWatchers` 顶层只读 watcher `schemaDigest`，经 `schemaDigestForWatcher()` 值返回包装 `EntityType::schemaDigestQuote()`，函数式只读 watcher 与 `stats/stampsPerSecond` 同款）；dry-run 模式位已完成（2026-11-21，batch 58 `260a7fe3`：`PlanInputs::dryRun` 使 `planReload` 在 load 后止步——SWAP_BACK+DESTROY，无 CLEANUP（会断掉存活实体的双版本窗口）/无 RECOVER（旧解释器未被触碰）/无 MIGRATE，同时仍守 digest 屏障；关键真相 `dataTypesWiped = isFullReload && (dryRun || !defsUsable)`——全量 dry-run 无论解析成败都已清空注册数据类型，故只能作探针；`BigWorld.reloadScript(fullReload, expectedDigest, dryRun)` 导出，矩阵扫 digest×dryRun 共 32 组 + 2 形状用例钉住） — `2d65213e`、`66d7cb48`、`e19b07d4`、`966b56c7`、`fde25f2c`
- [x] R4 客户端/FantasyDemo 资源 Linux 验证（程序化生成最小占位树 + 生成脚本可重建） — entitydef_test 375→380
- [ ] io_uring / 批量 UDP 评估（暂缓：用户 2026-10-15 明确暂不开发；保留条目待性能数据触发） — 无
- [ ] 集群级黑盒回归链（login → 实体创建 → Cell 切换 → BaseApp 崩溃恢复 → DB digest 端到端一键冒烟）：缺集群 bootstrap 编排 + DB 实例 + 端到端断言脚本 — 无
- [x] Windows CI runner（无，依赖 R3）：`.github/workflows/ci-windows.yml` 两腿全绿（MSVC 19.51 / VS 18 / x64） — `466ff3e3`→`9b95a921`

## C. 完全没有（无设计无承载物）

- [x] server 侧 reload 迁移语义测试承载（cellapp 级 scaffold：让 `EntityType::migrate` 的 modules 换窗与 `Entity::migrate` 的 `__class__` 换回滚可断言） — `9b85a334`（cellapp_test 31 用例）
- [ ] 集群级黑盒回归链（无任何脚本/框架）：缺集群 bootstrap 编排 + DB 实例 + 端到端断言脚本三件物料，需用户给物料或拍板立项 — 无

## 维护规则

- 每完成一项：勾选并附 commit hash；新事项按 B/C 档入账并给验收口径与期次。
- 不许把设计稿/分析章记成已完成；架构章的「源码验证重点」只是验收建议来源。
- 每次巡检（patrol-*）更新本账并单独 commit。

## D. docs 英文对应缺口（子目录）登记（2026-10-16，2026-10-24 全部闭合）

- [x] docs 双语化：49 个 `.en.md` 镜像命名协议与页内手写切换器废弃，改用 VitePress 官方 `locales`（root 英文 / `docs/zh/` 中文，54 页 ↔ 54 页 1:1 全配对，内部链接 0 死链） — `3ed88fe5`、`5b3775cf`
- [x] 子目录缺口逐项登记：adr 4 / architecture 28 / getting-started 4 / upgrade-plan 7 / references 1 / analysis 5 全配对 — `3ed88fe5`（2026-10-24 闭合）

## 待办（P2，2026-11-01 用户令全舰队推广）

- [ ] UI-AUDIT · P2 · 界面审核（impeccable 流程）：原生桌面界面此前不在 web 界面审核范围，补审——按 impeccable 审计（层级/排版/对比度 a11y/布局/边界态），报告 + 截图入 docs/review/，坏损当场修。
  - 范围修正（2026-11-12 取证）：仓内确有原生桌面界面，共 3 个且全为 **Windows 专属 MFC**：`worldeditor`（`tools/worldeditor/CMakeLists.txt:910` `BW_ADD_TOOL_EXE( … WIN32 )`、`afxwin.h`）、`modeleditor`（`tools/modeleditor/CMakeLists.txt:40`，`GUI/model_editor.cpp` 仅 Win32 壳）、`particle_editor`（`tools/particle_editor/CMakeLists.txt:197`，`afxdhtml.h`/`afxpriv.h`）；三者只在 `BW_CMAKE_TARGET=tools` 生成（`build/cmake/BWConfiguration_tools.cmake:110-112`），本仓 Linux `make el7` 服务器面不构建，server 组件与 server/tools 均为 CLI（`BWConfiguration_server.cmake:174-179`：bwmachined/bots/clear_auto_load/consolidate_dbs/message_logger 等），仓内无 wx/gtk/Qt/tkinter 一方代码（命中项全在 `build/…/common/Lib/test/` 的第三方 Python 标准库测试）。【BLOCKED·需 Windows + VS + MFC 构建/显示环境】验收前提：能在 Windows runner 上起 worldeditor/modeleditor/particle_editor 并截图。
- [ ] DOC-GIF · P2 · 文档 · 配置开关效果动态图（每个开关 off→on 对比 + GIF）：枚举本仓全部「开启后有效果」的开关/配置项；自动流水线生成证据（GUI 用应用截图或录屏，CLI 用终端录屏）；文档「配置效果」页逐开关嵌入 off/on 图 + GIF；验收：清单内每个开关都有 off/on + GIF，流水线可重跑，兼作配置效果回归测试。
  - 进度（2026-11-12，未开工，先记范围）：可枚举面已确认可离线完成——开关定义集中在 `programming/bigworld/game/etc/` 模板（`*.conf.in`）+ `game/etc/.../config.py` + `lib/config/*.hpp` 读取点，可用 grep 出全量清单；但「off/on 证据」的采集要集群或客户端跑起来（server 开关看日志/行为差异，需 `baseappmgr`+`cellappmgr`+`dbapp` 集群；GUI 开关属 :65 同批 Windows-only MFC 工具），**故本项依赖集群 bootstrap（:44/:50）与 Windows runner（:37 R3 三树决）**，当前按批次窗口先登记范围、未产出清单文件，未勾选。

## 漏洞扫描遗留（2026-11-03 全舰队扫描，源 scratch/vuln-scan-*.md）

- [ ] VULN-BIGWORLD-001 [严重·GHSA-cg2g-xh64-v4q7/GHSA-mrv2-3pqr-j5jr] dompurify 3.4.16→≥3.4.17（monaco-editor 引入）（package-lock.json 仓，终扫 6（严重1+高危5））【BLOCKED·规范禁 npm 改 lock】npm-lock 仓只审计未改动；验收：迁 pnpm（pnpm import）后 pnpm audit --audit-level=high 该包不再命中（或编排者批准 npm 侧修复后 npm audit 清零）；该仓无 test 脚本，audit-only
- [ ] VULN-BIGWORLD-002 [高危] @vue/server-renderer <3.5.42→≥3.5.42（package-lock.json 仓，终扫 6（严重1+高危5））【BLOCKED·规范禁 npm 改 lock】npm-lock 仓只审计未改动；验收：迁 pnpm（pnpm import）后 pnpm audit --audit-level=high 该包不再命中（或编排者批准 npm 侧修复后 npm audit 清零）；该仓无 test 脚本，audit-only
- [ ] VULN-BIGWORLD-003 [高危] vue 3.2.13–3.5.41→≥3.5.42（package-lock.json 仓，终扫 6（严重1+高危5））【BLOCKED·规范禁 npm 改 lock】npm-lock 仓只审计未改动；验收：迁 pnpm（pnpm import）后 pnpm audit --audit-level=high 该包不再命中（或编排者批准 npm 侧修复后 npm audit 清零）；该仓无 test 脚本，audit-only
- [ ] VULN-BIGWORLD-004 [高危·GHSA-28wg-ghj8-5hjv/GHSA-2v37-7h3g-55p8] nanoid 3.3.12→≥3.3.18（舰队安全钉口径 3.3.20）（package-lock.json 仓，终扫 6（严重1+高危5））【BLOCKED·规范禁 npm 改 lock】npm-lock 仓只审计未改动；验收：迁 pnpm（pnpm import）后 pnpm audit --audit-level=high 该包不再命中（或编排者批准 npm 侧修复后 npm audit 清零）；该仓无 test 脚本，audit-only
- [ ] VULN-BIGWORLD-005 [高危] source-map-js 1.0.0–1.2.1→≥1.2.2（package-lock.json 仓，终扫 6（严重1+高危5））【BLOCKED·规范禁 npm 改 lock】npm-lock 仓只审计未改动；验收：迁 pnpm（pnpm import）后 pnpm audit --audit-level=high 该包不再命中（或编排者批准 npm 侧修复后 npm audit 清零）；该仓无 test 脚本，audit-only
- [ ] VULN-BIGWORLD-006 [高危] vite ≤6.4.3→≥6.4.4（npm audit fixAvailable=false，需先迁 pnpm/升直接依赖）（package-lock.json 仓，终扫 6（严重1+高危5））【BLOCKED·规范禁 npm 改 lock】npm-lock 仓只审计未改动；验收：迁 pnpm（pnpm import）后 pnpm audit --audit-level=high 该包不再命中（或编排者批准 npm 侧修复后 npm audit 清零）；该仓无 test 脚本，audit-only

## 代码深审遗留（2026-11-01 深审，P1 已全部修完并推送；总账见 docs/review/2026-11-01-code-review.md）

- [x] CR-P2-01 · 引用计数 · `script.cpp:107,133,192` `PyFrame_GetCode` 强引用从不 DECREF（pushStack/updateStackTop/PythonProfiler::handleStack 每事件漏 1）。**承重**：该泄漏保住交给 StackTracker 的裸指针，裸修会 UAF，须同时改为拷贝 name/filename 字符串。 — `a7268bf1`（stack_tracker StackItem 自带 char[128] 缓冲、profiler 条目/显示/层级 char[96] 缓冲、memory_debug 去 interning 块；cellapp 生产构建 + 21 模块全绿，network_test 系 sysctl 漂移已修）
- [x] CR-P2-02 · 引用计数 · `pywatcher.cpp:1202-1233` `PyFunctionWatcher::init` 所有路径泄漏 `pName`/`pType`（PySequence_GetItem 新引用）。 — `034a9471`（`init` 的 tuple 分支用 `PyObjectPtr` STEAL 持有 `pName`/`pType`，所有退出路径释放；`CallableWatcher::addArg` 已拷贝 argName 进 `ArgDescription::BW::string`，释放安全；cellapp 生产构建 + 21 模块全绿）
- [x] CR-P2-03 · 引用计数 · `script.hpp:508-532` `getNestedValue` 每次 GetAttrString 泄漏、终引用未释放；EntityType 构造 ×2 → 每次热重载每类型 8 引用。 — `d7b8dd0f`（`owned` 跟踪当前 hop：每次重指前释放上一跳、终引用在所有退出路径释放；`setData` 只借不拥有，故 setData 后仍须释放；cellapp 生产构建 + 21 模块全绿）
- [x] CR-P2-04 · 引用计数 · `py_traceback.cpp:593-594` `PySys_SetObject` INCREF 不 steal，PyCFunction_New 一次性泄漏。 — `82c56366`（已对 vendored CPython 3.13 源核实：`PySys_SetObject`→`sys_set_object`→`PyDict_SetItem` 只 INCREF 不 steal；`initExceptionHook` 保存 `PyCFunction_New` 结果到局部 `pFunc`、`PySys_SetObject` 后 `Py_DECREF`，NULL 时不触碰 hook；cellapp 生产构建 + 21 模块全绿）
- [x] CR-P2-05 · 热重载 · `entity_type.cpp:381` reloadScript 在 importEntityClass 失败时仍 `setPyType(newRef())`，把活着的 pPyType_ 抹成 NULL → newEntity() 恒 NULL。应失败时保留旧类。 — `50b44c26`（导入失败的分支 `continue` 跳过 `setPyType`：旧类保持原位、类型继续可用，`isOK=false` 照旧走上层失败路径；成功与非 cell 分支行为不变；cellapp 生产构建 + 21 模块全绿）
- [x] CR-P2-06 · 热重载（疑）· `entity_type.ipp:73-100` 持 PyDict_GetItemString 借用的 sys 模块跨 PyDict_Clear，理论可 DECREF 到零；`:98` 注释「steals a reference」措辞错误（SetItemString 是 INCREF，代码正确）。 — `e2902315`（`swapInModules` 在 `PyDict_Clear` 前 `Py_INCREF(pSysModule)` 把借用升级为持有、函数尾部 `Py_DECREF` 平衡；纠正「Steals a reference」注释为「INCREF 不 steal，故须在此释放」；新增 `EntityType_swapInModulesKeepsSysAliveAcrossTheClear`：构造 sys 模块唯一强引用即 dict 持有的场景，验证 swap 后指针仍存活、被恢复；cellapp 生产构建 + 21 模块全绿）
- [x] CR-P2-07 · 热重载（疑）· `entity_type.cpp:420` `s_curTypes_ = s_newTypes_` 无锁改写，getType()/getDescription() 并发读。 — `a16627cd`（`std::shared_mutex` 护卫：`migrate` 全量 swap 独占、`getType`/`getDescription`/`nameToIndex` 共享；迁移缝入 `entity_type.ipp` 与 `swapInModules` 同法，故单测无需链接 cellapp 组件；新增 `EntityType_fullReloadSwapBlocksWhileReadersHold` + `EntityType_nameToIndexBlocksWhileFullReloadSwaps` 双测（共享持有挡 migrate、独占持有挡 nameToIndex）；cellapp 生产构建 + 21 模块全绿（cellapp_test 58 用例，56→58））
- [x] CR-P2-08 · 热重载（疑）· `cellapp.cpp:2529-2560` MIGRATE_ENTITIES 经 `EntityMigration::rollout` 原地遍历 population()（同相邻 MIGRATED_ALL 循环），onMigrate 钩子可建/销实体 → 迭代器失效；回滚半程有 aliveOp 跳过已亡实体（收窄），迁移半程未闭合。 — `c97b23ad`（`rollout` 迁移半程先快照 population 再遍历：onMigrate 钩子销毁当前实体时 `~Entity` 的 `population_.remove` 会抹掉迭代器所在节点，`++it` 成为 UB；快照不被钩子变更，已亡实体仍被 aliveOp 跳过、钩子新建实体留在创建时类；`entity_migration_plan.hpp` 头注释与 rollout 文档注释同步改写为快照语义、相邻 MIGRATED_ALL 循环保留原地遍历暴露；新增 `MigrationRollout_MidPassDestroyCannotBreakThePass`（迁移钩子自毁并把自身抹出活容器，快照保证其后实体照常迁移）；cellapp 生产构建 + 21 模块全绿（cellapp_test 59 用例，58→59））
- [x] CR-P2-09 · 错误处理（疑）· `buffered_ghost_message_factory.cpp:104-108` MF_ASSERT 在 release 为 no-op，findEntity 返 NULL 时解引用。 — `f073f362`（`BufferedGhostSetRealMessage::play` 改判空：findEntity 返 NULL（实体已销毁）时 ERROR_MSG + 丢弃消息并 return，不再解引用；与同文件 `BufferedCreateGhostMessage::play` 的判空手法一致；cellapp 生产构建 + 21 模块全绿（cellapp_test 59 用例））
- [ ] CR-P2-10 · 错误处理 · `script.cpp:1547-1551` `setData(PyObject*, bool&)` 的 PyLong_AsLong 溢出错误被忽略，留 OverflowError 挂起。
- [ ] CR-P2-11 · 错误处理（疑）· `py_logging.cpp:52,68` AsUTF8 结果未判空即传 LogMsg/metaAsJSON；同族 `py_script_object.hpp:1417-1418`、`script.cpp:1555-1556`、`pickler.cpp:149-150`。
- [ ] CR-P2-12 · 静默失败 · `entity.cpp:5311-5331` callBaseMethod 未知方法序号不报错也不 data.finish()（对比 callClientMethod :5367-5372 有日志）。
- [ ] CR-P2-13 · 逻辑（疑）· `data_downloads.cpp:557` StringDataDownloadFactory::onCreate 现要求 PyUnicode_Check，Py2.7 下可过的 bytes 二进制负载现 TypeError 拒绝。
- [ ] CR-P2-14 · 网络越界 · `watcher_nub.cpp:448,464,482,499`（另 646-680）wdm->count 无界循环，astr += strlen(astr)+1 越界读（管理端口，常 localhost-only）。
- [ ] CR-P2-15 · NULL 解引用 · `data_type.cpp:178-180` retrieve() 在 len > BS_BUFF_MAX_SIZE 且超长时返 NULL → new MemoryIStream(NULL,len) 后 readPackedInt 解引用 *(uint8*)NULL。
- [ ] CR-P2-16 · 死循环 · `udp_channel.cpp:530-541` initFromStream 循环在 createFromStream 恒返 NULL 时 numToReceive 不递减 → 永不退出，且 bufferedReceives_ 掩蔽回绕。
- [ ] CR-P2-17 · NULL 解引用 · `fragmented_bundle.cpp:162-173` Packet::createFromStream NULL 未判即 new FragmentedBundle(pPacket.get())。
- [ ] CR-P2-18 · NULL 解引用 · `network_stats.cpp:149-156` strstr(...)+2 解引用发生在 NULL 守卫之前。
- [ ] CR-P2-19 · 复制粘贴 · `data_type.cpp:744` const_iterator 拷贝构造 `size_( other.index_ )` 应为 `other.size_`（当前潜伏）。
- [x] CR-P2-20 · 计划/逻辑 · `entity_migration_plan.hpp` advance() 曾令 rollbackNeeded = migrated > 0，首个实体失败且 migrated==0 时半迁移态不回滚 — 已改 touched 计数（`EntityMigrationPlan_FirstFailureTouchesRollsBack` + `MigrationRollout_FirstEntityHookFailureRollsItselfBack` 双层钉住）。
- [ ] CR-P2-21 · 内存耗尽 · `zip_stream.cpp:135-146` origLen 由对端声明（~2^32）后 new unsigned char[origLen]，无上限。
- [ ] CR-P2-22 · 内存耗尽 · `udp_bundle_processor.cpp:338,551` new char[dataLength_] 取声明报文长，越界发现前已分配。
- [ ] CR-P2-23 · 硬编码凭据 · `db_storage_mysql/db_config.cpp:48-49` 编译期兜底 username/password = "bigworld"。
- [ ] CR-P2-24 · SQL 标识符拼接 · `mysql_database.cpp:568` 实体类型名裸拼进 FROM tbl_ 表名；同族 `table_meta_data.cpp:120` SHOW INDEX FROM tableName。
- [ ] CR-P2-25 · 整数溢出→分配 · `binary_stream.hpp:353,407` operator>>(BW::vector&) 与 TYPEDEF_STREAMING_VECTOR 无条件 reserve(attacker_max)。
- [ ] CR-P3-01 · 格式串 · `log_msg.cpp:558` printf(buffer) 用报文文本当格式串——**仅 PLAYSTATION3 分支**，Linux/Windows 不可达。
- [ ] CR-P3-02 · 覆盖面说明 · `tools/*` C++ 仍含 Py2 专属 PyString_*/PyInt_* 调用，故这些 tool 目标未对 3.13 构建（非缺陷）。

## VS Code 插件（2026-11-21 用户令：类似 kode 但单版本——插件二进制直接复用 BigWorld C++，不要 kode 那种 Rust 新引擎+TS 双版本并存）

- [x] VSX-1 · P1 · 插件骨架目录 `vscode/`（package.json + l10n + syntaxes 起步）：仓根新建 `vscode/` 扩展目录，TS 侧只做编辑器集成（语言配置/补全委托/命令面板），**不做独立分析引擎**；语言基础件（语言配置、片段、基本语法高亮）直接落 VS Code 原生能力，无二进制参与。 — `114abb55`（vscode/ 骨架：package.json + language-configuration + 两语法 + def 片段 + l10n 双语 + 薄壳 extension.ts（仅命令注册、零解析逻辑）+ logo/LICENSE/.vscodeignore）
- [x] VSX-2 · P1 · 复用 C++ 引擎：`lib/entitydef`（EntityDescription/DataType 解析、MD5 schema digest）与 `lib/network` 已有整树与单测（entitydef 318/network 210 用例）；插件核心命令编译为 C++ 二进制（cmake 目标），以 stdio JSON-RPC 子进程形态被 TS 侧调起——解析/校验/digest 全在 C++ 内完成，**单一实现单一版本**；验收：同一 `entities.xml`/`*.def` 输入，CLI 工具与插件输出逐字节一致。 — `59451031`（cmake/samples/entitydef 内新目标 bw_lsp + bw_lsp_test（复用 lib/entitydef）；stdio Content-Length 帧 + JSON-RPC：initialize/bw-schemaDigest/bw-validate/bw-definition/shutdown/exit；CLI schema-digest/validate/definition；验收：fantasydemo schema-digest=18D00DC6... 与 server map 逐字节一致、validate 好/坏 def、definition 类型/别名/属性、serve 全握手；bw_lsp_test 11 用例 + entitydef ctest 全绿；--res 非目录给干净报错而非 CRITICAL 中止）
- [x] VSX-3 · P1 · 门禁与打包：`vscode/` 侧单测（vitest，仅测 TS 薄壳：RPC 编解码、委托、命令注册）+ C++ 侧沿用既有 ctest；打包 vsix 时二进制随包（或首启下载）；CI 加 `vscode/` 腿；验收：`code --install-extension` 装上后 F12/补全/校验命令在 FantasyDemo `res/` def 树上真实可用。 — `709ef69c`（TS 门禁：vitest 测 RPC 编解码/委托/命令注册、esbuild 打包、vsce 打包（仅 artifact，非发版）；CI 腿 ci-vscode.yml：typecheck/typecheck:test/vitest/compile/package + frozen-lockfile，ubuntu-24.04，paths vscode/**；验收：tsc 严格 0 错、vitest 全绿、vsce 打包 14 文件、无新第三方运行时依赖）
- [x] VSX-4 · P2 · 首批命令面：def 文件 schema 校验（红波浪线）、entities.xml ↔ def 交叉引用检查、`BigWorld.schemaDigest()` 查询命令、跳转定义（EntityType/DataType/ClientServerProperty）；验收口径在 VSX-3 起跑后按实际用户走查补齐。 — `bfb1cf59`（bw/crossReference：entity_defs 里未被 entities.xml 声明的 def 报 bw-lsp/orphan-def（按名）；读 def 目录走 BWResource readDirectory、对 parsed description map 比对（getNames/nameToIndex），校验逻辑仍在 lib/entitydef 单一实现；cross-ref CLI 子命令 + bigworld.crossReference 命令（findings 列 BigWorld 输出通道）+ l10n 双语；C++ bw_lsp_test 14 用例 + TS 38 用例、双 gate 全绿；FantasyDemo 走查证据进 vscode/README.md：schema-digest=18D00DC69AC756433D091F537DA7030E、validate 好/坏 def（NOTATYPE 红波浪线带行/列）、definition 类型 Avatar→def + 属性 playerName→自身、cross-ref 干净树/孤立 def、serve 全握手）
