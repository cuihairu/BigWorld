# 热重载生产化设计


<div class="arch-hero">

`BigWorld.reloadScript` 当前定位为开发期工具（源码注释原话 "never in a production environment"）。本文档基于对 reload 链路的逐行审计，给出将其生产化所需的五件套设计：版本化 schema、双版本兼容窗口、dry-run、失败回滚、跨进程一致性屏障，以及分期落地路径与测试承载。

</div>

## 现状链路审计

以下事实均以 2026-10-15 的 dev 分支源码为证。

### 调用链

| 步骤 | 代码位置 | 行为 |
|------|----------|------|
| 入口 | `server/cellapp/cellapp.cpp:2974` | `BigWorld.reloadScript(fullReload=True)`，仅在本 cellapp 生效；跨组件由调用方自行保证 |
| 1. 新解释器 | `cellapp.cpp:2329-2330` | `Script::createInterpreter` + `swapInterpreter`——重载在新解释器里进行，旧解释器仍在调用栈上存活 |
| 2. 加载 | `cellapp.cpp:2332-2340` | fullReload → `EntityType::init(true)` 重新解析 entity defs 进 `s_newTypes_` 并打印 MD5 digest；否则 `EntityType::reloadScript()` 只重导入类；随后 `UserDataObjectType::load` |
| 3. 换回 | `cellapp.cpp:2350-2351` | 换回旧解释器并销毁新解释器（新模块表已先拷贝进 `s_pNewModules_`） |
| 4. 迁移 | `cellapp.cpp:2353-2362` | `EntityType::migrate`（模块换窗）→ `UserDataObjectType::migrate` → `ServerEntityMailBox::migrateMailBoxes` → 逐实体 `Entity::migrate()`（`MF_VERIFY` 包裹） |
| 5. 收尾 | `cellapp.cpp:2364` | `EntityType::cleanupAfterReload(isFullReload)` 与迁移同一 tick 执行 |
| 失败路径 | `cellapp.cpp:2368-2385` | 换回旧解释器；非 full reload 时 `EntityType::reloadScript(true)` 重试旧脚本（依赖 sys.modules 未被动过）；cleanup；销毁新解释器；返回 false |

### 关键语义体

- **模块换窗**：`EntityType::migrate` 的 `isFullReload=false` 分支已提取为 `EntityType::swapInModules`（`entity_type.ipp`），2026-10-15 起在 cellapp 单测可断言。
- **实体换类**：`Entity::migrate`（`server/cellapp/entity.cpp:4245`）——type-id 守卫（类型缺失或 id 不匹配 → 判 obsolete 返回 false）→ 换 `pEntityType_` 并经 `EntityType::old()` 挂旧类型 → `PyObject_SetAttrString(__class__)`（失败 → 恢复 `pEntityType_` 返回 false）→ 调脚本侧 `onMigrate`。
- **双版本钩子**：`EntityType::old()` + `MethodDescription::supersede(CELL)` 使旧方法签名在窗口内继续可解析。

## 失败模式清单

