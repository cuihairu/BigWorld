# AOI、Witness 与 Ghost

<div class="arch-hero">

BigWorld 的 AOI 不是简单的“半径内广播”。Witness 维护客户端视野、带宽预算、实体别名、可靠位置更新和 SpaceData 同步；Ghost 维护跨 Cell 边界副本，为 AOI 和实体迁移提供连续性。

</div>

## 先给结论

AOI、Witness、Ghost 的关系：

- `Witness` 附着在有客户端的 real entity 上，负责管理该客户端的 Area of Interest。
- `AoITrigger` 把 RangeList 的进入/离开事件转换成 `Witness::addToAoI()` / `removeFromAoI()`。
- `EntityCache` 记录某个实体在该 Witness 视野内的客户端发送状态，不是普通集合元素。
- 客户端看到新实体不是一次性 create，而是 `enterAoI -> requestEntityUpdate -> createEntity` 的两阶段握手。
- Witness 更新队列按 priority heap、AoIUpdateScheme、packet budget 和 bandwidth deficit 调度。
- `Ghost` 是相邻 CellApp 上的实体副本，用于跨 Cell AOI、offload 和恢复。
- `EntityGhostMaintainer` 周期性维护 ghost，并判断 real entity 是否应该 offload 到其他 Cell。

关键源码：

- `Witness` 注释说明它在客户端附着到实体时创建，主要活动是管理 AoI list，见 [witness.hpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/witness.hpp:21)。
- `Entity` 注释说明 AoI 使用 x/z 等长范围和 hysteresis，进入 AoI 和离开 AoI 使用不同半径，见 [entity.hpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/entity.hpp:106)。
- `Witness` 维护 `aoiMap_`、`entityQueue_`、`aoiRadius_`、`aoiHyst_`、`pAoIRoot_`、`freeAliases_` 和可靠位置历史，见 [witness.hpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/witness.hpp:263)。
- `EntityCache` 的 flags 包含 `ENTER_PENDING`、`REQUEST_PENDING`、`CREATE_PENDING`、`GONE`、`WITHHELD`、`REFRESH`，见 [entity_cache.hpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/entity_cache.hpp:104)。
- `AoITrigger::triggerEnter()` / `triggerLeave()` 在 [witness.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/witness.cpp:3586)。
- `Witness::update()` 的发送队列主循环在 [witness.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/witness.cpp:1088)。
- `EntityGhostMaintainer::check()` 先检查 offload，再标记/创建/删除 haunts，见 [entity_ghost_maintainer.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/entity_ghost_maintainer.cpp:42)。

## Witness 是什么

Witness 不是客户端连接本身。客户端连接主要在 Proxy/BaseApp；Witness 在 CellApp 上，代表“这个玩家实体看到什么”。

`Witness` 的职责包括：

- 维护 AoI 内实体集合。
- 决定实体 enter/create/leave 消息。
- 管理实体 ID alias，减少客户端包体。
- 发送 SpaceData 变化。
- 发送自身详细位置和其他实体 volatile 更新。
- 根据带宽预算调度更新。
- 处理手动 AoI、withhold、position detailed 等脚本控制。

源码见 [witness.hpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/witness.hpp:91)。

`Witness` 构造时还会做几件关键初始化：

- 使用 `defaultAoIRadius` 作为初始 AoI 半径。
- 把 `pAoIRoot_` 默认设为本实体的 `RangeListNode`。
- 初始化 `freeAliases_`，并保留 `NO_ID_ALIAS`。
- 向 `CellApp` 注册 witness。
- 先向 Proxy 发送首个 `tickSync`。
- 如果是初始创建 real，则发送 `createCellPlayer` 给客户端。

源码见 [witness.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/witness.cpp:124)。

## AoI 的 hysteresis

`Entity` 注释明确说 AoI 是 x/z 轴等长范围，并有外扩 hysteresis 区域：

- 实体进入 AoI：进入内层 AoI 范围。
- 实体离开 AoI：移动到 hysteresis 外才离开。

源码见 [entity.hpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/entity.hpp:106)。

这能避免边界抖动：

- 没有 hysteresis 时，实体在边界来回抖动会反复 enter/leave。
- 有 hysteresis 后，进入和离开阈值分离，减少网络消息和客户端对象 churn。

