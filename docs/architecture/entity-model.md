# 实体模型：Base / Cell / Client

<div class="arch-hero">

BigWorld 的实体不是单对象模型，而是拆成 Base、Cell、Client 三个权威域。Base 承载会话、数据库和跨 Cell 协调；Cell 承载空间、位置、AOI、物理和 real/ghost；Client 只接收经过 Witness 和带宽调度过滤后的视图。

</div>

## 先给结论

BigWorld 的核心抽象是“同一个游戏实体在不同进程中有不同部分”：

- `Base`：长期在线身份、数据库 ID、Proxy/客户端连接、Cell mailbox、创建/销毁 Cell 实体、写 DB、备份归档。
- `Cell Entity`：空间内权威对象，维护位置、方向、Controller、AOI、real/ghost、事件历史和客户端同步。
- `Client Entity`：客户端可见投影，通过 Witness、EntityCache、Bundle 和 EntityDef 同步生成。

关键源码：

- `Base` 注释说明 Base entity 驻留在 BaseApp，可创建关联 Cell entity，并通过 `CellEntityMailBox` 与 Cell entity 通信，见 [base.hpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/baseapp/base.hpp:38)。
- `Base` 提供 `createCellEntity()`、`restoreTo()`、`cellBundle()`、`sendToCell()`、`writeToDB()`、`backupTo()`、`offload()` 等接口，见 [base.hpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/baseapp/base.hpp:97)。
- `Entity` 注释说明 Cell entity 可为 real 或 ghost，每个实体只有一个 authoritative real，可有 0 到多个 ghost，见 [entity.hpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/entity.hpp:89)。
- `Entity` 能访问 base 和 client mailbox，远程调用由 `.def` 文件指定，见 [entity.hpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/entity.hpp:127)。

## 三层职责

<div class="decision-grid">
  <div class="decision-card">
    <h3>Base</h3>
    <p>身份、会话、数据库、Proxy、全局逻辑、Cell 引用和跨 Cell 协调。</p>
  </div>
  <div class="decision-card">
    <h3>Cell</h3>
    <p>空间权威、位置、物理、AOI、real/ghost、Controller 和移动。</p>
  </div>
  <div class="decision-card">
    <h3>Client</h3>
    <p>只接收可见实体和属性投影，不拥有服务端权威状态。</p>
  </div>
</div>

这种分层不是 MVC，也不是普通 ECS。它是 MMO 分布式状态归属模型。

## Base 的职责

`Base` 继承 `PyObjectPlus`，是 Python 脚本可见对象。它保存：

- `EntityID`
- `DatabaseID`
- `EntityType`
- Cell mailbox
- 当前 `SpaceID`
- UDP channel
- timers
- backup/archive 策略
- destroy/create cell pending 状态

源码见 [base.hpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/baseapp/base.hpp:58)。

Base 的重要方法包括：

- `writeToDB()`：写数据库。
- `requestCellDBData()`：向 Cell 请求持久化数据。
- `backupTo()` / `writeBackupData()`：备份到其他 BaseApp。
- `offload()`：BaseApp 退休或负载迁移时迁出。
- `createCellEntity()`：创建对应 Cell 实体。
- `setCurrentCell()`：更新 Cell 实体所在 CellApp 地址。
- `callBaseMethod()` / `callCellMethod()`：跨域方法调用。

这说明 Base 是实体长期身份和跨域协调的中心。

## Cell Entity 的职责

`Entity` 也继承 `PyObjectPlus`，但它驻留在 CellApp，负责空间内权威逻辑。

源码注释列出关键语义：

- real / ghost。
- 位置和方向。
- Controller 驱动的位置变化。
- AOI。
- Client 可见性。
- Base mailbox 和 Client mailbox。

见 [entity.hpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/entity.hpp:89)。

Cell Entity 保存和暴露：

- `position()` / `direction()`。
- `volatileInfo()`。
- `isReal()` / `pReal()`。
- `realAddr()` / `nextRealAddr()`。
- `Space` / `Cell`。
- `EventHistory`。
- `writeClientUpdateDataToBundle()`。
- `addHistoryEventLocally()`。
- `offload()` / `onload()` / `createGhost()`。

见 [entity.hpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/entity.hpp:163)。

## Real 与 Ghost

每个 Cell Entity 有且只有一个 authoritative real，可能有多个 ghost。

ghost 的作用：

