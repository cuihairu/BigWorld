# 动态扩展与容灾

<div class="arch-hero">

BigWorld 的扩展不是现代 Kubernetes HPA 式“拉起无状态副本”，而是 MMO 状态分片下的运行时接纳、负载均衡、Cell/Entity 迁移和进程级恢复。理解它必须区分资源调度、状态迁移和故障恢复三个层面。

</div>

## 先给结论

BigWorld 支持运行时加入新的 `BaseApp` / `CellApp`，也有 `Reviver + machined` 的进程级恢复机制。但它不是自动云原生弹性系统。

关键源码：

- `CellAppMgr` 初始化时注册 load balance、meta load balance、overload check timer，见 [cellappmgr.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellappmgr/cellappmgr.cpp:190)。
- `CellAppMgr::addApp()` 是接纳新 CellApp 的入口，见 [cellappmgr.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellappmgr/cellappmgr.cpp:1245)。
- 新 CellApp 需要 BaseApp 和 Alpha DBApp 已知，否则暂不接纳，见 [cellappmgr.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellappmgr/cellappmgr.cpp:1252)。
- `CellAppMgr::startTimer()` 启动 GameTick 并创建 `TimeKeeper`，见 [cellappmgr.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellappmgr/cellappmgr.cpp:1774)。
- `CellApps::sendGameTime()` 向所有 CellApp 发送当前游戏时间，见 [cellapps.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellappmgr/cellapps.cpp:540)。
- `ComponentReviver` 注册 birth/death listener 并定期 ping 组件，见 [component_reviver.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/reviver/component_reviver.cpp:62)。
- `Reviver::revive()` 通过 `CreateMessage` 请求 machined 拉起进程，见 [reviver.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/reviver/reviver.cpp:440)。

## 三层扩展模型

BigWorld 的动态扩展可以分成三层：

<div class="decision-grid">
  <div class="decision-card">
    <h3>资源层</h3>
    <p>外部启动进程，进程向 machined 和 Manager 注册。</p>
  </div>
  <div class="decision-card">
    <h3>控制层</h3>
    <p>BaseAppMgr/CellAppMgr 接纳进程，分配 ID、同步共享数据、广播时间。</p>
  </div>
  <div class="decision-card">
    <h3>状态层</h3>
    <p>Cell 负载均衡、实体 offload、Ghost 维护、Base 迁移和恢复。</p>
  </div>
</div>

现代云平台主要解决资源层；BigWorld 的难点主要在状态层。

## CellApp 接纳流程

`CellAppMgr::addApp()` 会先判断是否允许接纳：

- 没有 BaseApp 地址时不接纳。
- 没有 Alpha DBApp 时不接纳。
- Evaluation build 只允许一个 CellApp。
- `allowNewCellApps_` 为 false 时不接纳。
- 正在 recovery 时不接纳。

源码见 [cellappmgr.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellappmgr/cellappmgr.cpp:1249)。

接纳成功后：

- `initData.id = ++lastCellAppID_`
- `initData.time = this->time()`
- 写入 BaseApp 地址、DBApp Alpha 地址、ready 状态、timeout period。
- `cellApps_.add()` 加入集合。
- 将 App 放入 `pendingApps_`。
- reply 返回 `CellAppInitData`。
- 下发 shared cell data 和 global data。

源码见 [cellappmgr.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellappmgr/cellappmgr.cpp:1306)。

<MermaidDiagram title="CellApp 动态加入">
sequenceDiagram
  participant CellApp
  participant CAM as CellAppMgr
  participant Base as BaseApp
  participant DB as DBApp Alpha

  CellApp->>CAM: addApp request
  CAM->>CAM: check BaseApp known
  CAM->>CAM: check DBApp Alpha known
  CAM->>CAM: check allowNewCellApps/recovery
  CAM->>CellApp: reply CellAppInitData
  CAM->>CellApp: setSharedData/globalData
  CAM->>CellApp: gameTime/info later
  CAM->>CAM: pendingApps add
</MermaidDiagram>

这说明 CellApp 不是随便启动就能承载实体，必须被控制面接纳并同步初始状态。

## 负载均衡与 GameTick

`CellAppMgr::init()` 注册三个定时器：

- `LoadBalance`
- `MetaLoadBalance`
- `OverloadCheck`

源码见 [cellappmgr.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellappmgr/cellappmgr.cpp:190)。

`CellAppMgr::startTimer()` 使用 `Config::updateHertz()` 启动 `GameTick`，并创建 `TimeKeeper`。源码见 [cellappmgr.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellappmgr/cellappmgr.cpp:1774)。

CellAppMgr 会向所有 CellApp 广播 game time，见 [cellapps.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellappmgr/cellapps.cpp:540)。

这说明扩展不是只把进程放进列表，还要把它纳入统一 Tick 和负载控制。

## Entity Offload

Cell 负载均衡最终会落到实体迁移和 Ghost 维护。

源码入口：

- `OffloadChecker::run()` 执行 offload 和 ghost 检查，见 [offload_checker.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/offload_checker.cpp:41)。
- `Cell::offloadEntity()` 调用 `Entity::offload()`，见 [cell.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/cell.cpp:183)。
- `Entity::offload()` 是真实实体迁移入口之一，见 [entity.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/entity.cpp:1968)。
- `RealEntity` 在 offload 前通知 ghosts，见 [real_entity.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/real_entity.cpp:243)。

这说明动态扩展真正难点不是“多启动一个进程”，而是：

- 何时切 Cell 边界。
- 哪些实体要迁移。
- Ghost 是否已准备好。
- 迁移期间消息如何缓冲。
- Channel version 如何识别过期包。
- Base/Cell/Client 三方状态如何一致。