<MermaidDiagram title="AoI 与 Hysteresis">
flowchart TD
  A[Entity enters inner AoI] --> B[send enter/create]
  B --> C[Known in Witness AoI]
  C --> D{position outside hysteresis?}
  D -- no --> C
  D -- yes --> E[send leave/delete]
</MermaidDiagram>

真正触发进入/离开的不是每 tick 全量扫描，而是 `AoITrigger` 接在 `RangeList` 上：

- `AoITrigger` 继承 `RangeTrigger`，构造时会先访问当前覆盖到的大实体，再插入 RangeList，见 [witness.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/witness.cpp:73)。
- `triggerEnter()` 忽略自己和 manual AoI 实体，然后调用 `Witness::addToAoI()`，见 [witness.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/witness.cpp:3586)。
- `triggerLeave()` 同样忽略自己和 manual AoI 实体，然后调用 `Witness::removeFromAoI()`，见 [witness.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/witness.cpp:3602)。

`setAoIRadius()` 会先把 radius clamp 到 `maxAoIRadius`，再调用 `pAoITrigger_->setRange()`，并根据 AoI root 类型更新实体或移动节点上的 trigger，见 [witness.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/witness.cpp:2109)。相关配置在 [cellapp_config.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/cellapp_config.cpp:28)。

这说明 AoI 半径、最大半径、ghostDistance 不是文档概念，而是 CellApp 配置约束的一部分。`CellAppConfig::sanityCheckSettings()` 还会检查 `maxAoIRadius` 与 `ghostDistance`、`defaultAoIRadius` 与 `maxAoIRadius` 的关系，见 [cellapp_config.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/cellapp_config.cpp:163)。

## EntityCache 与发送队列

Witness 内部维护：

- `entityQueue_`：已知实体发送队列。
- `aoiMap_`：AoI 内实体缓存映射。
- `bandwidthDeficit_`：带宽欠账。
- `maxPacketSize_`：每次更新包大小约束。
- `freeAliases_[256]`：客户端实体 alias 池。

源码见 [witness.hpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/witness.hpp:274)。

这说明 BigWorld 的 AoI 更新不是“一次性遍历全量实体发给客户端”，而是缓存实体状态并按预算调度。

`EntityCache` 是这个队列的状态机。flags 定义包括：

- `ENTER_PENDING`：等待向客户端发送 `enterAoI`。
- `REQUEST_PENDING`：已发送 enter，等待客户端 `requestEntityUpdate`。
- `CREATE_PENDING`：已收到 request，等待发送 `createEntity`。
- `GONE`：等待从优先级队列中移除。
- `WITHHELD`：实体在 AoI 中，但暂不发给客户端。
- `REFRESH`：需要把客户端视图删掉再重新进入。
- `PRIORITISED`：下一次 `Witness::update()` 要优先发送。
- `MANUALLY_ADDED` / `ADDED_BY_TRIGGER`：区分脚本手动 AoI 与 RangeList 自动触发。

源码见 [entity_cache.hpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/entity_cache.hpp:138)。

进入 AoI 时，`Witness::addToAoI()` 会：

1. 忽略 destroyed entity 和不能出现在客户端的 entity。
2. 如果 cache 已存在且是 `GONE`，调用 `EntityCache::reuse()`。
3. 如果 cache 不存在，加入 `aoiMap_`，设置 `ENTER_PENDING`，再加入 `entityQueue_`。
4. 标记来源是手动添加还是 trigger 添加。
5. 非 offload 场景下调用实体脚本 `onEnteredAoI`。

源码见 [witness.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/witness.cpp:2202)。

离开 AoI 时，`Witness::removeFromAoI()` 不会立即删除 cache。它会清理 manual/trigger 来源标记；如果另一个来源仍然存在，就直接返回。真正要离开时只设置 `GONE`，并调用脚本 `onLeftAoI`，见 [witness.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/witness.cpp:2305)。

这个延迟删除很重要：leave 消息要和后续 bundle、event number、alias 回收保持顺序，不能在 RangeList 触发时直接从所有结构里消失。

## enter/request/create/leave 握手

客户端第一次看到 AoI 实体不是直接收到完整实体数据。源码里是一个小状态机：

<MermaidDiagram title="AoI 实体进入客户端的状态机">
stateDiagram-v2
  [*] --> ENTER_PENDING: addToAoI
  ENTER_PENDING --> REQUEST_PENDING: sendEnter
  REQUEST_PENDING --> CREATE_PENDING: requestEntityUpdate
  CREATE_PENDING --> UPDATABLE: sendCreate
  UPDATABLE --> GONE: removeFromAoI
  GONE --> [*]: leaveAoI + delete cache
  GONE --> REFRESH: reuse but event history trimmed
  REFRESH --> ENTER_PENDING: delete + re-enter