- 让邻近 CellApp 提前拥有实体副本。
- 支撑 AOI 跨 Cell 边界。
- 支撑 entity offload 前后消息不中断。
- 支撑 CellApp death 和恢复场景下的状态判断。

real 的职责：

- 拥有权威脚本逻辑。
- 写 real-only 属性。
- 驱动位置、Controller 和事件历史。
- 维护 haunts，也就是 ghost 所在 CellApp。

这就是 BigWorld 与普通分片服务器的差异：它不是简单把坐标落在哪个分片就只在哪个分片存在，而是在边界附近主动维护 ghost。

## Mailbox 是跨域引用

Base 可以通过 `CellEntityMailBox` 与 Cell entity 通信；Cell entity 可以通过 Base mailbox 和 Client mailbox 调用远端方法。

这和 [通信抽象与 RPC](/architecture/communication-rpc) 直接相关：

- Mailbox 不是对象指针。
- 它封装 EntityID、地址、组件归属和接口方法。
- 方法参数由 EntityDef 的 `MethodArgs` 序列化。
- 传输层由 Mercury Bundle / Channel 承载。

所以 BigWorld 的“对象方法调用”本质是分布式消息。

## 生命周期回调

Base 侧回调包括：

- `onDestroy`
- `onOnload`
- `onRestore`
- `onGetCell`
- `onLoseCell`
- `onCreateCellFailure`
- `onWriteToDB`
- `onPreArchive`
- `onTimer`

源码见 [base.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/baseapp/base.cpp:447)。

Cell 侧回调包括：

- `onRestore`
- `onGhostCreated`
- `onGhostDestroyed`
- `onWriteToDB`
- `onNoise`
- `onSpaceGone`
- `onGetWitness`
- `onLoseWitness`
- `onWitnessed`
- `onEnteringCell`
- `onEnteredCell`
- `onLeavingCell`
- `onLeftCell`

源码见 [entity.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/entity.cpp:733)。

这些回调是脚本层扩展点，但也是风险点：回调中可能销毁、迁移、offload 实体，所以 C++ 代码经常要防止对象生命周期变化。

## 为什么这样拆

这种拆分解决了 MMO 的几个问题：

- 玩家连接和空间模拟不一定在同一进程。
- 空间负载可以按 Cell 切分迁移。
- Base 长期存在，Cell 可随位置创建、销毁、迁移。
- 数据库写入由 Base 统一协调，必要时请求 Cell 数据。
- Client 只看 Witness 筛选后的投影，不需要知道完整服务端状态。

这是 BigWorld 的核心设计价值之一。

## 代价

代价同样明显：

- 一个实体的状态分散在 Base、Cell、Client、DB、Ghost 中。
- 生命周期复杂，创建/销毁/迁移都要跨进程协调。
- Mailbox 可能过期，Channel version 需要处理旧包。
- EntityDef 变更会影响多个域。
- 脚本回调可能改变迁移过程中的对象状态。
- 调试一个实体问题需要跨 BaseApp、CellApp、DBApp、客户端日志。

## 源码取舍

Base/Cell/Client 三层模型的源码取舍是把不同权威域拆开：

- Base 承担长期身份、数据库协调、Proxy 和跨 Cell 生命周期管理。
- Cell 承担空间、位置、AOI、物理和 real/ghost 状态。
- Client 只接收 Witness 筛选后的实体投影和自身可调用的 exposed 方法。
- DB 只持久化 EntityDef persistent 视图，不保存完整运行时对象图。

源码代价是一个实体问题通常跨多个域传播：Mailbox、Channel、EntityDef、DBID、Cell real/ghost、Client entity cache 都可能参与同一条故障链。

## 源码验证重点

实体模型测试应覆盖域边界：

- Base-only、Cell-only、Client-visible、Persistent 属性应按 EntityDef 数据域进入对应流。
- 创建 Cell entity 时，Base mailbox、Cell mailbox 和客户端 player 关系应一致。
- Cell 丢失、offload、restore 后，Base 持有的 Cell mailbox 不能指向旧地址。
- Client 方法只能通过 exposed method range 调用，不能绕过 Proxy 直连 CellApp。
- DB 写入应由 Base 协调，必要时请求 Cell persistent 数据。
- Ghost 只应作为跨 Cell 副本和迁移辅助，不能被误当成客户端实体连接。

## 本章边界

本章解释实体三层模型。下一章继续分析 AOI、Witness 与 Ghost，这是 Client 可见性和跨 Cell 边界一致性的核心。
