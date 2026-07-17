# 持久化与 DB 线程模型

<div class="arch-hero">

BigWorld 的持久化不是业务代码直接写 SQL。BaseApp 负责协调 Base/Cell 数据、备份和归档；DBApp 通过 IDatabase 抽象屏蔽 MySQL/XML 等后端；MySQL 后端把阻塞数据库操作下沉到后台任务。

</div>

## 先给结论

持久化链路可以概括为：

<div class="flow-strip">
  <span class="flow-node">Base tick</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">archive/backup</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">Base.writeToDB</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">Cell writeToDBRequest</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">DBApp writeEntity</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">IDatabase</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">DB background task</span>
</div>

关键源码：

- BaseApp 每个 tick 调用 `backup()` 和 `archive()`，见 [baseapp.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/baseapp/baseapp.cpp:1561)。
- `Base::archive()` 调用 `onPreArchive`，然后 `writeToDB(WRITE_BASE_CELL_DATA)`，见 [base.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/baseapp/base.cpp:1916)。
- `Base::writeToDB()` 如果存在 Cell entity 且没有 Cell 数据，会先 `requestCellDBData()`，见 [base.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/baseapp/base.cpp:1947)。
- `requestCellDBData()` 向 CellApp 发 `CellAppInterface::writeToDBRequest`，见 [base.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/baseapp/base.cpp:1968)。
- `Archiver::tick()` 按 archive period 分摊实体归档，并根据 DBApp channel 窗口决定是否跳过，见 [archiver.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/baseapp/archiver.cpp:68)。
- `DBApp` 实现 `IDatabase` pass-through，见 [dbapp.hpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/dbapp/dbapp.hpp:229)。
- MySQL 后端 `putEntity()` 等操作会加入后台任务，见 [mysql_database.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/db_storage_mysql/mysql_database.cpp:440)。

## BaseApp 的持久化职责

BaseApp 不是只把消息转发到 DBApp。它还负责：

- 决定什么时候 archive。
- 决定是否 backup。
- 调用脚本 `onPreArchive` / `onWriteToDB`。
- 需要 Cell 数据时向 Cell entity 请求。
- 处理 secondary DB。
- 控制 DBApp channel 背压。
- BaseApp retiring 时处理备份和 offload。

这说明持久化和运行时状态治理强耦合。

## 自动归档

`Base::autoArchive()` 根据 `shouldAutoArchive_` 决定是否执行归档，`NEXT_ONLY` 成功后关闭自动归档，见 [base.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/baseapp/base.cpp:1896)。

`Base::archive()` 会：

- 如果 Cell entity 正在 destroy pending，通常跳过。
- 调用 `onPreArchive`，脚本可返回 false 取消。
- 如果 Base 已销毁，跳过。
- 调用 `writeToDB(WRITE_BASE_CELL_DATA)`。

源码见 [base.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/baseapp/base.cpp:1916)。

## Cell 数据请求

如果 Base 有 Cell entity，但调用 `writeToDB()` 时没有 Cell data，就会先发请求：

```cpp
bundle.startRequest(CellAppInterface::writeToDBRequest, handler);
bundle << id_;
sendToCell();
```

源码见 [base.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/baseapp/base.cpp:1970)。

这体现了 Base/Cell 分层持久化：

- Base 持有数据库协调权。
- Cell 持有空间和 cell-only 属性权威。
- 写 DB 时必须把两边数据拼起来。

## writeToDB 防御逻辑

`Base::writeToDB()` 会处理多种边界：

- flags 为 0 时直接失败。
- delete from DB 时自动加 log off，并清掉 base/cell data 写入。
- 未写入 DB 的实体不需要 log off。
- 类型不能在 Cell 上时清掉 `WRITE_CELL_DATA`。
- 如果需要 Cell data 但 `pCellData_` 不存在，报错并失败。
- 写 base data 前调用 `onWriteToDB(cellData)`。
- 如果不是销毁中，会先 `backupBaseNow()`。

源码见 [base.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/baseapp/base.cpp:1984) 和 [base.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/baseapp/base.cpp:2050)。

这说明写 DB 不只是序列化，还要考虑备份、一致性和生命周期状态。

## Secondary DB

BaseApp 可以写 secondary SQLite DB：

- 条件包括存在 `pSecondaryDB`、已有完整 DBID、不是 autoload/delete/primary/explicit dbid 等。
- 写入时先 `addToStream()`，再带 `GameTime` 写 secondary DB。
- explicit write 时 commit secondary DB。

源码见 [base.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/baseapp/base.cpp:2074)。

`Archiver::tickSecondaryDB()` 会 tick secondary DB，并按 `maxCommitPeriodInTicks` commit，见 [archiver.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/baseapp/archiver.cpp:39)。

这属于 BigWorld 的容灾/恢复设计，不是普通缓存。

## Archiver 分摊策略