这些内容后续实体迁移和 AOI 章节会深入展开。

## Reviver 容灾模型

`Reviver` 负责监控关键组件并请求 machined 重启。

`ComponentReviver::init()` 做三件事：

1. 读取 ping period、timeout。
2. 通过 machined 查找目标 interface 地址。
3. 注册 birth/death listener。

源码见 [component_reviver.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/reviver/component_reviver.cpp:62)。

`ComponentReviver::handleTimeout()` 每次 timer 到期：

- 如果还允许 miss，则发送 ping request。
- 收到 `REVIVER_PING_YES` 后重置 miss 计数并标记 attached。
- miss 太多则调用 `revive()`。

源码见 [component_reviver.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/reviver/component_reviver.cpp:252)。

`handleMessage()` 收到 death 消息时，如果地址匹配当前组件，也会触发 revive，见 [component_reviver.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/reviver/component_reviver.cpp:180)。

<MermaidDiagram title="Reviver 恢复流程">
flowchart TD
  A[ComponentReviver] --> B[register birth/death listener]
  A --> C[start ping timer]
  C --> D{ping response?}
  D -- yes --> E[reset missed count attached]
  D -- no --> F[decrement pingsToMiss]
  F --> G{miss too many?}
  G -- no --> C
  G -- yes --> H[Reviver revive]
  B --> I{death message}
  I -- matching addr --> H
  H --> J[CreateMessage to machined]
  J --> K[start process with recover flag]
</MermaidDiagram>

## machined 的边界

`Reviver::revive()` 通过 `CreateMessage` 向本地 machined 请求创建进程：

- `cm.uid_ = getUserId()`
- `cm.recover_ = 1`
- `cm.name_ = createComponent`
- `cm.config_ = BW_COMPILE_TIME_CONFIG`
- 发送到 `127.0.0.1`

源码见 [reviver.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/reviver/reviver.cpp:450)。

源码里还有 TODO：最好知道 machined 是否真正成功启动进程，失败原因之一可能是 `machined.conf` 未配置正确。见 [reviver.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/reviver/reviver.cpp:459)。

这说明 Reviver 是进程级恢复，不是完整编排系统。它能请求重启，但不等价于现代 orchestrator 的调度、健康检查、资源约束、滚动升级和事件审计。

## 故障恢复边界

BigWorld 有多层恢复：

- Reviver/machined：进程死亡后重启。
- Manager birth/death：组件发现和状态更新。
- BaseApp/CellApp death handler：处理实体和 Channel 恢复。
- DB 备份和实体持久化：恢复状态来源。
- Channel version 和 recently-dead channels：识别过期包和死亡组件残留消息。

但恢复不是魔法：

- 如果内存状态没有备份，恢复可能丢失。
- 如果 Entity 处于 offload 中间态，恢复逻辑更复杂。
- 如果数据库不可用，进程重启也无法完成业务恢复。
- 如果控制面本身脑裂，旧 machined 模型没有现代共识系统保证。
- 如果协议版本不兼容，重启后的进程也可能无法加入。

## 当时取舍

BigWorld 的扩展与容灾反映的是物理机/虚拟机时代的 MMO 集群设计：

- 进程角色固定，管理器集中协调。
- machined 提供机器级进程发现和启动能力。
- Reviver 做轻量进程监控和重启。
- 负载均衡围绕 Cell、Entity 和 Ghost，而不是围绕 HTTP 请求。
- 多进程隔离崩溃域，避免单进程多线程全服崩溃。

没有选择 Kubernetes、Nomad、service mesh 的原因很直接：这些不属于它主要设计年代；更重要的是，普通无状态编排解决不了 MMO 实体迁移问题。

## 现代方案对比

<div class="decision-table">

| 维度 | BigWorld | 现代常见方案 | 判断 |
| --- | --- | --- | --- |
| 进程启动 | machined CreateMessage | systemd/Kubernetes/Nomad | 可逐步替换资源层 |
| 服务发现 | machined + Mercury birth/death | DNS/registry/control plane | 需要桥接旧协议 |
| 扩容 | App 注册 + Manager 接纳 | HPA/KEDA/autoscaler | 只解决拉起进程，不解决状态迁移 |
| 负载均衡 | Cell/Entity offload | shard/actor rebalance | BigWorld 更贴近 MMO 空间模型 |
| 容灾 | Reviver ping + death listener | liveness/readiness + restart policy | Reviver 简单，但观测和策略弱 |
| 一致性 | Manager 权威协调 | etcd/RAFT/lease | 现代控制面更强，但迁移成本高 |

</div>

## 现代化建议

1. 先保留 BigWorld 的状态迁移模型，不要用 Kubernetes 直接替代 CellAppMgr。
2. 把 machined/Reviver 能力抽象成可替换 supervisor 接口。
3. 让进程注册、birth/death、revive 事件全部结构化输出。
4. 给 CellApp 加入、拒绝、pending、ready、负载迁移建立指标。
5. 对 Cell offload、Base offload、CellApp death 建立多进程故障注入测试。
6. 引入现代编排时只替换资源层，状态层仍由 Manager 和 Entity 迁移协议控制。
7. 如果要自动扩缩容，扩容决策必须基于 game load、spare time、AOI 压力和迁移成本，而不是 CPU 一项指标。

## 本章边界

本章只建立动态扩展与容灾的框架。后续需要继续深入实体模型、AOI/Ghost、Cell 分区、实体迁移和 DB 持久化，才能完整解释 BigWorld 的状态治理。