</MermaidDiagram>

`Witness::sendEnter()` 会分配 ID alias，把 cache 从 `ENTER_PENDING` 改成 `REQUEST_PENDING`，然后根据目标是否在 vehicle 上发送 `enterAoIOnVehicle` 或 `enterAoI`，见 [witness.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/witness.cpp:1601)。

客户端随后通过 `requestEntityUpdate` 回来。`Witness::requestEntityUpdate()` 会：

- 确认 entity 还在 AoI。
- 拒绝请求自己或请求非 pending entity 的异常情况。
- 把 `REQUEST_PENDING` 改成 `CREATE_PENDING`。
- 校验客户端传来的 LoD event stamps 数量不能超过 cache 的 LoD 数。
- 记录 LoD event numbers，然后重新加入发送队列。

源码见 [witness.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/witness.cpp:2025)。

`Witness::sendCreate()` 清除 `CREATE_PENDING`，写 vehicle 变化，按实体是否 volatile 选择 `createEntity` 或 `createEntityDetailed`，再写 entity id、client type id、位置、方向和外层 detail level 属性，见 [witness.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/witness.cpp:1643)。

离开时，`EntityCache::addLeaveAoIMessage()` 发送 `leaveAoI`，并把 LoD event numbers 写回 bundle，见 [entity_cache.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/entity_cache.cpp:342)。随后 `Witness::onLeaveAoI()` 回收 ID alias，见 [witness.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/witness.cpp:2096)。

这个握手设计的价值是：

- enter 很轻，先告诉客户端“有这个实体”。
- create 可以根据客户端已有 event stamps 和 LoD 状态补差量。
- leave 携带 event stamps，为实体快速离开又重新进入时复用或 refresh 提供依据。
- alias 只给 volatile entity 分配，减少高频位置更新包体。

`Witness::allocateIDAlias()` 明确只给有 volatile 数据的实体分配 alias；没有可用 alias 或非 volatile 实体返回 `NO_ID_ALIAS`，见 [witness.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/witness.cpp:1881)。

## Witness 发送预算与优先级

`Witness::update()` 是 AOI 发送的核心热路径。它先计算本 tick 期望包大小：

```cpp
const float throttle = CellApp::instance().emergencyThrottle();
const int desiredPacketSize = int(maxPacketSize_ * throttle) -
        bandwidthDeficit_ + bundle.size();
```

源码见 [witness.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/witness.cpp:1102)。

然后它按固定顺序发送：

1. SpaceData changes。
2. reference position 或 player detailed position。
3. vehicle stack 内的实体，强制优先。
4. `entityQueue_` heap 中 priority 最靠前的实体。
5. 根据 bundle size、`desiredPacketSize` 和 `witnessUpdateMaxPriorityDelta` 停止本 tick 发送。
6. 更新 `bandwidthDeficit_`，必要时封顶。
7. 重新维护 heap。
8. flush 到 Proxy，并写下一 tick 的 `tickSync`。

源码见 [witness.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/witness.cpp:1088)。

`handleStateChange()` 负责把 pending flags 推进为客户端消息：

- `GONE`：发送 leave 并从 AoI 删除。
- `WITHHELD`：如果客户端已经知道该实体，先 delete，再重新设为 `ENTER_PENDING`。
- `ENTER_PENDING`：发送 enter。
- `CREATE_PENDING`：发送 create 并更新 priority。
- `REFRESH`：delete 后重新 enter。

源码见 [witness.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/witness.cpp:986)。

普通 updatable entity 走 `sendQueueElement()`。它先计算 LoD priority，再调用实体的 `writeClientUpdateDataToBundle()` 写属性/位置更新，见 [witness.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/witness.cpp:1043)。

priority 不是单纯距离排序。`EntityCache::updatePriority()` 用 `AoIUpdateSchemes::apply(updateSchemeID_, distSQ)` 计算 delta，并用 `witnessUpdateDeltaGrowthThrottle` 限制 delta 增长，最后累加到 cache priority，见 [entity_cache.ipp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/entity_cache.ipp:106)。

