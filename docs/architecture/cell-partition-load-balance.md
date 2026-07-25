# Cell 分区与负载均衡

<div class="arch-hero">

BigWorld 的负载均衡不是把请求分给后端服务器，而是调整空间中 Cell 的几何边界，让实体权威 real 所属 CellApp 发生变化。它本质是空间分区控制面，而不是 L4/L7 流量负载均衡。

</div>

## 先给结论

CellAppMgr 管理全局空间分区，CellApp 管理本地空间实体。两边都有 `Space` 类，但职责不同：

- `server/cellappmgr/Space`：控制面 Space，维护 `CellData`、BSP 树、负载、几何范围、Cell 增删。
- `server/cellapp/Space`：运行时 Space，维护本地实体、CellInfo tree、RangeList、SpaceData、物理空间和恢复数据。

关键源码：

- CellAppMgr 侧 `Space` 有 `addCell()`、`loadBalance()`、`updateRanges()`、`findCell()`，见 [space.hpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellappmgr/space.hpp:22)。
- CellAppMgr 侧 `CellData` 继承 `CM::BSPNode`，表示当前运行的 Cell，见 [cell_data.hpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellappmgr/cell_data.hpp:17)。
- `Space::loadBalance()` 更新 BSP load，计算 safety bound，调用 `balance()`，再通知 CellApps geometry，见 [space.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellappmgr/space.cpp:201)。
- CellApp 侧 `Space` 管理 `pCellAt()`、RangeList、space data、entities 和 recovery data，见 [space.hpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/space.hpp:46)。
- `Cell::checkOffloadsAndGhosts()` 创建 `OffloadChecker`，见 [cell.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/cell.cpp:157)。

## 控制面 Space

CellAppMgr 的 `Space` 负责决定世界如何切成 Cell：

- `cells_`：当前 Space 的 CellData 集合。
- `pRoot_`：BSP 分区树。
- `preferredIP_`：倾向使用的机器 IP。
- `spaceBounds_`：空间边界。
- `artificialMinLoad_`：人工最小负载。
- `isBalancing_`：避免递归 balance。

源码见 [space.hpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellappmgr/space.hpp:129)。

这不是业务 Space，而是全局分区控制器。

## CellData 与 BSP

`CellData` 继承 `CM::BSPNode`。这意味着 Cell 本身是 BSP 树叶子节点，同时参与：

- `updateLoad()`
- `avgLoad()`
- `balance()`
- `updateRanges()`
- `addToStream()`
- `addCellTo()`
- `removeCell()`

源码见 [cell_data.hpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellappmgr/cell_data.hpp:21)。

这说明 BigWorld 的空间划分不是固定网格，而是可动态调整的 BSP 分区。

## 负载从哪里来

CellApp 侧每个游戏 tick 都会更新负载并上报给 CellAppMgr：

- `CellApp::updateLoad()` 用上一 tick 时间、spare time、transient load、throttle 估算 persistent load，见 [cellapp.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/cellapp.cpp:1177)。
- `CellApp::handleGameTickTimeSlice()` 先 `updateLoad()`，再 `cellAppMgr_.informOfLoad( persistentLoad_ )`，再 `updateBoundary()` 上报实体边界和 chunk bounds，见 [cellapp.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/cellapp.cpp:1302)。
- CellAppMgr 侧 `CellApp::informOfLoad()` 记录 `lastReceivedLoad_`，叠加 Space 的 artificial min load，生成 `currLoad_`，再用 `loadSmoothingBias()` 更新 `smoothedLoad_`，见 [cellapp.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellappmgr/cellapp.cpp:72)。
- `CellData::updateLoad()` 自己不重新计算 CPU，而是读取关联 `CellApp::currLoad()`，同时计算 `areaNotLoaded_`，见 [cell_data.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellappmgr/cell_data.cpp:539)。
- `CellData::avgSmoothedLoad()`、`totalSmoothedLoad()`、`minLoad()`、`maxLoad()` 都返回关联 CellApp 的 smoothed load，见 [cell_data.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellappmgr/cell_data.cpp:568)。

所以这里的“Cell 负载”源码上更准确地说是“承载该 Cell 的 CellApp 负载”。当一个 CellApp 承载多个 Cell 时，`CellData` 的叶子负载不是独立 per-cell CPU 采样，而是 CellApp 级负载在 BSP 树上的投影。这也是为什么 CellAppMgr 还需要实体边界层级和 chunk bounds 来限制分割线移动，不能只按叶子 CPU 数值移动。