| # | 模式 | 证据 | 后果 |
|---|------|------|------|
| F1 | release 下实体迁移失败被静默吞掉 | `MF_VERIFY` 在 `_RELEASE` 展开为 `(exp)`（`cstdmf/debug.hpp:428`），`cellapp.cpp:2361` 的返回值无人消费 | 该实体留在旧类，而 `sys.modules`/`s_curTypes_` 已是新版本——半迁移态；dev 构建则直接 abort 在 reload 中途 |
| F2 | `onMigrate` 脚本异常不算失败 | `entity.cpp:4297` `Script::call(..., okIfFnNull=true)` 仅打印异常，`migrate()` 仍返回 true | 脚本级半迁移，且无失败计数 |
| F3 | 双版本窗口长度为零 | `cleanupAfterReload(isFullReload)` 在迁移同一 tick 调用（`cellapp.cpp:2364`），立刻 `old(NULL)` 断链 | `supersede` 机制实际只在迁移循环内有效，边界上的在途旧签名调用没有缓冲期 |
| F4 | 无跨进程一致性 | `cellapp.cpp:2944-2946` 文档明示"用户负责在所有 server 组件上调用"；digest 已计算（`entity_type.cpp:280-287`）但只打印、不比对、不传播 | cellapp 已迁移而 baseapp/dbmgr 仍旧 defs 时，跨进程 type-id 漂移 |
| F5 | 自定义数据类型实例不迁移 | `cellapp.cpp:2951-2960` 文档要求用户手工逐实例重指 `__class__` | 依赖调用方脚本正确性，无引擎侧保障 |
| F6 | 批量迁移完成钩子是死代码 | `Entity::migratedAll`（`entity.hpp:284` 声明、`entity.cpp:4308` 定义）全仓无调用方 | `onMigratedAll` 永不触发 |
| F7 | 回滚后旧描述仍处于 superseded 态 | `entity.cpp:4267` `pNewType->old(pOldType)` 先于 `__class__` 尝试（`entity.cpp:4275`）执行；`entity_type.cpp:168-185` 首次调用即无条件对旧描述 `supersede(CELL)`（全部 cell 方法改名 `old_` 前缀、清名重建 map）；`entity.cpp:4287` `__class__` 失败回滚 `pEntityType_ = pOldType`，而全仓无 un-supersede（批次 33 实测：`find("cellTick")`→NULL、`find("old_cellTick")` 可达、`internalMethod`/`exposedMethod` 下标派发不受影响、digest 随改名漂移） | 回滚"成功"的实体按名字的 mailbox 方法查找全断（`mailbox.cpp:122/240` `findMethod` 返回 NULL→脚本侧报错），按 msgID 的暴露派发仍活——半残实体；第 4 节回滚设计必须连 un-supersede（或以窗口期重解析补偿），且 schema digest 必须在 supersede 前采集 |

## 设计

### 1. 版本化 schema

以既有 `entityDescriptionMap` MD5 digest 作为 schema 版本号（不新造版本体系）：

- digest 从"只打印"改为"可查询"：挂 cellapp watcher（只读），供屏障与人工核验。
- `BigWorld.reloadScript` 增加可选 `expectedDigest` 参数：传入时与本次加载结果比对，不匹配直接判失败（走既有失败路径，零迁移）。

### 2. 双版本兼容窗口

- `cleanupAfterReload` 拆两段：迁移 tick 只清 `s_pNewModules_`；`pOldSelf_` 断链改为延迟——由 tick 钩子在配置的窗口（秒或 tick 数，默认 0 保持现行为）后执行。
- 窗口内 `EntityType::old()` 链有效；注意 F7 实测边界：supersede 之后按名字的查找已断（只剩 `old_` 前缀名），窗口只保得住**下标/msgID 派发**——按名补解析需另设计。窗口到期统一断链并告警残留计数。

### 3. dry-run

- reload 增加模式位：只执行步骤 1–2（新解释器内 init/reloadScript + UDO load + 全类型 import），然后直接销毁新解释器、不做任何 migrate。
- 本质是复用既有失败路径的"零接触"性质：旧解释器从未被换出，风险为零；产出口径为"将加载成功的类型集 + digest + 预计失效类型清单"。
- 这是 F1/F4 前置的廉价探测：先 dry-run 全组件，digest 一致且无失效类型才真跑。

### 4. 失败回滚

- 逐实体 `Entity::migrate` 循环改为**收集-决策**：失败（F1 的两类失败 + F2 的脚本异常计入）先记录并继续或即停，由 reload 请求的策略位决定；返回值不再经 `MF_VERIFY`。
- 已迁移实体的回滚 = `Entity::migrate` 的逆操作（`pEntityType_` 与 `__class__` 对调回旧类型，旧类型经 `old()` 链仍可达）。注意 F7：逆操作必须同时回退 `supersede` 的改名（否则按名查找仍断）；版本化 schema 的 digest 采集点也必须定在 supersede 之前。
- 决策默认"即停+回滚"，保 L34 验收"失败无半迁移状态"。

### 5. 跨进程一致性屏障

