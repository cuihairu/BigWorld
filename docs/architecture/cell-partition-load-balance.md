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

## 为什么是几何边界，而不是请求分发

MMO 空间状态是有位置的。负载均衡必须回答：

- 哪个 CellApp 对某个坐标区域拥有权威。
- 实体移动到边界时是否需要迁移。
- Ghost 需要在哪些邻接 Cell 上创建。
- AOI 跨边界时哪些实体应该可见。

这不是 Nginx 或 service mesh 能解决的问题。BigWorld 的负载均衡调整的是空间权威归属。

## CellApp 侧 Space

CellApp 侧 `Space` 管理本地实际运行数据：

- `pCell_`：本 CellApp 在此 Space 中拥有的 Cell。
- `entities_`：本 Space 内实体集合。
- `cellInfos_`：其他 Cell 的信息。
- `rangeList_`：空间范围查询和 AOI 支撑结构。
- `spaceDataMapping_`：SpaceData。
- `pCellInfoTree_`：从控制面同步来的 CellInfo 树。
- `pPhysicalSpace_`：物理空间。

源码见 [space.hpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/space.hpp:201)。

它提供 `pCellAt(x,z)` 用于判断位置属于哪个 Cell，见 [space.hpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/space.hpp:57)。

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

## 当时取舍

动态 BSP Cell 分区的优点：

- 能按真实负载调整区域大小。
- 稀疏区域可以大 Cell，热点区域可以拆小。
- 和实体 offload / Ghost / AOI 紧密配合。
- 不要求固定网格预设。

代价：

- 分区树和 geometry 更新复杂。
- 迁移成本可能抵消负载均衡收益。
- Cell 边界抖动会造成 ghost/offload 压力。
- 需要防止目标 Cell 未加载几何或正在删除。
- 测试和可视化要求高。

## 现代对比

<div class="decision-table">

| 方案 | 优点 | 代价 | 对 BigWorld 的判断 |
| --- | --- | --- | --- |
| 动态 BSP Cell | 自适应负载，贴合开放世界 | 实现复杂 | 当前核心 |
| 固定网格 | 简单、易分片 | 热点区域难处理 | 可作为新项目基线，不适合直接替换 |
| QuadTree/Octree | 空间语义自然 | 动态迁移仍复杂 | 可对比 BSP |
| Actor shard | 状态边界清晰 | 空间连续性需额外处理 | 长期架构方向 |
| 云 HPA | 资源层成熟 | 不懂空间状态 | 只能配合 CellAppMgr 使用 |

</div>

## 现代化建议

1. 给 BSP 树、Cell range、loadSafetyBound 做可视化。
2. 记录每次 balance 前后 Cell 面积、负载、实体数、迁移数。
3. 给 Cell 边界移动设置冷却和 hysteresis，避免抖动。
4. 把 `shouldOffload`、retire、remove 状态纳入运维面板。
5. 自动扩容要同时考虑 spare time、AOI、迁移成本和 DB 压力。
6. 引入 Kubernetes 时只管理 CellApp 进程生命周期，不替代 Cell 分区控制。

## 本章边界

本章解释 Cell 分区和负载均衡。下一章深入实体迁移/offload 的消息和状态顺序。
