# AOI、Witness 与 Ghost

<div class="arch-hero">

BigWorld 的 AOI 不是简单的“半径内广播”。Witness 维护客户端视野、带宽预算、实体别名、可靠位置更新和 SpaceData 同步；Ghost 维护跨 Cell 边界副本，为 AOI 和实体迁移提供连续性。

</div>

## 先给结论

AOI、Witness、Ghost 的关系：

- `Witness` 附着在有客户端的 real entity 上，负责管理该客户端的 Area of Interest。
- `EntityCache` 记录某个实体在该 Witness 视野内的发送状态。
- `Ghost` 是相邻 CellApp 上的实体副本，用于跨 Cell AOI、offload 和恢复。
- `EntityGhostMaintainer` 周期性维护 ghost，并判断 real entity 是否应该 offload 到其他 Cell。

关键源码：

- `Witness` 注释说明它在客户端附着到实体时创建，主要活动是管理 AoI list，见 [witness.hpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/witness.hpp:21)。
- `Entity` 注释说明 AoI 使用 x/z 等长范围和 hysteresis，进入 AoI 和离开 AoI 使用不同半径，见 [entity.hpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/entity.hpp:106)。
- `Witness` 维护 `aoiMap_`、`entityQueue_`、`aoiRadius_`、`aoiHyst_`、`pAoIRoot_`、`freeAliases_` 和可靠位置历史，见 [witness.hpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/witness.hpp:263)。
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

## EntityCache 与发送队列

Witness 内部维护：

- `entityQueue_`：已知实体发送队列。
- `aoiMap_`：AoI 内实体缓存映射。
- `bandwidthDeficit_`：带宽欠账。
- `maxPacketSize_`：每次更新包大小约束。
- `freeAliases_[256]`：客户端实体 alias 池。

源码见 [witness.hpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/witness.hpp:274)。

这说明 BigWorld 的 AoI 更新不是“一次性遍历全量实体发给客户端”，而是缓存实体状态并按预算调度。

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

## 当时取舍

BigWorld 的 AoI/Ghost 设计偏复杂，但它解决了开放世界 MMO 的核心问题：

- 玩家视野不应广播全世界。
- Cell 边界不应让实体突然消失。
- 负载均衡需要可迁移实体。
- 客户端带宽需要按实体优先级和状态变化调度。
- 可靠/不可靠混合位置更新需要上层语义参与。

代价：

- 状态机复杂。
- Ghost 创建/删除和 offload 顺序容易出 bug。
- AOI 抖动、带宽预算、可靠位置基准都需要大量测试。
- Debug 需要跨 Witness、EntityCache、RealEntity、Haunt、Channel。

## 现代对比

<div class="decision-table">

| 方案 | 优点 | 代价 | 对 BigWorld 的判断 |
| --- | --- | --- | --- |
| BigWorld Witness/Ghost | 深度贴合开放世界 MMO | 复杂度高 | 当前核心价值 |
| 简单半径广播 | 实现简单 | 带宽和边界问题严重 | 只适合小房间服 |
| Interest Management Service | 可独立扩展 | 增加网络跳数和一致性问题 | 可用于新架构，不易直接替换 |
| ECS spatial query | 数据局部性好 | 分布式 Ghost 仍需设计 | 可优化 Cell 内部 |
| Client-side prediction only | 客户端流畅 | 权威和作弊风险 | 不能替代服务端 AOI |

</div>

## 现代化建议

1. 给 Witness 更新拆分指标：enter/create/leave、volatile、reliable position、bandwidth deficit。
2. 给 Ghost 维护增加可视化：haunts、nextRealAddr、buffered messages。
3. 为 offload 消息重排建立 deterministic 测试。
4. 对 AoI hysteresis 做边界测试，防止反复 enter/leave。
5. 新增调试页展示某个玩家的 AoI map 和发送队列。
6. 优化时先测带宽和 Tick 时间，不要先重写 AOI 算法。

## 本章边界

本章解释 Client 可见性和跨 Cell 副本。下一章分析 Cell 分区与负载均衡，即这些 Ghost 和 offload 为什么会发生。