`CellAppLoadConfig` 的几个阈值直接参与这个流程：

- `lowerBound`：组级低负载收缩阈值，低于它时可以 retire CellApp 上的 Cell。
- `safetyBound`：基础安全上限。
- `safetyRatio`：用当前空间平均 smoothed load 推高安全上限，`loadSafetyBound = max(safetyBound, avgSmoothedLoad * safetyRatio)`。
- `warningLevel`：过载告警阈值，不是分割线移动的直接输入。

源码见 [cell_app_load_config.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellappmgr/cell_app_load_config.cpp:14)。

## loadBalance 流程

`Space::loadBalance()` 做的事情：

1. 防止递归调用。
2. 检查 BSP root 是否存在。
3. `pRoot_->updateLoad()`。
4. 构造无限大初始 rect。
5. 计算 `loadSafetyBound`，避免新 Cell 加入时让过载更严重。
6. 记录 balance 前是否已加载 required chunks。
7. 调用 `pRoot_->balance(rect, loadSafetyBound)`。
8. 再次更新 load。
9. 如果 balance 后反而需要加载 chunk，记录错误。
10. 延迟向所有 CellApps 发送 geometry。
11. 删除不再包含的 cells。
12. 检查 cells 是否加载 mapped geometry。

源码见 [space.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellappmgr/space.cpp:201)。

<MermaidDiagram title="CellAppMgr 负载均衡流程">
flowchart TD
  A[loadBalance timer] --> B[Space loadBalance]
  B --> C[pRoot updateLoad]
  C --> D[calculate loadSafetyBound]
  D --> E[pRoot balance]
  E --> F[pRoot updateLoad]
  F --> G[inform CellApps geometry]
  G --> H[remove obsolete cells]
  H --> I[check geometry loaded]
</MermaidDiagram>

## BSP 分割线如何移动

`CM::InternalNode` 表示一条水平或垂直分割线，叶子才是 `CellData`。每轮 balance 不是重建整棵树，而是在现有 BSP 上递归移动 `position_`：

- `InternalNode::updateLoad()` 汇总左右子树的 total load、smoothed load、retiring 数、未加载面积、min/max load，并合并实体边界层级和 chunk bounds，见 [internal_node.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellappmgr/internal_node.cpp:261)。
- `InternalNode::doBalance()` 如果没有 retiring，先比较左右子树 `avgLoad()` 相对当前节点平均值的偏差；左侧更重就向左移动分割线，右侧更重就向右移动分割线，见 [internal_node.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellappmgr/internal_node.cpp:389)。
- 如果任一子树 retiring，则方向直接由 retiring 子树决定：左子树 retiring 就让左侧继续缩小，右子树 retiring 就让右侧继续缩小，见 [internal_node.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellappmgr/internal_node.cpp:403)。
- 真正移动前必须满足三个条件：方向不是 `BALANCE_NONE`、左右子树都 `hasBeenCreated()`、被扩大的来源侧 `maxLoad()` 小于 `loadSafetyBound`，见 [internal_node.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellappmgr/internal_node.cpp:408)。
- 移动上限取 `entityLimitInDirection()` 和 `chunkLimitInDirection()` 的 closest limit。前者来自实体边界层级，后者来自 chunk bounds 与 `ghostDistance`，见 [internal_node.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellappmgr/internal_node.cpp:418)。
- 如果方向反转，`adjustAggression()` 降低 aggression；如果持续同向或不动，则逐步提高 aggression，见 [internal_node.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellappmgr/internal_node.cpp:445)。

这段源码说明 BigWorld 的 balance 是带安全阈值、实体迁移量限制、chunk 加载限制和抖动抑制的边界移动算法，不是“左边 CPU 高就直接把一半区域切给右边”。

## 实体边界与 chunk 边界

分割线移动有两个硬约束：

- 实体边界约束：`entityBoundLevels().entityBoundForLoadDiff()` 根据希望卸载的 CPU 量选择分割线最多能移动到哪里，见 [internal_node.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellappmgr/internal_node.cpp:569)。
- chunk 约束：`chunkLimitInDirection()` 从被扩大的子树 `balanceChunkBounds()` 取边界，并加减 `CellAppMgrConfig::ghostDistance()`，见 [internal_node.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellappmgr/internal_node.cpp:595)。

