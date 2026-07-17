# 实体迁移与 Offload

<div class="arch-hero">

BigWorld 的实体迁移不是把对象 memcpy 到另一个进程。它是 Real/Ghost 状态转换、脚本回调、属性序列化、Channel 迁移、Ghost sign-off、buffered messages 和 Cell 边界控制共同完成的状态机。

</div>

## 先给结论

Cell 实体 offload 的核心路径：

1. Cell 周期性执行 `checkOffloadsAndGhosts()`。
2. `OffloadChecker` 遍历 real entities。
3. `EntityGhostMaintainer` 判断实体位置属于哪个 home cell。
4. 确认目标 CellApp channel 健康并已准备 ghost。
5. `Cell::offloadEntity()` 调用 `onLeavingCell`。
6. `Entity::offload()` 向目标 CellApp 写 `onload` 消息。
7. `Entity::convertRealToGhost()` 写 real 数据、设置 `nextRealAddr_`、销毁 real part。
8. 目标 CellApp 读取 real 数据，ghost 转 real。
9. 旧 real 通知其他 ghosts 下一个 real 地址。
10. Ghost 播放 buffered messages。

关键源码：

- `Cell::checkOffloadsAndGhosts()` 创建 `OffloadChecker`，见 [cell.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/cell.cpp:157)。
- `OffloadChecker::run()` 遍历 real entities 并发送 offloads，见 [offload_checker.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/offload_checker.cpp:43)。
- `EntityGhostMaintainer::checkEntityForOffload()` 依据 `Space::pCellAt()` 和目标 Channel 状态判断是否迁移，见 [entity_ghost_maintainer.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/entity_ghost_maintainer.cpp:81)。
- `Cell::offloadEntity()` 调用 `onLeavingCell` 并把 real 变 ghost，见 [cell.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/cell.cpp:183)。
- `Entity::offload()` 写 `CellAppInterface::onload` 并调用 `convertRealToGhost()`，见 [entity.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/entity.cpp:1968)。
- `Entity::readRealDataFromStreamForOnloadInternal()` 按 offload 相同顺序读 real data，见 [entity.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/entity.cpp:4723)。

## Offload 触发条件

实体不是一跨边界就立即迁移。`EntityGhostMaintainer::checkEntityForOffload()` 会检查：

- `Space::pCellAt(position.x, position.z)` 找到 home cell。
- home cell 不能是当前 cell。
- home cell 不能处于 delete pending。
- 目标 `CellAppChannel` 必须存在。
- 目标 channel 必须 `isGood()`。

源码见 [entity_ghost_maintainer.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/entity_ghost_maintainer.cpp:83)。

这避免把实体迁移到不可用或正在删除的 Cell。

## Offload 状态机

<MermaidDiagram title="实体 Offload 状态机">
stateDiagram-v2
  [*] --> RealOnSource
  RealOnSource --> GhostPreparedOnDest: create/unmark haunt
  GhostPreparedOnDest --> OffloadQueued: addToOffloads
  OffloadQueued --> LeavingSource: onLeavingCell
  LeavingSource --> StreamingToDest: write onload + real data
  StreamingToDest --> GhostOnSource: convertRealToGhost
  StreamingToDest --> RealOnDest: onload + read real data
  GhostOnSource --> AwaitGhostSetReal
  AwaitGhostSetReal --> GhostFollowingNewReal: ghostSetNextReal
  RealOnDest --> [*]
</MermaidDiagram>

这不是单步操作，而是跨进程、多消息、多对象状态转换。

## 写入顺序与读取顺序

`Entity::convertRealToGhost()` 中，如果有目标 channel：

- 写 real data 到 stream。
- 设置 `pRealChannel_`。
- 设置 `nextRealAddr_`。
- 调用 `offloadReal()` 删除 real part。
- 删除 real-only properties，只留下 ghost + real shared 属性。

源码见 [entity.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/entity.cpp:2005)。

目标端读取时，`readRealDataFromStreamForOnloadInternal()` 注释明确要求按照 offload 相同顺序读取：

- 先 Entity。
- 再 script 相关数据。
- 再 real 数据。

源码见 [entity.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/entity.cpp:4723)。

这说明 wire format 和状态转换强耦合，不能随意调整字段顺序。

## 脚本回调顺序

Cell offload 会触发脚本回调：

- 源 Cell：`onLeavingCell`。
- 源 Cell 转为 ghost 后：`onLeftCell`。
- 目标 Cell：`onEnteringCell`。
- 目标 ghost 转 real 后：`onEnteredCell`。

