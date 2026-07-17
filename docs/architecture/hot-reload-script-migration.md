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

## 与序列化的关系

热更新最危险的变化不是函数体变化，而是类型契约变化：

- 属性新增、删除、改类型。
- 方法参数变化。
- Base/Cell/Client 数据域变化。
- UDO 类型变化。
- Mailbox 方法表变化。
- 持久化字段变化。

这些变化都会穿透 `EntityDescription`、`DataDescription`、`MethodArgs` 和 Mercury Bundle。热更新章节必须和 [序列化与 EntityDef](/architecture/serialization-entitydef) 一起阅读。

## 当时取舍

这种热更新机制在当时有价值：

- 大型 MMO 脚本迭代成本高，开发环境需要快速 reload。
- Python 解释器允许一定程度的动态类型替换。
- EntityDef 和脚本紧耦合，必须提供引擎级迁移入口。
- 多进程架构下，调试单个 App 的脚本 reload 比全服重启快。

但它没有提供现代生产热更通常需要的：

- 版本化 schema。
- 双版本协议兼容窗口。
- 灰度发布。
- 事务回滚。
- 迁移前 dry-run。
- 跨进程一致性屏障。

## 现代方案对比

<div class="decision-table">

| 方案 | 优点 | 代价 | 对 BigWorld 的判断 |
| --- | --- | --- | --- |
| 当前 reloadScript | 开发迭代快，深度集成 Entity | 生产风险极高 | 保留为开发工具 |
| 进程滚动重启 | 边界清晰，可回滚 | 需要状态迁移和会话转移 | 生产优先方向 |
| 双版本协议 | 支持灰度和兼容窗口 | schema 管理成本高 | 必须先补 EntityDef 版本治理 |
| Lua/脚本热替换 | 更轻量 | 仍有状态迁移问题 | 不是换语言即可解决 |
| WASM 沙箱 | 隔离强，版本边界清晰 | 集成和性能成本 | 可研究新逻辑模块 |
| Actor snapshot 迁移 | 状态边界清晰 | 架构重构大 | 长期方向，不是短期改造 |

</div>

## 现代化建议

1. 明确 `reloadScript` 只用于开发和测试环境。
2. 对 EntityDef 变化做兼容性检查，禁止破坏性热更新。
3. 给 `onMigrate` 建立测试框架和迁移报告。
4. 对 BaseApp/CellApp reload 增加全链路 dry-run。
5. 生产升级优先走滚动重启 + 实体迁移 + 版本兼容，而不是直接脚本热换。
6. Python 3.12 迁移时重点审计 `Py_NewInterpreter`、`PyThreadState`、`__class__` 替换和 C API 兼容。

## 本章边界

本章解释脚本热更新。下一章分析测试体系和可控时间，因为热更新、可靠 UDP、Timer、重发、Cell 迁移都需要可重复的时间与故障注入测试。