`CellData::calculateAreaNotLoaded()` 会把当前 range 按 `ghostDistance - 1` 膨胀，再和 `spaceBounds_`、`chunkBounds_` 求交，计算 required area 中还没有加载的面积，见 [cell_data.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellappmgr/cell_data.cpp:428)。

`CellData::balanceChunkBounds()` 也会按同样的 ghost 距离膨胀 desired rect：如果当前 chunk bounds 已覆盖 desired rect，就返回 chunk bounds；否则退回 range，见 [cell_data.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellappmgr/cell_data.cpp:685)。

这就是 `ghostDistance` 同时影响 AoI/Ghost 和负载均衡的原因：Cell 边界附近必须预先有 ghost 支撑和 chunk 资源，否则实体刚被 offload 到新 Cell，就可能进入几何未加载或邻居不可见的区域。

## 创建、分裂与删除

新增 Cell 的源码链路：

1. `Space::addCell()` 选择 CellApp，或 `Space::addCellTo()` 针对指定 `CellData` 分裂，见 [space.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellappmgr/space.cpp:436)。
2. `Space::addCell( CellApp&, CellData* )` 创建 `CellData`，插入 BSP；如果传了 `pCellToSplit`，调用 `pRoot_->addCellTo()`，否则调用 `pRoot_->addCell()`，见 [space.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellappmgr/space.cpp:368)。
3. `CellData::addCell()` 用当前 range 的最大边界作为新分割点，新 Cell 初始 range 是零宽或零高，然后返回新的 `InternalNode`，见 [cell_data.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellappmgr/cell_data.cpp:355)。
4. 控制面随后向目标 CellApp 发送 `CellAppInterface::addCell` 请求，并把整棵 BSP tree stream 过去，见 [space.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellappmgr/space.cpp:406)。
5. `AddCellReplyHandler` 收到 CellApp 回复后，把对应 `CellData::hasBeenCreated` 置为 true，见 [space.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellappmgr/space.cpp:32)。

删除不是直接从树上摘掉：

- `CellData::balance()` 更新 range，并把 `isOverloaded_` 设置为 smoothed load 是否超过 `loadSafetyBound`，见 [cell_data.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellappmgr/cell_data.cpp:618)。
- 当 Cell 没有面积、正在 shrink 或 retiring、且实体数为 0 时，才加入 `Space::cellsToDelete_`，见 [cell_data.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellappmgr/cell_data.cpp:627)。
- `Space::loadBalance()` 先下发最新 geometry，再遍历 `cellsToDelete_` 调 `removeSelf()`，确保被删 Cell 也知道最终布局，见 [space.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellappmgr/space.cpp:243)。
- `Space::eraseCell()` 从 `cells_` 移除，通知其他 CellApps，并在 BSP 上 `removeCell()`，见 [space.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellappmgr/space.cpp:653)。

这个顺序很关键：如果先删除再下发布局，运行时 CellApp 可能不知道边界已经收缩，也就无法正确 offload 剩余 real entity。

## 为什么是几何边界，而不是请求分发

MMO 空间状态是有位置的。负载均衡必须回答：

- 哪个 CellApp 对某个坐标区域拥有权威。
- 实体移动到边界时是否需要迁移。
- Ghost 需要在哪些邻接 Cell 上创建。
- AOI 跨边界时哪些实体应该可见。

这不是 Nginx 或 service mesh 能解决的问题。BigWorld 的负载均衡调整的是空间权威归属。

## CellApp 侧 Space

CellApp 侧 `Space` 管理本地实际运行数据，并接收 CellAppMgr 下发的 BSP geometry：

- `pCell_`：本 CellApp 在此 Space 中拥有的 Cell。
- `entities_`：本 Space 内实体集合。
- `cellInfos_`：其他 Cell 的信息。
- `rangeList_`：空间范围查询和 AOI 支撑结构。
- `spaceDataMapping_`：SpaceData。
- `pCellInfoTree_`：从控制面同步来的 CellInfo 树。
- `pPhysicalSpace_`：物理空间。

源码见 [space.hpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/space.hpp:201)。

几何下发后的运行时链路：

