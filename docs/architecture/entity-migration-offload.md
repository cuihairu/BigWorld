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

## Ghost 维护顺序

`OffloadChecker::run()` 不是只检查是否跨边界。它在 Space 没有 shutdown 时遍历本 Cell 的 `realEntities()`，每个 real entity 交给 `EntityGhostMaintainer::check()`，最后统一 `sendOffloads()`，见 [offload_checker.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/offload_checker.cpp:43)。

`EntityGhostMaintainer::check()` 的顺序很关键：

1. 如果 `Cell::shouldOffload()` 为 true，先执行 `checkEntityForOffload()`，可能把 `pOffloadDestination_` 填好并加入 offload list。
2. `markHaunts()` 把当前 real entity 的所有 haunts 标记为待检查，同时记录目标 offload Cell 是否已有 ghost。
3. `createOrUnmarkRequiredHaunts()` 按 `ghostDistance + appealRadius` 构造 interest area，遍历 `Space::visitRect()` 命中的 Cell。
4. `visit()` 对已有 haunt 清 mark；对新需要的 Cell 调 `addHaunt()` 和 `Entity::createGhost()`。
5. 如果正在 offload，只允许在目标 Cell 上创建 ghost，避免给非目标 Cell 创建多余 channel。
6. `deleteMarkedHaunts()` 删除仍被 mark 的过期 ghosts，但受 `maxGhostsToDelete()`、new real 保留时间和 `minGhostLifespanInTicks()` 限制。

源码见 [entity_ghost_maintainer.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/entity_ghost_maintainer.cpp:46)、[entity_ghost_maintainer.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/entity_ghost_maintainer.cpp:184) 和 [entity_ghost_maintainer.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/entity_ghost_maintainer.cpp:248)。

所以“目标 CellApp 已准备 ghost”不是口头约束：源码用 `doesOffloadDestinationHaveGhost || numGhostsCreated_ == 1` 断言目标 ghost 已存在或本轮刚创建，见 [entity_ghost_maintainer.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/entity_ghost_maintainer.cpp:60)。

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

实际写流发生在 `Entity::writeRealDataToStream()` 和 `Entity::writeRealDataToStreamInternal()`：

1. 先写 `EntityID`。
2. 再写 `teleportFailure = false`。
3. 用 `internalNetworkCompressionType()` 包一层压缩流。
4. 写 real-only properties，也就是 `propCountGhost()` 到 `propCountGhostPlusReal()` 之间的属性。
5. 写 token `RealProps`。
6. 调 `RealEntity::writeOffloadData()` 写 RealEntity 状态。
7. 写 exposed-for-replay 的 base properties。

源码见 [entity.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/entity.cpp:2068) 和 [entity.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/entity.cpp:2090)。

`RealEntity::writeOffloadData()` 继续写：

- channel 状态。
- velocity、topSpeed、topSpeedY、physicsCorrections。
- `shouldAutoBackup_`。
- 当前 `spaceID` 和 `isTeleport`。
- haunt 地址列表，并且如果当前 CellApp 地址不在 haunt 里，会强制追加当前地址，因为源端 offload 后会变成 ghost。
- `controlledBy_`、profiler、real controllers、`periodsWithoutWitness_`、`recordingSpaceEntryID_`。
- 如果有 Witness，写标记 `'W'` 并写 witness offload data；否则写 `'-'`。

源码见 [real_entity.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/real_entity.cpp:487)。

目标端读取时，`readRealDataFromStreamForOnloadInternal()` 注释明确要求按照 offload 相同顺序读取：

- 先 Entity。
- 再 script 相关数据。
- 再 real 数据。

源码见 [entity.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/entity.cpp:4723)。

目标端具体顺序：

1. `Entity::onload()` 从 `GHOST_ONLY` 的 raw varlen 消息里读 `teleportFailure`，先触发 `onEnteringCell`，再调用 `convertGhostToReal()`，见 [entity.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/entity.cpp:4560)。
2. `convertGhostToReal()` 禁止普通回调，清掉指向旧 real 的 `pRealChannel_`，再 `readRealDataFromStreamForOnload()`，见 [entity.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/entity.cpp:4657)。
3. `readRealDataFromStreamForOnloadInternal()` 先补齐 real-only properties，校验 `RealProps` token，再 `createReal()` 并调用 `RealEntity::init(..., CREATE_REAL_FROM_OFFLOAD, ...)`，见 [entity.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/entity.cpp:4730)。
4. 读取 exposed-for-replay base properties 后，启动 real controllers，见 [entity.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/entity.cpp:4762)。
5. `convertGhostToReal()` 把实体加入 Cell 的 real 列表，调用 `relocated()`，再用 high-priority callback buffer 触发 `onEnteredCell`，见 [entity.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/entity.cpp:4676)。

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