`Archiver::tick()` 根据 `archivePeriodInTicks()` 把 `basesToArchive_` 分摊到多个 tick 中：

- archive disabled 时不做。
- 如果没有 secondary DB 且 DBApp channel send window 使用超过阈值，跳过归档。
- 周期结束后重新洗牌实体列表。
- 每 tick 只归档当前范围内已写过 DB 的 Base。

源码见 [archiver.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/baseapp/archiver.cpp:68)。

这体现了游戏服务器持久化的关键取舍：不能为了落库阻塞 Tick，也不能把 DBApp 网络窗口打满。

## DBApp 与 IDatabase

`DBApp` 注册 `DBAppInterface`，见 [dbapp.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/dbapp/dbapp.cpp:231)。

`DBApp` 暴露：

- `loadEntity`
- `writeEntity`
- `deleteEntity`
- `lookupEntity`
- `lookupEntities`
- `writeSpaces`
- `writeGameTime`
- `secondaryDBRegistration`
- `updateSecondaryDBs`

见 [dbapp.hpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/dbapp/dbapp.hpp:111)。

同时它提供 `IDatabase` pass-through，注释明确说调用这些方法而不是直接调用 `IDatabase`，这样 DBApp 可以拦截和处理，见 [dbapp.hpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/dbapp/dbapp.hpp:229)。

## MySQL 后台任务

MySQL 后端把阻塞数据库操作变成后台任务：

- `getEntity`
- `putEntity`
- `delEntity`
- `executeRawCommand`
- `getIDs`
- `setGameTime`
- `writeSpaceData`
- secondary DB operations

搜索证据显示 `MySqlDatabase::putEntity()` 创建 `PutEntityTask` 并加入 `BufferedEntityTasks`，见 [mysql_database.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/db_storage_mysql/mysql_database.cpp:440)。

`PutEntityTask::performBackgroundTask()` 执行数据库写入，`performEntityMainThreadTask()` 回主线程调用 handler，见 [put_entity_task.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/db_storage_mysql/tasks/put_entity_task.cpp:60)。

这与 [线程架构与后台任务](/architecture/threading-background-tasks) 中的后台任务模型一致。

## 一致性边界

BigWorld 的持久化不是强事务分布式数据库模型。需要注意：

- Base 和 Cell 数据要跨进程请求后拼接。
- 写 DB 前可能先备份 Base。
- Secondary DB 和 primary DB 有不同路径。
- DBApp channel 背压会导致归档跳过。
- BaseApp retiring/offload 会影响备份和写 DB。
- 如果 DBApp 不可用，源码会报数据丢失风险。

例如 `Base::writeToDB()` 找不到 DBApp 时会报错并返回 false，见 [base.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/baseapp/base.cpp:2129)。

## 当时取舍

这种设计的优点：

- DB 阻塞操作不压主 Reactor。
- Base 统一协调 Base/Cell 持久化。
- Archive 分摊到多个 tick，保护游戏循环。
- Secondary DB 提供恢复能力。
- IDatabase 抽象允许 MySQL/XML 等后端。

代价：

- 一致性模型复杂。
- 写 DB 失败的业务补偿依赖上层。
- Base/Cell 数据拼接有消息时序风险。
- DB schema 与 EntityDef 强绑定。
- 后台任务积压会影响登录、写档和恢复。

## 现代对比

<div class="decision-table">

| 方案 | 优点 | 代价 | 对 BigWorld 的判断 |
| --- | --- | --- | --- |
| DBApp + IDatabase | 与引擎状态模型深度集成 | 自定义协议和工具链 | 当前核心 |
| 直接业务 SQL | 简单 | 阻塞、耦合、难恢复 | 不适合主线程 |
| Async DB driver | 降低线程开销 | 需重写 DBApp 调用链 | 可作为后端优化 |
| Event sourcing | 回放和审计强 | 存储量和查询复杂 | 可用于新系统，不宜直接替换 |
| 分布式事务 | 强一致 | 延迟高、复杂 | 不适合高频游戏 Tick |
| 云数据库 + 队列 | 运维强 | 延迟和一致性需评估 | 适合外围服务 |

</div>

## 现代化建议

1. 给 DBApp channel window、archive skip、后台任务队列加指标。
2. 给 Base/Cell writeToDB 建立 tracing，串起 EntityID、DBID、GameTime、flags。
3. 为 EntityDef 持久化格式建立 golden tests。
4. 把写 DB 失败策略文档化，区分可重试、不可重试和数据丢失。
5. Python 3.12 迁移前先确认 Pickle、ScriptObject、DataSource/DataSink 与 DB stream 兼容。
6. 若引入 async DB，先封装在 IDatabase 后端，不改变 Base/DBApp 协议。

## 本章边界

本章解释持久化和 DB 线程模型。后续还需要继续分析安全、Watcher/Profiler/日志、内存生命周期、构建平台和 Python 3.12 迁移路线。