`AoIUpdateScheme` 的默认公式是 `(distance * distanceWeighting + 1) * weighting`。如果 min/max delta 都为 0，则该 scheme 被视为 coincident，LoD priority 按 0 处理，见 [aoi_update_schemes.hpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/aoi_update_schemes.hpp:18) 和 [aoi_update_schemes.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/aoi_update_schemes.cpp:90)。

这套机制的实际效果：

- 近处实体天然更频繁更新。
- 远处实体不会因为每 tick 都在队列里而吃光预算。
- vehicle stack 优先，避免玩家所在载具位置关系抖动。
- emergency throttle 和 bandwidth deficit 会把网络压力反馈到本 tick 发送量。

## 位置更新可靠性

`Witness::addDetailedPlayerPosition()` 会决定是否发送玩家自身详细位置，并判断是否可以跳过未变化的位置，见 [witness.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/witness.cpp:882)。

关键逻辑：

- 非 volatile 或 witness-controlled entity 不发送。
- 记录 `lastSentReliableGameTime_`、`lastSentReliablePosition_`、`lastSentReliableDirection_`。
- 如果位置方向未变化，并且在配置允许时间内，可以跳过。
- 某些情况下会把下一条消息标为 reliable。

发送可靠位置后调用 `onSendReliablePosition()` 记录最后可靠位置，见 [witness.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/witness.cpp:1416)。

这和 Mercury 的可靠 UDP 设计直接相关：位置通常是 volatile，但某些参考位置或详细位置必须可靠，否则后续相对位置会失去基准。

## tickSync

Witness 更新末尾会向 Proxy 发送 `tickSync`，告诉 Proxy 后续消息属于下一 tick：

源码见 [witness.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/witness.cpp:1406)。

这说明客户端同步不是纯实时流，而是围绕 GameTime/Tick 做批次边界。

## Ghost 的作用

Ghost 是 real entity 的跨 Cell 副本。它服务三个目标：

- 让邻近 CellApp 能给本地 Witness 提供远端实体的视图。
- 让 real entity offload 到相邻 Cell 时，目标 Cell 已有基础状态。
- 在 CellApp death 或消息乱序时提供恢复和缓冲依据。

`EntityGhostMaintainer` 的 `check()` 会：

1. 如果当前 Cell 允许 offload，检查实体是否应该迁移到 home cell。
2. 标记所有 haunts。
3. 创建或取消标记仍需要的 haunts。
4. 删除无效 haunts。

源码见 [entity_ghost_maintainer.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/entity_ghost_maintainer.cpp:46)。

## AOI 完整调用链

### 实体进入 AOI

实体进入玩家视野的完整调用链：

源码入口：[witness.cpp:3586](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/witness.cpp:3586)

<div class="flow-strip">
  <span class="flow-node">AoITrigger::triggerEnter()</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">忽略自己和 manual AoI</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">Witness::addToAoI()</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">创建 EntityCache</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">设置 ENTER_PENDING</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">加入 entityQueue_</span>
</div>

### 实体离开 AOI

实体离开玩家视野的完整调用链：

源码入口：[witness.cpp:2096](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/witness.cpp:2096)

<div class="flow-strip">
  <span class="flow-node">AoITrigger::triggerLeave()</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">EntityCache::addLeaveAoIMessage()</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">发送 leaveAoI</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">写入 LoD event numbers</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">Witness::onLeaveAoI()</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">回收 ID alias</span>
</div>

### Witness 更新发送

Witness 每 Tick 更新并发送实体数据的完整调用链：

源码入口：[witness.cpp:1088](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/witness.cpp:1088)

<div class="flow-strip">
  <span class="flow-node">Witness::update()</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">计算 desiredPacketSize</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">发送 SpaceData changes</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">发送 reference position</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">发送 vehicle stack 实体</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">按 priority 发送 entityQueue_</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">更新 bandwidthDeficit_</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">flush 到 Proxy</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">写 tickSync</span>
</div>

### Ghost 创建与维护

Ghost 实体的创建和维护调用链：

源码入口：[entity_ghost_maintainer.cpp:46](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/entity_ghost_maintainer.cpp:46)

<div class="flow-strip">
  <span class="flow-node">EntityGhostMaintainer::check()</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">检查 offload 状态</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">标记所有 haunts</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">创建/取消标记 haunts</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">删除无效 haunts</span>
</div>

## Offload 前的 Ghost 准备

`EntityGhostMaintainer::checkEntityForOffload()` 会根据实体位置查 `Space::pCellAt(x,z)`：