分两期：

- **P1（无新 RPC）**：dry-run + `expectedDigest` 参数把屏障放在调用方——跨组件编排脚本先全组件 dry-run 收 digest，一致才逐组件真 reload。引擎只提供原语。
- **P2（引擎侧）**：cellappmgr 广播 reload 请求、收集各组件 ack（digest+就绪位）后统一下发；需要 cellappmgr/管理面改动，独立立项。

## 分期落地路径

| 期 | 内容 | 测试承载 | 状态 |
|----|------|----------|------|
| P0 | 模块换窗语义可测（`swapInModules` 提取） | cellapp/unit_test（已落地，2 用例） | 2026-10-15 完成 |
| P1 | dry-run 模式 + digest 可查询 + `expectedDigest` | prepare 阶段要跑 `EntityType::init`（BWResource + entity defs + 脚本导入），现 cellapp/unit_test harness 不可达——与 P2 同源承载缺口，需 C 档先行或集群物料 | 待做（承载缺口） |
| P2 | 失败收集-决策 + 实体级回滚 | 决策核 + 结果类缝已落地 2026-10-27（`entity_migration_plan.hpp`；`Entity::migrate(FailureClass*)`；cellapp_test 7 用例） | 进行中：引擎接线 + 回滚执行待做——回滚逆只有一半可表达（见下） |
| P3 | 双版本兼容窗口 | 需 cellapp tick 级承载 | 待做 |
| P4 | 引擎侧屏障（P2 期） | 需集群黑盒链（C 档） | 待做 |

## 验收映射（对应 todo L34）

- 「迁移路径测试覆盖 `Entity::migrate` 语义保持」→ P2，承载缺口：`Entity::migrate` 需要 Entity 实例 + 脚本类型，现 cellapp/unit_test 只编入低耦合 TU（C 档"server 侧 reload 迁移语义测试承载"同源）。
- 「失败无半迁移状态」→ 加载期失败今天已天然无半迁移（失败路径不触发 migrate）；迁移期半迁移由 P2 的收集-决策+回滚关闭。**批次 36（2026-10-15）起该性质有测试承载**：`ReloadScript::planReload`（`reload_script_plan.hpp`）把 `CellApp::reloadScript` 的动作序列做成数据，失败尾断言不含任何 `MIGRATE_*`，成功尾断言 scratch 解释器先销毁再 migrate、cleanup 最后（7 用例，cellapp_test）。同一提取如实上报 **F8**——全量重载失败时 `EntityType::init(true)`（`entity_type.cpp:262`）已调 `DataType::clearStaticsForReload()` 清空 `s_singletonMap_` 而失败路径不重建，`planReload` 以 `dataTypesWiped` 字段标出；迁移期半迁移与 F8 同属 P2 承载缺口。
- **批次 51（2026-10-27）起 P2 决策核有了测试承载**：`EntityMigration::classify`（`entity_migration_plan.hpp`）命名 `Entity::migrate` 的三种失败——GUARD_OBSOLETE 什么都不动、CLASS_SWAP_REJECTED 恢复 `pEntityType_`、SCRIPT_EXCEPTION 仍报成功——也正是新增 `Entity::migrate(FailureClass*)` 重载上报的内容；`EntityMigration::advance` 承载收集-决策策略（STOP_ON_FAILURE / CONTINUE_ON_FAILURE，停止是粘性的，只有先前迁移过才需要回滚）。同一提取如实记录为何 migrate 循环仍在 `MF_VERIFY` 而非停下回滚：四个逆步骤中两个指针/类恢复是普通赋值，但 REVERT_SUPERSEDE_RENAME 没有逆——`supersede()` 给旧描述的 cell 方法加 `old_` 前缀且每次调用再叠一层，`EntityType::old` 在 `pOldSelf_` 已置时直接返回不改名，故同一类型的第二个实体上观察到的失败并未造成它需要撤销的那个改名——而 CLEAR_OLD_TYPE_LINK 根本没有 setter。`rollbackInverseComplete()` 为 false，这个边界是被断言的而非假设的。
