# 脚本热更新与迁移

<div class="arch-hero">

BigWorld 的 `reloadScript` 不是现代意义上的安全生产热更新，而是一个高风险的运行时迁移机制。它通过新 Python 解释器加载脚本和 EntityDef，再切回旧解释器迁移类型、Mailbox 和现有实体对象。

</div>

## 先给结论

源码明确提醒：`CellApp::reloadScript` 应谨慎使用，绝不能用于生产环境。见 [cellapp.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/cellapp.cpp:2322)。

热更新实际流程：

1. 创建新 Python interpreter。
2. 切换到新 interpreter。
3. 加载 EntityType / EntityDef / UDO。
4. 切回旧 interpreter。
5. 销毁新 interpreter。
6. 迁移 EntityType。
7. 迁移 UDO、Mailbox。
8. 遍历运行中 Base 或 Cell 实体，修改 `__class__`。
9. 调用脚本 `onMigrate`。
10. 清理 reload 后的临时状态。

关键源码：

- `Script::createInterpreter()` 创建新解释器，见 [script.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/pyscript/script.cpp:593)。
- `Script::swapInterpreter()` 切换当前解释器，见 [script.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/pyscript/script.cpp:644)。
- `BaseApp` 侧 `reloadScript()` 在 [script_bigworld.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/baseapp/script_bigworld.cpp:1483)。
- `CellApp` 侧 `reloadScript()` 在 [cellapp.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/cellapp.cpp:2322)。
- `Entity::migrate()` 修改 `__class__` 并调用 `onMigrate`，见 [entity.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/entity.cpp:4245)。
- `Base::migrate()` 也有类似迁移逻辑，见 [base.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/baseapp/base.cpp:2770)。

## 为什么要用新解释器

直接在旧解释器里 import 新脚本有几个问题：

- 旧模块仍在 `sys.modules`。
- 当前 Python 调用栈可能还在执行旧代码。
- EntityType 初始化失败时需要回滚。
- 需要先验证新脚本和 EntityDef 是否能加载。

`Script::createInterpreter()` 的注释说明它用于单线程应用创建新 interpreter，并可通过 `swapInterpreter()` 切换，见 [script.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/pyscript/script.cpp:587)。

它创建新 interpreter 后会：

- 设置与当前解释器相同的 `sys.path`。
- 合并 init-time modules。
- 再切回原解释器。

源码见 [script.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/pyscript/script.cpp:595)。

## BaseApp 热更新链路

`BaseApp` 的 `BigWorld.reloadScript()` 在 [script_bigworld.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/baseapp/script_bigworld.cpp:1483)。

流程：

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

成功路径中，BaseApp 遍历 `BaseApp::instance().bases()` 并对每个 Base 调用 `migrate()`，见 [script_bigworld.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/baseapp/script_bigworld.cpp:1509)。

失败路径中，如果不是 full reload，会尝试 `EntityType::reloadScript(true)` 恢复旧脚本，见 [script_bigworld.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/baseapp/script_bigworld.cpp:1525)。

## CellApp 热更新链路

`CellApp::reloadScript()` 比 BaseApp 多了 UDO 迁移：

- 创建并切换新 interpreter。
- `EntityType::init(true)` 或 `EntityType::reloadScript()`。
- `UserDataObjectType::load()`。
- 切回旧 interpreter。
- 销毁新 interpreter。
- `EntityType::migrate()`。
- `UserDataObjectType::migrate()`。
- `ServerEntityMailBox::migrateMailBoxes()`。
- 遍历 `Entity::population()`，每个实体 `migrate()`。
- `EntityType::cleanupAfterReload()`。

源码见 [cellapp.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/cellapp.cpp:2328)。

这说明 CellApp 热更新还要处理空间实体、Ghost、UDO 和 Mailbox 的组合状态，风险比普通脚本 reload 大得多。

## Entity 迁移

`Entity::migrate()` 的核心逻辑：

1. 根据旧类型名获取新 `EntityType`。
2. 检查新旧 type id 是否一致。
3. 更新 `pEntityType_`。
4. `PyObject_SetAttrString(this, "__class__", newPyType)`。
5. 调用脚本 `onMigrate(self)`。

源码见 [entity.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/entity.cpp:4245)。

如果无法设置 `__class__`，迁移失败并恢复旧类型，见 [entity.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/entity.cpp:4273)。

源码中还有一个 TODO：可以遍历 properties 并迁移，但当前只是调用 `onMigrate`，见 [entity.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/entity.cpp:4291)。

这就是热更新的关键风险：属性结构变化不一定自动迁移，脚本必须自己处理兼容。

## Mailbox 迁移

热更新还会调用 `ServerEntityMailBox::migrateMailBoxes()`：

- BaseApp 侧见 [script_bigworld.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/baseapp/script_bigworld.cpp:1507)。
- CellApp 侧见 [cellapp.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/cellapp.cpp:2355)。