源码注释见 [entity.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/entity.cpp:831)。

文档里必须强调源码注释中的提醒：这些回调“Think twice before using”，很多情况应该用更好的数据类型避免。

原因是回调能执行脚本，脚本可能销毁、teleport、再次 offload 或修改状态。

## Channel 与 nextRealAddr

`Entity::convertRealToGhost()` 会把 `nextRealAddr_` 设置成目标 CellApp 地址，见 [entity.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/entity.cpp:2028)。

`RealEntity::destroy()` 在 offload 时会通知所有非目标 ghosts：

- 发送 `ghostSetNextReal`。
- 告诉 ghost 新 real 的地址。
- reset/destroy 当前 real channel。

源码见 [real_entity.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/real_entity.cpp:246)。

`Entity::ghostSetNextReal()` 设置 `nextRealAddr_` 后，会播放 buffered messages，见 [entity.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/entity.cpp:4868)。

这正是 Mercury indexed channel version 的业务背景：offload 后旧包、新包、缓冲包可能交错到达，必须识别消息来源和版本。

## Offload 与 Ghost 的关系

`OffloadChecker::sendOffload()` 注释说，单个 offload 受 ghosting capacity 和 channel 状态约束，见 [offload_checker.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/offload_checker.cpp:102)。

虽然当前函数实现只是调用 `cell_.offloadEntity()`，但前置的 `EntityGhostMaintainer` 已经保证目标条件。

换句话说：

- Ghost 不是 offload 的附属品。
- Ghost 是 offload 安全执行的前置条件之一。

## Cell 删除边界

Cell 删除也依赖 offload 完成。

`Cell::isReadyForDeletion()` 要求：

- buffered entity messages 为空。
- buffered input messages 为空。
- `isRemoved_` 为 true。
- real entities 为空。
- space entities 为空。
- pending ACK 为空。

源码见 [cell.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/cell.cpp:140)。

这说明 BigWorld 不会在迁移消息未清空时粗暴删除 Cell。

## 与 Base Offload 的区别

本章主要讲 Cell entity offload。BaseApp 也有 Base offload，用于 BaseApp retiring、备份和恢复。

Base 侧有：

- `Base::backupTo()`
- `Base::writeBackupData()`
- `Base::offload()`
- `Base::readBackupData()`

见 [base.hpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/baseapp/base.hpp:109)。

两者区别：

- Cell offload 是空间权威迁移。
- Base offload 是会话/实体长期部分在 BaseApp 间迁移。
- 两者都依赖 Mercury、BinaryStream、EntityDef 和 Mailbox。

## 当时取舍

BigWorld 的 offload 机制复杂，但它带来关键能力：

- 大世界无需固定每个区域进程。
- 玩家移动跨 Cell 不需要断线。
- 负载均衡可以在线调整。
- Ghost 让边界 AOI 连续。
- Channel version 和 buffered messages 处理乱序。

代价：

- 状态机很难测试。
- Wire format 顺序强耦合。
- 脚本回调会放大复杂度。
- offload 期间失败恢复困难。
- 迁移成本可能造成瞬时负载尖峰。

## 现代对比

<div class="decision-table">

| 方案 | 优点 | 代价 | 对 BigWorld 的判断 |
| --- | --- | --- | --- |
| BigWorld offload | 在线迁移实体，边界连续 | 状态机复杂 | 核心能力 |
| 固定区域不迁移 | 简单 | 边界和热点问题明显 | 不适合开放世界 MMO |
| Actor handoff | 边界清晰 | 仍需 AOI/Ghost 设计 | 可借鉴状态机表达 |
| Snapshot + restore | 容错好 | 延迟和一致性成本 | 适合低频迁移 |
| Lockstep 分区 | 一致性强 | 延迟高，不适合大 MMO | 不匹配 BigWorld |

</div>

## 现代化建议

1. 把 offload 状态机显式化，增加状态枚举和 tracing。
2. 为 onload/offload stream 建立 schema/golden tests。
3. 给 `nextRealAddr_`、Channel version、buffered messages 建立可视化。
4. 脚本回调中禁止或警告高风险操作，例如再次 offload 或 destroy。
5. 用 fake clock 和 packet reordering 测试 offload 乱序。
6. 对迁移成本做预算，负载均衡时纳入迁移开销。

## 本章边界

本章解释 Cell entity offload。下一章分析持久化与 DB 线程模型，解释实体状态如何落库、备份和恢复。