1. CellApp 收到 `addCell` 时调用 `pSpace->updateGeometry( data )`，然后从 `cellInfos_` 里找到自己地址对应的 `CellInfo`，再创建本地 `Cell`，见 [cellapp.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/cellapp.cpp:1530)。
2. `Space::updateGeometry()` 先把旧 `CellInfo` 标记为 shouldDelete，删除旧 `pCellInfoTree_`，再用 `readTree()` 从 stream 重建 BSP，见 [space.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/space.cpp:881)。
3. 重建完成后，如果本地 Cell 仍存在且没有被删除，会立即 `pCell_->checkOffloadsAndGhosts()`，让实体按新边界迁移或补 ghost，见 [space.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/space.cpp:935)。
4. `Space::readTree()` 按 type 读取 branch/leaf；leaf 里按 CellApp 地址复用或创建 `CellInfo`，并读取 load 与 `hasBeenCreated`，见 [space.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/space.cpp:2284)。
5. `Space::pCellAt(x,z)` 查询本地 `pCellInfoTree_`，见 [space.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/space.cpp:2259)。
6. `CellInfo::pCellAt()` 如果 `hasBeenCreated()` 为 false 会返回 NULL，明确阻止 offload 到尚未创建完成的 Cell，见 [cell_info.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/cell_info.cpp:66)。

因此 `hasBeenCreated` 在控制面和运行时两边都参与安全门控：CellAppMgr 不让未创建子树 shrink；CellApp 不把实体 offload 到未创建 leaf。

## 分区如何触发 offload 与 ghost

几何更新只改变“坐标属于哪个 Cell”的判断，真正迁移实体发生在 CellApp 的 offload 检查中：

- `CellApp::checkOffloads()` 按 `checkOffloadsPeriodInTicks()` 周期触发 `cells_.checkOffloads()`，见 [cellapp.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/cellapp.cpp:1271)。
- `Cell::checkOffloadsAndGhosts()` 创建 `OffloadChecker` 并执行 `run()`，见 [cell.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/cell.cpp:157)。
- `OffloadChecker::run()` 遍历本 Cell 的 real entities，为每个实体创建 `EntityGhostMaintainer` 检查 ghost/offload，然后统一 `sendOffloads()`，见 [offload_checker.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/offload_checker.cpp:41)。
- `EntityGhostMaintainer::checkEntityForOffload()` 用实体当前位置调用 `space().pCellAt(position.x, position.z)` 找 home Cell；如果目标是自己、目标未创建、目标 pending delete、或目标 channel 不可用，就不迁移，见 [entity_ghost_maintainer.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/entity_ghost_maintainer.cpp:81)。
- `createOrUnmarkRequiredHaunts()` 按 `ghostDistance + appealRadius` 构造 interestArea，再通过 `space().visitRect()` 找需要 ghost 的 Cell，见 [entity_ghost_maintainer.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/entity_ghost_maintainer.cpp:159)。
- 如果即将 offload，只在目标 Cell 上创建必要 ghost，避免给非目标 Cell 创建多余 channel，见 [entity_ghost_maintainer.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/entity_ghost_maintainer.cpp:210)。

所以分区 balance 的直接结果不是“控制面搬实体”，而是“控制面改变 BSP tree，运行时 CellApp 用新 tree 判断实体是否已经跨出本 Cell，再由 offload/ghost 机制执行迁移”。

<MermaidDiagram title="分区下发到实体迁移">
sequenceDiagram
  participant M as CellAppMgr Space
  participant A as CellApp Space
  participant C as Cell
  participant E as EntityGhostMaintainer
  M->>M: pRoot balance
  M->>A: updateGeometry BSP stream
  A->>A: readTree rebuild CellInfo tree
  A->>C: checkOffloadsAndGhosts
  C->>E: check real entities
  E->>A: pCellAt position
  E->>C: addToOffloads if target valid
  C->>A: offloadEntity via CellAppChannel
</MermaidDiagram>

## Cell offload 开关

`Cell::shouldOffload()` 同时检查本 Cell 开关和 CellApp 全局开关：

```cpp
return shouldOffload_ && CellApp::instance().shouldOffload();
```

源码见 [cell.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/cell.cpp:601)。

这说明控制面可以暂停 offload，避免在更新、恢复、关服、空间 shutdown 等敏感阶段继续迁移实体。

## Retire 与 Remove

Cell 可以进入 retiring/removing 状态：

- `Cell::retireCell()` 设置 `isRetiring_`，见 [cell.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/cell.cpp:628)。
- `Cell::removeCell()` 设置 `isRemoved_`，并等待相关 CellApps ack，见 [cell.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/cell.cpp:651)。
- `Cell::isReadyForDeletion()` 要求 buffered messages 为空、real entities 为空、space entities 为空、pending ACK 为空，见 [cell.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/cell.cpp:140)。