这说明 Mailbox 类型也绑定脚本和实体类型。仅替换 Python class 不够，远程引用也要迁移到新类型元数据。

## 为什么不能用于生产

源码已经给出明确警告。本质原因包括：

- 运行中对象在旧 Python 调用栈上，无法完全丢弃旧 interpreter。
- `__class__` 动态替换对 Python 类型布局和 C 扩展对象有严格约束。
- EntityDef 字段变化可能破坏网络流、持久化、默认值和属性访问。
- `onMigrate` 是脚本钩子，迁移质量依赖业务代码。
- 迁移过程中 handler、Timer、网络消息可能继续进入系统。
- 部分失败后只能尽力恢复，不是事务性回滚。
- 多进程 BaseApp/CellApp/DBApp/Client 协议版本必须一致。

所以 BigWorld 的热更新更适合开发调试，而不是生产无停机升级。

## 热更新完整调用链

### CellApp 热更新流程

CellApp 触发热更新的完整调用链：

源码入口：[cellapp.cpp:2322](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/cellapp.cpp:2322)

<div class="flow-strip">
  <span class="flow-node">CellApp::reloadScript()</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">Script::createInterpreter()</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">加载新脚本</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">Script::swapInterpreter()</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">遍历所有 Entity</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">Entity::migrate()</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">迁移 Mailbox</span>
</div>

### BaseApp 热更新流程

BaseApp 触发热更新的完整调用链：

源码入口：[script_bigworld.cpp:1509](/home/cui/workspaces/BigWorld/programming/bigworld/server/baseapp/script_bigworld.cpp:1509)

<div class="flow-strip">
  <span class="flow-node">BigWorld.reloadScript()</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">Script::createInterpreter()</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">加载新脚本</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">Script::swapInterpreter()</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">遍历 bases_</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">Base::migrate()</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">迁移 Mailbox</span>
</div>

### Entity 迁移流程

单个 Entity 迁移的完整调用链：

源码入口：[entity.cpp:4245](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/entity.cpp:4245)

<div class="flow-strip">
  <span class="flow-node">Entity::migrate()</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">获取新 EntityType</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">检查 type id 一致性</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">更新 pEntityType_</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">PyObject_SetAttrString("__class__")</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">调用 onMigrate()</span>
</div>

### 失败恢复流程

热更新失败时的恢复调用链：

源码入口：[script_bigworld.cpp:1525](/home/cui/workspaces/BigWorld/programming/bigworld/server/baseapp/script_bigworld.cpp:1525)

<div class="flow-strip">
  <span class="flow-node">迁移失败</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">检查是否 full reload</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">EntityType::reloadScript(true)</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">恢复旧脚本</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">尝试恢复旧类型</span>
</div>

## 与序列化的关系

热更新最危险的变化不是函数体变化，而是类型契约变化：

- 属性新增、删除、改类型。
- 方法参数变化。
- Base/Cell/Client 数据域变化。
- UDO 类型变化。
- Mailbox 方法表变化。
- 持久化字段变化。

这些变化都会穿透 `EntityDescription`、`DataDescription`、`MethodArgs` 和 Mercury Bundle。热更新章节必须和 [序列化与 EntityDef](/architecture/serialization-entitydef) 一起阅读。

## 源码取舍

这种热更新机制的价值来自运行时脚本和实体对象强耦合：

- 大型 MMO 脚本迭代成本高，开发环境需要快速 reload。
- Python 解释器允许一定程度的动态类型替换。
- EntityDef 和脚本紧耦合，必须提供引擎级迁移入口。
- 多进程架构下，调试单个 App 的脚本 reload 比全服重启快。

源码边界也很明确：

- 版本化 schema。
- 双版本协议兼容窗口。
- 事务回滚。
- 迁移前 dry-run。
- 跨进程一致性屏障。

## 源码验证重点

热更新测试应覆盖脚本 reload 和实体迁移的真实边界：

- `CellApp::reloadScript()` 不能在实体迁移、销毁或回调禁用状态下破坏对象状态。
- `Entity::migrate()` 应保留 EntityID、mailbox、Base/Cell 关系和持久化属性。
- `onMigrate` 失败时应有明确错误路径，不能留下半迁移实体。
- EntityDef 属性、方法参数、数据域或 UDO 类型变化时，应明确拒绝不兼容热更新。
- 多进程 reload 应验证 BaseApp、CellApp、DBApp 和客户端 digest 一致性。
- 热更新后的方法调用、属性同步、DB 写入和 offload 流应继续使用同一套 EntityDef 解释。

## 本章边界

本章解释脚本热更新。下一章分析测试体系和可控时间，因为热更新、可靠 UDP、Timer、重发、Cell 迁移都需要可重复的时间与故障注入测试。