目标端 `RealEntity::readOffloadData()` 读取 haunt 地址列表后，会对每个有效 ghost 发送 `ghostSetReal`，告诉它新的 owner 地址和 `numTimesRealOffloaded`，见 [real_entity.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/real_entity.cpp:296)。

源端和其他 ghost 的握手顺序是：

1. 源 real 在 `convertRealToGhost()` 中设置 `nextRealAddr_`，然后 `offloadReal()`。
2. `RealEntity::destroy( pNextRealAddr )` 给所有非目标 haunt 发送 `ghostSetNextReal`，这是当前 real 发给这些 ghosts 的最后一条消息。
3. 目标 real 创建后，`readOffloadData()` 给保留的 ghosts 发送 `ghostSetReal`。
4. ghost 收到 `ghostSetNextReal` 后，只接受来自 `nextRealAddr_` 的后续 GHOST_ONLY 消息，并播放该地址对应的 buffered subsequence。
5. ghost 收到 `ghostSetReal` 时，如果 `numTimesRealOffloaded` 不是期望值，会把这条消息作为新 subsequence 延迟，等前序 offload 完成。

源码见 [real_entity.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/real_entity.cpp:246)、[entity.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/entity.cpp:4831) 和 [entity.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/entity.cpp:4868)。

这正是 Mercury indexed channel version 的业务背景：offload 后旧 real、新 real、缓冲包可能交错到达，必须用 owner 地址、offload 次数和子序列边界识别消息来源。

## Buffered ghost messages

`BufferedGhostMessages` 按 `EntityID` 管理缓冲，再按来源 `Mercury::Address` 分 queue：

- `BufferedGhostMessages::add()` 把消息放进某实体某来源队列，见 [buffered_ghost_messages.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/buffered_ghost_messages.cpp:11)。
- `playSubsequenceFor(entityID, srcAddr)` 只播放当前应该接收的来源地址队列，见 [buffered_ghost_messages.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/buffered_ghost_messages.cpp:22)。
- `BufferedGhostMessagesForEntity::playSubsequence()` 如果找不到当前来源队列，会保留其它来源队列，不会错误播放，见 [buffered_ghost_messages_for_entity.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/buffered_ghost_messages_for_entity.cpp:59)。
- `BufferedGhostMessageQueue::playSubsequence()` 会播放到下一个 subsequence end 为止；如果播放到 `ghostSetNextReal`，可能递归触发下一来源队列，见 [buffered_ghost_message_queue.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/buffered_ghost_message_queue.cpp:144)。
- `delaySubsequence()` 要求第一条消息是 subsequence start，并把旧的 unexpected subsequence 一起延迟，见 [buffered_ghost_message_queue.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/buffered_ghost_message_queue.cpp:173)。

`BufferedGhostMessages::isSubsequenceStart()` 把 `ghostSetReal` 视为子序列开始，`isSubsequenceEnd()` 把 `ghostSetNextReal` 和 `delGhost` 视为子序列结束，见 [buffered_ghost_messages.hpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/buffered_ghost_messages.hpp:58)。

`Space::createGhost()` 也参与乱序处理：如果同 ID 实体已经存在，或该实体从同来源已有缓冲消息，就把 `createGhost` 包装成 `BufferedCreateGhostMessage`，而不是直接创建或覆盖，见 [space.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/space.cpp:228)。

消息入口处还有一层保护：

- `EntityMessageHandler::handleMessage()` 对 `GHOST_ONLY` 消息，如果实体不存在、`shouldBufferMessagesFrom(srcAddr)` 为 true、或同来源正在 delay，就创建 buffered message，见 [message_handlers.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/message_handlers.cpp:117)。
- 如果 `GHOST_ONLY` 消息明显发给已销毁 ghost，例如当前实体已经是 real、或当前实体正在 offload 到该来源但消息不是 subsequence start，会丢弃并回失败，见 [message_handlers.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/message_handlers.cpp:126)。
- `REAL_ONLY` 消息到达时，如果本进程没有 real，会查 ghost 上缓存的 real channel 或 population 的 real channel 并转发，见 [message_handlers.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/message_handlers.cpp:183)。
- `Entity::shouldBufferMessagesFrom()` 的规则很直接：real 和 zombie ghost 不缓冲；如果设置了 `nextRealAddr_`，只接受 next real；否则只接受当前 real，见 [entity.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/entity.cpp:7730)。

这几层共同保证：旧 real 的尾包、新 real 的首包、ghost 创建包、ghost 删除包可以乱序到达，但不会直接按到达顺序破坏实体生命周期。

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