这说明 Cell 删除不是直接释放对象，而是等实体迁移、缓冲消息和 ACK 完成。

## 组级扩缩容

单个 Space 内的 `loadBalance()` 只能移动已有 Cell 的边界。如果要引入新 CellApp 或缩掉 CellApp，需要 `metaLoadBalance()`：

- `CellAppMgr::metaLoadBalance()` 先构造 `CellAppGroups`。这些 group 表示可以通过普通空间分区互相均衡负载的一组 CellApps，见 [cellappmgr.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellappmgr/cellappmgr.cpp:770)。
- 过载判断使用 `avgCellAppLoad() + metaLoadBalanceTolerance()` 作为 merge threshold，过载 group 会进入 `checkForOverloaded()`，见 [cellappmgr.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellappmgr/cellappmgr.cpp:775)。
- `CellAppGroups::checkForOverloaded()` 收集所有超过阈值的 group，并按负载从高到低调用 `addCell()`，见 [cell_app_groups.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellappmgr/cell_app_groups.cpp:66)。
- `CellAppGroup::addCell()` 如果有正在 retiring 的 CellApp，会先取消 retire；否则选择一个 Space 并调用 `Space::addCell()`，见 [cell_app_group.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellappmgr/cell_app_group.cpp:465)。
- 低负载时，`checkForUnderloaded()` 判断移除一个 CellApp 后 group 平均负载仍低于 `lowerBound`，就找 smoothed load 最低且不是 only-retiring 的 CellApp，调用 `retireAllCells()`，见 [cell_app_group.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellappmgr/cell_app_group.cpp:490)。

这说明扩容的基本单位仍然是“给某个 Space 添加 Cell”，不是把整个进程流量切过去；缩容也不是 kill 进程，而是先让它的 Cell retire、等实体迁移清空。

## 实现取舍

- 优点：能按真实热点动态调整区域大小，稀疏区域可以大 Cell，热点区域可以拆小 Cell。
- 优点：BSP tree、ghost、offload、chunk loading 是同一套空间语义，不需要额外把坐标路由同步到另一套系统。
- 代价：CellApp 级 load 投影到 CellData，精度不是纯 per-cell CPU，需要实体边界层级来限制迁移量。
- 代价：边界移动必须考虑 chunk bounds 和 ghostDistance，否则会把实体迁移到资源没准备好的区域。
- 代价：新增、删除、retire 都是异步状态机，`hasBeenCreated`、`isDeletePending`、`pendingAcks` 这些门控缺一不可。

## 源码验证重点

1. 检查 `CellApp::handleGameTickTimeSlice()` 中 `informOfLoad()` 是否在 `updateBoundary()` 前调用，确认负载和边界更新的时序。
2. 在 CellAppMgr watcher 上观察 `Space/bsp`、`areaNotLoaded`、`loadAvg/loadMin/loadMax`、`numRetiringCells`，对应 [space.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellappmgr/space.cpp:944)。
3. 观察 `CellData::addToStream()` 下发给 CellApp 的 leaf 是否包含 `hasBeenCreated`，否则运行时可能 offload 到尚未创建完成的 Cell。
4. 人为增大某个 CellApp 负载后，确认 `InternalNode::doBalance()` 是否因 `pFromNode->maxLoad() >= loadSafetyBound` 拒绝移动。
5. 在 chunk 未加载场景下，确认 `areaNotLoaded()` 非零时是否走 `balanceOnUnloadedChunks()` 或停止移动。
6. 在 retire 场景下，确认 retiring 子树持续 shrink，直到无面积且实体数为 0 后进入 `cellsToDelete_`。
7. 在运行时 CellApp 上观察 `cellInfos/*/hasBeenCreated`、`isDeletePending` 和 `rect`，确认 `Space::readTree()` 与控制面 BSP 一致。
8. 让实体跨越分割线，确认 `EntityGhostMaintainer::checkEntityForOffload()` 先通过 `pCellAt()` 找目标 Cell，再检查 pending delete 和 channel 状态。

这些验证点都可以从源码直接定位，不需要引入额外架构假设。

## 本章边界

本章解释 Cell 分区和负载均衡。下一章深入实体迁移/offload 的消息和状态顺序。
