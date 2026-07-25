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

## 持久化流到底写了什么

BigWorld 写数据库时，写入单元不是“某个属性生成一条 SQL”，而是“把当前实体的持久化视图重新序列化成一段实体流，再交给 DB 后端解释”。

Base 侧的证据很直接：

- `Base::addToStream()` 写 Base 属性时，只传 `EntityDescription::BASE_DATA | EntityDescription::ONLY_PERSISTENT_DATA`，见 [base.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/baseapp/base.cpp:3927)。
- 写 Cell 数据时，`PyCellData::addToStream()` 也传 `addPersistentOnly=true`，见 [base.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/baseapp/base.cpp:3944)。

Cell 侧不是单独维护另一套 DB 序列化格式，而是复用备份格式：

- `RealEntity::writeBackupProperties()` 的注释明确说，备份和 `writeToDB` 使用相同格式，见 [real_entity.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/real_entity.cpp:910)。
- `Base::getDBCellData()` 会先吃掉 Cell backup 的外层包，再把其中的 Cell 属性 dict 解出来，见 [base.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/baseapp/base.cpp:1215)。

所以完整链路是：

1. Base 决定归档或显式 `writeToDB()`。
2. 如果需要 Cell 数据，Base 向 Cell 请求备份格式的数据。
3. Base 从备份流里提取 Cell 属性 dict。
4. Base 重新拼出“Base persistent + Cell persistent”的实体流。
5. DBApp/IDatabase/MySQL 再把这段流写入数据库。

这里还要注意一个细节：落库流里除了实体属性，还可能伴随 Base mailbox、logoff、autoload 等元数据，它们不是属性列的一部分，而是和实体流并行处理。证据见 `IDatabase::putEntity(...)` 的参数和 `PutEntityTask` 对 `baseMailbox_` / `updateAutoLoad_` 的处理，分别在 [idatabase.hpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/db_storage/idatabase.hpp:221) 和 [put_entity_task.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/db_storage_mysql/tasks/put_entity_task.cpp:18)。

## 有没有通用的“脏数据标记”

这部分最容易被误解成 ORM。

当前源码里，确实存在“属性变化对象”，但它主要服务于 Cell 到 Client / Ghost 的增量同步：

- `PropertyOwnerBase::changeOwnedProperty()` 在属性变化时构造 `SinglePropertyChange`，见 [property_owner.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/entitydef/property_owner.cpp:19)。
- `Entity::onOwnedPropertyChanged()` 会把变化编码成客户端属性消息或 ghost 更新，见 [entity.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/entity.cpp:5572)。
- `PropertyChange::addToExternalStream()` / `addToInternalStream()` 是网络增量同步编码，不是数据库更新编码，见 [property_change.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/entitydef/property_change.cpp:139)。

但在数据库持久化路径上，我没有看到一套通用的“字段级 dirty set -> UPDATE 某几列”机制。源码证据更接近下面这个模型：

- 数据库写入由 `archive()`、显式 `writeToDB()`、销毁/登出等事件触发。
- 触发后重新从当前实体状态生成完整 persistent stream。
- MySQL 更新阶段再把这段流按映射拆回列或子表。

也就是说：

- 对网络同步来说，BigWorld 有精细的属性变化对象。
- 对数据库持久化来说，BigWorld 更像“按实体 persistent 视图整体刷新”，而不是典型 ORM 的 session dirty flush。

这也是为什么修改 Identifier 属性时，源码只是发出“以后写库可能失败”的提示，而不是立刻维护某个统一 dirty 标志，见 [entity.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/entity.cpp:6034)。

## 属性是怎么进数据库的

如果把问题具体化成“脚本里一个属性，最后怎么落到 MySQL 表里”，链路如下：

1. `Base::writeToDB()` 把当前 Base/Cell 的 persistent 属性写成二进制流，见 [base.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/baseapp/base.cpp:1984) 和 [base.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/baseapp/base.cpp:3927)。
2. `DBApp::writeEntity()` 读取 `flags`、`typeID`、`dbID`、`entityID`，把剩余流交给 `WriteEntityHandler`，见 [dbapp.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/dbapp/dbapp.cpp:2298)。
3. `WriteEntityHandler::writeEntity()` 决定这是首次插入、普通更新还是 logoff，仅在 `WRITE_BASE_CELL_DATA` 时把流传给数据库层，见 [write_entity_handler.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/dbapp/write_entity_handler.cpp:30)。
4. `MySqlDatabase::putEntity()` 创建 `PutEntityTask` 后台任务，见 [mysql_database.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/db_storage_mysql/mysql_database.cpp:440)。
5. `PutEntityTask::performBackgroundTask()` 调 `entityTypeMapping_.update(...)` 或 `insertNew(...)` / `insertExplicit(...)`，见 [put_entity_task.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/db_storage_mysql/tasks/put_entity_task.cpp:52)。
6. `EntityTypeMapping::visit()` 再用 `BASE_DATA | CELL_DATA | ONLY_PERSISTENT_DATA` 遍历 persistent 属性，把流拆成 SQL 参数，见 [entity_type_mapping.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/db_storage_mysql/mappings/entity_type_mapping.cpp:646)。

这条链路说明，数据库层从来没有直接读 Python 属性对象。它读到的是一段已经按 EntityDef 排好顺序的持久化流。

## MySQL 映射结构

MySQL 映射层不是单表万能映射，而是一组类型专用 `PropertyMapping`：

- `PropertyMapping` 是“流 <-> 数据库”的抽象基类，定义了 `fromStreamToDatabase()` 和 `fromDatabaseToStream()`，见 [property_mapping.hpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/db_storage_mysql/mappings/property_mapping.hpp:42)。
- `PropertyMapping::create()` 按 `DataType` 选择具体实现，见 [property_mapping.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/db_storage_mysql/mappings/property_mapping.cpp:132)。

