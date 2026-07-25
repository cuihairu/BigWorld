# 实体生命周期状态机

<div class="arch-hero">

BigWorld 的 Entity 不是普通业务对象。它可以是 Base、Cell real、Cell ghost、Proxy、带 Witness 的客户端代表，也可以在 offload 中从 real 转 ghost，再在目标 CellApp onload 成 real。生命周期状态机是理解整个 MMO 架构的核心。

</div>

## 先给结论

BigWorld 实体生命周期至少有这些关键状态：

- Base-only：只有 BaseApp 上的长期实体。
- Base + pending Cell：Base 正在请求创建 Cell 实体。
- Base + Cell real：CellApp 上有权威 real 实体。
- Ghost：非权威副本，用于 AOI/跨 Cell 可见性。
- Offloading：real 正在迁移到另一个 CellApp。
- Destroying：实体进入销毁过程，但不能立即 delete。
- Zombie ghost：恢复场景中可能残留的 ghost。
- Restored real：从备份/迁移流恢复出的 real。

关键源码：

- `Base::createCellEntity()` 请求创建关联 Cell 实体，见 [base.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/baseapp/base.cpp:3321)。
- `Base::sendCreateCellEntity()` 发送 `CellAppInterface::createEntityNearEntity`，见 [base.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/baseapp/base.cpp:3363)。
- `Cell::createEntityInternal()` 创建 real 实体，见 [cell.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/cell.cpp:300)。
- `Entity::offload()` 发 `CellAppInterface::onload` 并调用 `convertRealToGhost()`，见 [entity.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/entity.cpp:1968)。
- `Entity::convertRealToGhost()` 写迁移数据并 flush Witness，见 [entity.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/entity.cpp:2005)。
- `Entity::readRealDataFromStreamForOnload()` 在目标 CellApp 读回 real 数据，见 [entity.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/entity.cpp:4710)。
- `Entity::destroy()` 进入 destroyed 状态并处理 real/ghost 差异，见 [entity.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/entity.cpp:2886)。
- `Cell::~Cell()` / `Cell::shutDown()` 会销毁所有 real entities，见 [cell.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/cell.cpp:98)。

## 状态机总览

<MermaidDiagram title="Entity 生命周期状态机">
stateDiagram-v2
  [*] --> BaseOnly: createBase / login
  BaseOnly --> PendingCell: Base.createCellEntity
  PendingCell --> CellReal: Cell.createEntityInternal
  CellReal --> Offloading: load balance / teleport
  Offloading --> SourceGhost: convertRealToGhost
  Offloading --> TargetReal: onload + readRealData
  SourceGhost --> Ghost: haunts / AOI copy
  Ghost --> Destroying: ghost destroyed
  CellReal --> Destroying: Entity.destroy
  Destroying --> Destroyed: setDestroyed
  Ghost --> ZombieGhost: restore edge case
  ZombieGhost --> Destroying: destroyZombie
  TargetReal --> CellReal: start controllers + backup
  Destroyed --> [*]
</MermaidDiagram>

这个状态机不是源码中的单个 enum，而是从多个类和调用链归纳出的架构状态。

## Base 创建 Cell 实体

`Base::createCellEntity()` 是脚本 API 入口之一。

它先检查是否已经有关联 Cell 实体，如果传入附近实体 mailbox，则调用 `sendCreateCellEntityToMailBox()`。源码见 [base.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/baseapp/base.cpp:3321)。

`Base::sendCreateCellEntity()` 会：

- 创建独立 `UDPBundle`，而不是直接使用 channel 自带 bundle。
- `startRequest( CellAppInterface::createEntityNearEntity, pHandler )`。
- 写入 nearby entity id。
- 写入当前 entity channel version。
- 写入 `isRestore = false`。
- 调用 `addCellCreationData()` 写入创建数据。
- 发送到目标 CellApp。

源码见 [base.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/baseapp/base.cpp:3363)。

这里有一个关键注释：不用 channel 自带 bundle，因为 streaming 可能失败，需要能中途 abort。这个细节说明 Entity 创建不是普通 RPC，它对数据流完整性要求很高。