- 如果 home cell 是当前 cell，不 offload。
- 如果目标 cell 正在删除，不 offload。
- 如果目标 CellApp channel 不存在或不健康，不 offload。
- 否则把目标加入 offload list。

源码见 [entity_ghost_maintainer.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/entity_ghost_maintainer.cpp:81)。

这体现了 BigWorld 的安全边界：不能因为坐标跨边界就盲目迁移，必须确认目标 Cell 和 Channel 可用。

## Ghost 消息顺序

实体 offload 时，旧 real 会通知 ghosts 下一个 real 地址：

- `RealEntity::destroy()` 遍历 haunts。
- 对非目标 haunt 发送 `ghostSetNextReal`。
- 然后 reset/destroy 当前 real channel。

源码见 [real_entity.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/real_entity.cpp:246)。

`Entity::ghostSetNextReal()` 收到后设置 `nextRealAddr_`，并播放 buffered ghost messages，见 [entity.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/entity.cpp:4863)。

这就是 Channel version 和 buffered ghost messages 的业务意义：offload 会造成消息重排，Ghost 必须知道何时开始接受新 real 的消息。

## 源码取舍

源码里的 AoI/Ghost 不是单一“空间查询结果”，而是几套状态共同维护客户端可见性：

- `AoITrigger` 只负责把 RangeList 进入/离开事件转成 Witness 操作。
- `EntityCache` 保存客户端侧是否 pending、created、gone、withheld、refresh，以及 LoD event numbers。
- `Witness::update()` 按 packet budget、bandwidth deficit、priority heap 和 vehicle stack 分批写 bundle。
- `EntityGhostMaintainer` 维护 haunts，并在 offload 前确认目标 Cell 与 Channel 可用。
- `RealEntity` / `Entity` 的 `ghostSetNextReal`、buffered ghost messages 用于处理迁移期间的 real 切换和消息乱序。

这套设计把可见性、带宽、可靠位置、跨 Cell 副本和迁移顺序绑在一起。好处是客户端不会因为 Cell 边界或短暂离开 AoI 就丢失上下文；代价是调试必须同时看 Witness、EntityCache、RealEntity、Haunt、Channel 和 event history。

## 源码边界

几个容易误读的边界需要按源码处理：

- `requestEntityUpdate()` 请求非 pending cache 时记录错误并返回；LoD stamps 超过数量时记录 `CHEAT` 日志并截断，不是直接断开连接。
- `EntityCache::reuse()` 只有在 cache 仍是 `GONE` 且事件历史没有被裁剪时才能安全复用；如果 `lastEventNumber` 早于 `lastTrimmedEventNumber`，会设置 `REFRESH`。
- `handleStateChange()` 处理 `REFRESH` 时会先 `deleteFromClient()`，再 `setEnterPending()`，让客户端重新走 enter/request/create。
- `removeFromAoI()` 会区分 manual 和 trigger 来源；只要任一来源仍存在，就不能发 leave。
- `allocateIDAlias()` 只给有 volatile 数据且 alias 池有空位的实体分配别名。

## 源码验证重点

AOI/Witness/Ghost 的测试要覆盖状态顺序，而不是只看客户端最终有没有实体：

- 实体进入 AoI 后必须先进入 `ENTER_PENDING`，发送 enter 后才变成 `REQUEST_PENDING`。
- 客户端 `requestEntityUpdate` 只能作用于 pending cache，请求非 pending entity 应拒绝。
- LoD event stamps 数量超过 cache LoD 数时应被截断并记录 `CHEAT` 日志，不应越界写入 LoD event number。
- `removeFromAoI()` 只清理对应来源；manual 和 trigger 其中一个仍存在时不能发送 leave。
- `GONE` cache 应在 `Witness::update()` 中发 leave，并回收 alias。
- `EntityCache::reuse()` 遇到 event history 已被 trim 时必须触发 refresh，而不是复用旧 cache。
- `setAoIRadius()` 超过 `maxAoIRadius` 时必须 clamp。
- `witnessUpdateMaxPriorityDelta` 和 packet budget 应限制单 tick 发送数量。
- vehicle stack 内实体应优先于普通 heap 队列发送。
- offload 期间替换 AoI root 不应重复触发 `onEnteredAoI`。

## 本章边界

本章解释 Client 可见性和跨 Cell 副本。下一章分析 Cell 分区与负载均衡，即这些 Ghost 和 offload 为什么会发生。