## Teleport 分支

普通 offload 是相邻 Cell 之间的权威迁移；teleport 允许跨空间或跨远端 CellApp，因此源码走了单独消息 `CellAppInterface::onloadTeleportedEntity`，见 [cellapp_interface.hpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/cellapp_interface.hpp:143)。

源端 `RealEntity::teleport()` 的关键差异：

- 如果 mailbox id 为 0，只在本地改位置并调用 `onTeleportSuccess(NULL)`，不走 offload。
- 远端 teleport 会先调用 `onLeavingCell`，再向目标 CellApp 写 `onloadTeleportedEntity`。
- 该消息先携带 nearby entity id，再携带 createGhost 数据长度和 ghost 数据。
- 源端会调用 `Cell::offloadEntity(..., isTeleport=true)`，此时 `Cell::offloadEntity()` 不再重复调用 `onLeavingCell`。
- 源端临时改 local position/direction 写入 offload 数据，发送后恢复旧位置，避免源端 ghost 状态被错误位置污染。

源码见 [real_entity.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/real_entity.cpp:1169) 和 [real_entity.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/real_entity.cpp:1283)。

目标端 `CellApp::onloadTeleportedEntity()` 会把一条复合消息拆成两条内部消息：

1. 用 nearby entity 的 space id 补齐 createGhost stream。
2. 调 `CellAppInterface::createGhost` handler，先创建 ghost。
3. 调 `CellAppInterface::onload` handler，把 ghost 转 real。
4. 找到新实体后调用 `onTeleportSuccess( nearbyEntity )`。

如果 nearby entity 不存在，目标端丢弃 createGhost 数据，并把剩余 onload 数据回送给源端，设置 `teleportFailure=true`。源端 `Entity::onload()` 看到 teleport failure 后会移除失败目的地 haunt，并调用 `onTeleportFailure`，见 [cellapp.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/cellapp.cpp:2037) 和 [entity.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/entity.cpp:4618)。

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

## 源码取舍

BigWorld 的 offload 机制复杂，但源码里的复杂性主要来自这些约束：

- Real/Ghost 是同一 Entity 在不同 CellApp 上的不同权威状态，不能直接 memcpy。
- 目标端必须先有 ghost，才能把 ghost 转 real；否则 real-only 属性、controller、witness、history 和 channel 都缺上下文。
- Wire format 和读写顺序强耦合，`RealProps` token 只是开发期校验，不是 schema 演进机制。
- 脚本回调被插在迁移关键点，源码必须用 `callbacksPermitted(false)` 和 high-priority callback buffering 缩小破坏窗口。
- Ghost 消息只能保证单个 CellApp channel 内有序，跨旧 real 和新 real 的消息要靠 subsequence 拼接。
- Cell 删除必须等 buffered messages、real entities、space entities 和 removal ACK 全部清空。

这套设计换来的是在线边界迁移、AoI 连续和 CellApp 负载均衡，但代价是状态机难以局部推理。阅读或修改源码时，应优先保护消息顺序、回调顺序和 Real/Ghost 生命周期边界，不要先抽象成“对象迁移”。

## 源码验证重点

1. 在普通 offload 场景确认源端顺序：`onLeavingCell` -> `Cell::realEntities_.remove()` -> `Entity::offload()` -> `offloadReal()` -> `onLeftCell`。
2. 检查目标端 `onload` 消息必须打到 ghost：`CellAppInterface::onload` 是 `GHOST_ONLY`，不是普通 CellApp RPC。
3. 给 `writeRealDataToStreamInternal()` 和 `readRealDataFromStreamForOnloadInternal()` 加 token/golden stream 测试，确保 real-only 属性顺序不变。
4. 构造目标 ghost 不存在的情况，确认 `EntityGhostMaintainer::check()` 会在 offload 前创建目标 ghost，或触发断言。
5. 构造 `ghostSetReal` 先于 `ghostSetNextReal` 到达，确认 `Entity::ghostSetReal()` 会 delay subsequence，而不是立刻切换 real channel。
6. 构造 `createGhost` 到达时实体仍存在，确认 `Space::createGhost()` 会进入 `BufferedCreateGhostMessage`，不会覆盖实体。
7. 构造 teleport nearby entity 不存在，确认目标端回送 `teleportFailure=true`，源端触发 `onTeleportFailure`。
8. 删除 Cell 时确认 `isReadyForDeletion()` 的 buffered entity messages、buffered input messages、real entities、space entities、pending ACK 全部为空。

## 本章边界

本章解释 Cell entity offload。下一章分析持久化与 DB 线程模型，解释实体状态如何落库、备份和恢复。