## Cell 创建 Real

`Cell::createEntityInternal()` 是 CellApp 上创建 real 实体的核心路径。

关键步骤：

1. 从流中读取 `EntityID` 和 `EntityTypeID`。
2. 如果 ID 为 0，则通过 `idClient()` 分配新 ID。
3. 检查 population 中是否已有未销毁实体。
4. restore 场景下如果已有 ghost，可能视为 zombie ghost 并销毁。
5. `space_.newEntity(id, entityTypeID)` 创建 Entity。
6. 暂时禁止 callbacks。
7. 调用 `initReal()` 初始化 real。
8. 加入 real entity 列表。
9. 通知 population observers。
10. 恢复 callbacks。
11. 如果有 replay data，写入初始状态。
12. 立即 `pReal()->backup()` 发送备份到 BaseApp。

源码见 [cell.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/cell.cpp:300)。

重要设计点：

- 初始化期间禁止 callbacks，避免半初始化对象执行脚本。
- currentCell 消息延迟到和 backup 同 bundle，避免 Base 失去 cellData 与收到首个 backup 之间出现窗口。
- restore 场景显式处理 zombie ghost。

## Real 与 Ghost 的差异

在 BigWorld 中：

- Real 是权威实体，能执行位置、控制器、脚本逻辑和持久化相关状态。
- Ghost 是副本，服务 AOI 和跨 Cell 可见性。
- Entity 对象可以从 real 转 ghost，也可以在目标 CellApp 从 ghost/新对象 onload 成 real。

这不是复制对象那么简单，因为：

- Real-only 属性和 ghost 属性不同。
- Witness 只存在于某些 real Proxy 实体上。
- 控制器、Channel、Haunts、Ghosts 都要重新绑定。
- 迁移流必须保持 EntityDef 序列化顺序。

## Offload 迁移

`Entity::offload()` 只能在 real entity 上调用。它会：

- 断言 `isReal()`。
- 取目标 `CellAppChannel` 的 bundle。
- 非 teleport 时 start `CellAppInterface::onload` 消息。
- 调用 `convertRealToGhost(&bundle, pChannel, isTeleport)`。

源码见 [entity.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/entity.cpp:1968)。

`convertRealToGhost()` 做：

- 断言当前是 real。
- 禁止 callbacks。
- 如果有 Witness，先 `flushToClient()`。
- 如果有目标 pChannel，则 `writeRealDataToStream()` 写迁移数据。
- 后续销毁 real-only 部分，把实体留作 ghost。

源码见 [entity.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/entity.cpp:2005)。

迁移的本质：

- 源 CellApp：real -> ghost。
- 目标 CellApp：读取迁移流 -> real。
- Ghost 和 Haunt 需要知道 next real 地址。
- 客户端 Witness 状态必须在切换前 flush。

## Onload 恢复 Real

目标 CellApp 通过 `Entity::readRealDataFromStreamForOnload()` 读取迁移流。内部顺序必须和 offload 写入顺序一致：

1. 读取 real properties。
2. `TOKEN_CHECK( data, "RealProps" )`。
3. `createReal()`。
4. `pReal_->init( data, CREATE_REAL_FROM_OFFLOAD, ... )`。
5. 读取 exposed base properties for replay。
6. `startRealControllers()`。

源码见 [entity.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/entity.cpp:4710)。

这个顺序非常重要：如果 EntityDef、属性序列化、controller 数据或 replay 数据不兼容，实体迁移会失败。

## Destroy 流程

`Entity::destroy()` 注释说明：不能立刻 delete，因为其他对象可能还在引用它。源码见 [entity.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/entity.cpp:2882)。

如果是 real：

- 设置 `inDestroy_` 防重入。
- 用 `EntityPtr pThis = this` 保活。
- 调用脚本 `onDestroy`。
- 如果有 Base，通知 Base cell entity lost。
- `setDestroyed()`。
- 删除 ghosts。
- 通知 Cell `entityDestroyed()`。
- `convertRealToGhost()`。
- 如果没有 Base，则标记返回 ID。
- 从 population real channel 中清理。