从源码能确认几类常见映射：

- 基础数值、字符串、向量等，通常映射成列。
- `CLASS`、`FIXED_DICT` 走复合映射，不是简单一列。
- `ARRAY` / `TUPLE` 在非索引场景下可能走 `SequenceMapping` 子表，或者在 `dbLen() > 0` 时走 `BlobbedSequenceMapping`，见 [property_mapping.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/db_storage_mysql/mappings/property_mapping.cpp:149)。
- `USER_TYPE` 走用户类型映射，如果绑定失败会直接阻止 DBApp 继续，避免误改表结构，见 [property_mapping.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/db_storage_mysql/mappings/property_mapping.cpp:184)。

因此，一个 `.def` 属性并不必然等于“一列”。复杂类型可能展开成多列、子表，或者退化成 blob 存储。

除了实体属性表，MySQL 侧还维护运行时元数据：

- `EntityTypeMapping::addLogOnRecord()` 维护 `bigworldLogOns`，记录 `databaseID` 对应的 Base mailbox，见 [entity_type_mapping.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/db_storage_mysql/mappings/entity_type_mapping.cpp:774)。
- `updateAutoLoad()` 更新 autoload 标志，见 [entity_type_mapping.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/db_storage_mysql/mappings/entity_type_mapping.cpp:794)。

所以“数据库里的实体”实际上由两部分构成：

- persistent 属性映射出的业务数据。
- logon/autoload/mailbox 这类引擎运行时元数据。

## 这和 ORM 的区别

BigWorld 的数据库层和 ORM 有相似处，但核心模型并不一样：

<div class="decision-table">

| 维度 | BigWorld | 典型 ORM |
| --- | --- | --- |
| 持久化单元 | EntityDef 驱动的 persistent stream | 内存对象/Session |
| 写入触发 | `archive()` / `writeToDB()` / logoff | transaction flush / save |
| 更新粒度 | 重新生成实体 persistent 视图 | 常见是脏字段或脏对象 |
| 映射来源 | `.def` + `PropertyMapping` | 类注解/模型定义 |
| 复杂类型 | 可拆子表、复合列或 blob | 视 ORM 能力而定 |
| 运行时语义 | 和 AOI、Proxy、Base/Cell、登录元数据耦合 | 多数不关心实时网络语义 |

</div>

更准确的判断是：BigWorld 的 DB 层借用了“对象映射”的思路，但它首先是分布式实体引擎的持久化后端，不是通用业务 ORM。

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

## 源码取舍

持久化链路的核心取舍可以直接从代码看出来：

- DB 阻塞操作通过 `IDatabase` 后端任务下沉，不在 BaseApp 主 Reactor 里直接执行 SQL。
- Base 统一协调 Base/Cell persistent 数据，DBApp 只接收已经按 EntityDef 顺序编码好的实体流。
- Archive 按 tick 分摊，并受 DBApp channel send window 约束，避免写档把实时循环压垮。
- Secondary DB 复用 BaseApp 侧序列化结果，提供本地恢复路径，但不替代 primary DB。
- MySQL 映射层按 `PropertyMapping` 拆流到列、复合列、子表或 blob，不是 Python 对象到表的直接反射。

源码代价也很明确：

- Base/Cell 数据拼接依赖跨进程请求，消息时序和实体生命周期会影响写库结果。
- 数据库 schema 与 EntityDef persistent 视图强绑定，`.def` 变更会穿透到 digest、表同步和历史数据解释。
- 没有看到通用字段级 dirty set 驱动的 DB flush；属性变化对象主要服务网络增量同步。
- DBApp、后台任务、channel 背压和登录/恢复路径共用资源，积压会影响写档、查档和上线。

## 源码验证重点

持久化测试应覆盖实际链路，而不是只测 MySQL 后端能写入：

- `Base::archive()` 调用 `onPreArchive` 返回 false 时必须跳过写 DB。
- Base 有 Cell entity 且缺少 Cell 数据时，应先走 `writeToDBRequest` 请求 Cell 数据。
- `Base::addToStream()` 和 Cell 数据写入必须只包含 `ONLY_PERSISTENT_DATA`。
- 属性变化产生的 `PropertyChange` 不应被误当成数据库 dirty flush 机制。
- `EntityTypeMapping::visit()` 建表和写库时应使用同一份 persistent 视图。
- 复杂属性应按 `PropertyMapping::create()` 选择列、子表、复合映射或 blob。
- DBApp channel send window 超阈值时，Archiver 应跳过本轮 primary archive。
- `bigworldLogOns`、autoload、Base mailbox 和实体属性应分别验证，不能混成同一种属性列。
- `Base::writeToDB()` 找不到 DBApp 时应失败并记录数据丢失风险。

## 本章边界

本章解释持久化和 DB 线程模型。`.def` 如何决定 persistent 属性、Identifier、索引和数据库 digest，在 [EntityDef 契约与协议生成](/architecture/entitydef-contract-generation) 中继续展开。

## 相关文档

- EntityDef 契约生成，详见 [EntityDef 契约与协议生成](/architecture/entitydef-contract-generation)。
- 序列化实现，详见 [序列化与 EntityDef](/architecture/serialization-entitydef)。
- 实体模型，详见 [实体模型 Base/Cell/Client](/architecture/entity-model)。
- 实体生命周期，详见 [实体生命周期状态机](/architecture/entity-lifecycle-state-machine)。
- 登录与会话，详见 [登录、会话与 Proxy 接管](/architecture/login-session-proxy-flow)。