源码见 [entity.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/entity.cpp:2886)。

如果是 ghost：

- 调用 `onGhostDestroyed`。
- 保留 real address 信息，用于处理后续可能收到的消息。

源码见 [entity.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/entity.cpp:2944)。

销毁不是析构，而是协议状态变化。

## Zombie Ghost

`Cell::createEntityInternal()` 在 restore 场景下，如果 population 中已有 ghost，则可能把它视为 zombie ghost。注释说明 critical channels 应保证不会恢复一个仍然 alive 的实体，但恢复时仍可能看到残留 ghost。源码见 [cell.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/cell.cpp:322)。

处理方式是循环调用 `destroyZombie()`，直到实体 destroyed。源码见 [cell.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/cell.cpp:334)。

这说明容灾恢复不是简单重建对象，还要处理旧状态残留。

## Cell 关闭

`Cell::~Cell()` 和 `Cell::shutDown()` 都会循环销毁 `realEntities_`。源码见 [cell.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/cell.cpp:98)。

这说明 Cell 是 real entity 的拥有/管理边界之一：

- Cell 删除前必须处理 real 实体。
- 实体 destroy 可能触发回调、Base 通知、Ghost 删除。
- 删除 Cell 不是释放容器那么简单。

## 生命周期风险

实体生命周期的高风险点：

- 初始化期间脚本回调导致半初始化对象被访问。
- Offload 流写入/读取顺序不一致。
- EntityDef 变更导致迁移流不兼容。
- Destroy 中脚本回调再次触发 destroy。
- Base/Cell 通知失败导致状态不一致。
- Ghost 残留导致 restore 冲突。
- Witness 未 flush 导致客户端看到过期状态。
- Channel version 不匹配导致旧消息污染新 real。

这些风险解释了为什么 BigWorld 需要大量状态断言、callbacksPermitted、channel version、backup 和 ghost 机制。

## 源码取舍

BigWorld 采用 real/ghost/offload，是因为实体生命周期要同时满足空间权威、可见性和迁移：

- MMO 世界空间太大，不能单进程承载。
- AOI 跨 Cell 边界必须看到附近实体。
- 负载均衡需要把实体权威从一个 CellApp 移到另一个 CellApp。
- 客户端连接和长期身份在 BaseApp，空间逻辑在 CellApp。
- 实体迁移要保持脚本对象和 EntityID 连续性。

源码代价：

- 生命周期状态复杂。
- 序列化和热更新强耦合。
- 测试难度高。
- 跨进程 trace 必不可少。
- 脚本对象生命周期和 C++ Entity 生命周期紧耦合，析构、回调许可和迁移流必须一起验证。
- Channel version、buffered messages、zombie ghost 处理都增加了消息顺序复杂度。

## 源码验证重点

实体生命周期测试应围绕状态转换和跨进程序列：

- Base 创建、Cell 创建、real entity 创建和客户端 player 创建顺序必须一致。
- `callbacksPermitted` 为 false 时不应触发脚本生命周期回调。
- destroy 期间再次触发 destroy 或脚本回调时不能破坏状态机。
- offload/onload 流字段顺序必须和 EntityDef、backup、ghost 数据保持兼容。
- real/ghost 切换期间的 `ghostSetReal`、`ghostSetNextReal` 和 buffered messages 应按 channel version 过滤旧消息。
- zombie ghost 和 restore 场景应覆盖源 CellApp 死亡、目标 real 不存在、BaseApp 通知失败等路径。
- Witness 未 flush、Cell destroy pending、Base mailbox 更新失败都应有回归用例。

## 本章边界

本章把实体生命周期串成状态机。更细的 AOI、Cell 负载均衡、Offload 数据流已分别在 [AOI、Witness 与 Ghost](/architecture/aoi-witness-ghost)、[Cell 分区与负载均衡](/architecture/cell-partition-load-balance)、[实体迁移与 Offload](/architecture/entity-migration-offload) 中展开。
