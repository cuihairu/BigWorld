# 进程拓扑与职责切分

<div class="arch-hero">

BigWorld 服务端不是单进程游戏服务器，而是多进程 MMO 运行时。理解它的第一步，是把“玩家会话”“全局实体”“空间模拟”“数据库”“登录入口”“控制面”和“故障恢复”拆开看。

</div>

## 核心结论

BigWorld 的进程拓扑体现了典型 MMO 服务器分层：

- `LoginApp` 面向客户端登录入口。
- `BaseApp` 承载玩家会话、Base 实体、全局逻辑和与客户端的连接关系。
- `CellApp` 承载空间内实体、AOI、物理/位置、Cell 实体逻辑。
- `DBApp` 承载数据库交互、实体持久化、账号/实体查询。
- `BaseAppMgr`、`CellAppMgr`、`DBAppMgr` 是控制面和注册中心的一部分。
- `Reviver` 负责监控关键组件并请求 `machined` 拉起恢复进程。

这不是现代微服务式按业务域切分，而是围绕 MMO 的运行时权威域切分。

## 进程拓扑

<div class="topology-grid">
  <div class="topology-card">
    <h3>LoginApp</h3>
    <p>处理外部登录请求，完成挑战、认证、分配后续连接目标。</p>
  </div>
  <div class="topology-card">
    <h3>BaseApp</h3>
    <p>管理玩家在线会话、Base 实体、跨 Cell 协调和客户端通信。</p>
  </div>
  <div class="topology-card">
    <h3>CellApp</h3>
    <p>负责空间模拟、实体位置、AOI、Ghost、Cell 内逻辑。</p>
  </div>
  <div class="topology-card">
    <h3>DBApp</h3>
    <p>封装数据库任务，把阻塞数据库操作隔离到后台任务模型中。</p>
  </div>
  <div class="topology-card">
    <h3>Mgr 组件</h3>
    <p>负责注册、发现、负载信息、扩展接纳、恢复协调和全局控制。</p>
  </div>
  <div class="topology-card">
    <h3>Reviver</h3>
    <p>通过 ping、birth/death 消息和 machined 交互做进程级恢复。</p>
  </div>
</div>

<MermaidDiagram title="BigWorld 服务端进程关系">
flowchart LR
  Client[Client]
  Login[LoginApp]
  Base[BaseApp]
  Cell[CellApp]
  DB[DBApp]
  BAM[BaseAppMgr]
  CAM[CellAppMgr]
  DBM[DBAppMgr]
  Rev[Reviver]
  Machined[machined]

  Client --> Login
  Client <--> Base
  Base <--> Cell
  Base <--> DB
  Cell <--> DB
  Base --> BAM
  Cell --> CAM
  DB --> DBM
  BAM <--> CAM
  DBM <--> BAM
  DBM <--> CAM
  Rev --> Machined
  Rev -. ping/death .-> BAM
  Rev -. ping/death .-> CAM
</MermaidDiagram>

## 启动与注册链路

服务端进程入口普遍通过 `bwMainT<APP>()` 模板启动。源码入口：

<div class="evidence-grid">
  <div class="evidence-card">
    <h3>BaseAppMgr</h3>
    <p><code>server/baseappmgr/main.cpp</code> 调用 <code>bwMainT&lt;BaseAppMgr&gt;</code>。</p>
  </div>
  <div class="evidence-card">
    <h3>CellAppMgr</h3>
    <p><code>server/cellappmgr/main.cpp</code> 调用 <code>bwMainT&lt;CellAppMgr&gt;</code>。</p>
  </div>
  <div class="evidence-card">
    <h3>CellApp</h3>
    <p><code>server/cellapp/main.cpp</code> 调用 <code>bwMainT&lt;CellApp&gt;</code>。</p>
  </div>
  <div class="evidence-card">
    <h3>Reviver</h3>
    <p><code>server/reviver/main.cpp</code> 调用 <code>bwMainT&lt;Reviver&gt;</code>。</p>
  </div>
</div>

组件启动后会注册 Mercury interface，并向 `machined` 注册。例如：

- `BaseAppMgrInterface::registerWithInterface()` 与 `registerWithMachined()` 在 [baseappmgr.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/baseappmgr/baseappmgr.cpp:389)。
- `CellAppMgrInterface::registerWithInterface()` 与 `registerWithMachined()` 在 [cellappmgr.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellappmgr/cellappmgr.cpp:238)。
- `CellAppInterface::registerWithInterface()` 在 [cellapp.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/cellapp.cpp:539)，向 `machined` 注册在 [cellapp.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/cellapp.cpp:711)。
- `DBAppInterface::registerWithInterface()` 在 [dbapp.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/dbapp/dbapp.cpp:231)，向 `machined` 注册在 [dbapp.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/dbapp/dbapp.cpp:581)。

## 职责边界

### LoginApp

`LoginApp` 的职责是入口而不是长期会话承载。它注册外部 `LoginInterface` 和内部 `LoginIntInterface`，并监听 `DBAppMgr` 出生消息。源码入口在 [loginapp.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/loginapp/loginapp.cpp:296)。

设计取舍：

- 优点：登录入口与游戏会话隔离，便于限流和认证。
- 代价：登录成功后的连接迁移、挑战协议、DB 查询链路更复杂。

### BaseApp

`BaseApp` 是玩家在线会话和 Base 实体的核心进程。它需要知道 `BaseAppMgr`、`CellAppMgr`，并处理其他 BaseApp 的 birth 消息。源码入口：

- 注册内部接口：[baseapp.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/baseapp/baseapp.cpp:3066)
- 注册外部接口：[baseapp.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/baseapp/baseapp.cpp:3069)
- 处理 `CellAppMgr` birth：[baseapp.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/baseapp/baseapp.cpp:1894)
- 处理 `BaseAppMgr` birth：[baseapp.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/baseapp/baseapp.cpp:1910)

### CellApp

`CellApp` 是空间模拟进程。它向 `CellAppMgr` 注册，由 `CellAppMgr` 统一做空间、Cell、负载均衡和恢复协调。CellApp 接入 CellAppMgr 的网关请求在 [cellappmgr_gateway.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/cellappmgr_gateway.cpp:37)。

### Manager 组件

Mgr 组件不是业务微服务，而是控制面：

- `BaseAppMgr` 管理 BaseApp 集合、负载和 BaseApp 之间的发现。
- `CellAppMgr` 管理 CellApp、空间、Cell 边界、负载均衡和恢复。
- `DBAppMgr` 管理 DBApp、数据库组件出生死亡、与 Base/Cell 管理器协调。

`CellAppMgr::addApp()` 是运行时接纳新 CellApp 的关键入口，见 [cellappmgr.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellappmgr/cellappmgr.cpp:1245)。

## 动态扩展的真实含义

BigWorld 支持运行中接纳新 `BaseApp` / `CellApp`，但它不是 Kubernetes HPA 式自动扩缩容。

更准确的模型是：

<div class="flow-strip">
  <span class="flow-node">外部启动进程</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">向 machined / Mgr 注册</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">Mgr 接纳</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">负载再平衡</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">实体/Cell 迁移</span>
</div>

这种设计的重点是运行时迁移和控制面协调，而不是声明式资源调度。

## 当时取舍

BigWorld 的进程拆分在当时是务实选择：

- 多进程天然隔离崩溃域，比单进程多线程更容易定位故障。
- C++ 主循环加 Python 脚本嵌入，适合把性能敏感路径留在引擎层。
- Manager 作为权威控制面，避免每个 App 都维护全局复杂状态。
- `machined` 和 `Reviver` 形成进程级运维能力，符合当时物理机/虚拟机时代的集群管理方式。

它没有选择今天常见的容器编排和服务网格，原因很直接：这些技术路线不属于它的主要设计年代；而且 MMO 的状态迁移、AOI 和 Cell 负载均衡也不是普通无状态服务伸缩能解决的。

## 现代对比

<div class="decision-table">

| 维度 | BigWorld 方案 | 现代常见方案 | 取舍 |
| --- | --- | --- | --- |
| 进程组织 | 固定组件角色 + Manager | 容器化服务 + 编排平台 | BigWorld 更贴近游戏状态模型，现代方案运维生态更强 |
| 扩容 | 外部启动进程，Mgr 接纳并迁移 | HPA / KEDA / 自研调度器 | BigWorld 重运行时状态迁移，现代方案重资源调度 |
| 容灾 | Reviver + machined | supervisor / systemd / Kubernetes / Nomad | BigWorld 方案简单直接，但观测和策略表达较弱 |
| 服务发现 | Machined + Mercury birth/death | DNS / service registry / control plane | BigWorld 强耦合引擎协议，现代方案通用性更高 |
| 状态管理 | Entity、Cell、Ghost 内生模型 | Actor / ECS / 分片服务 | BigWorld 针对 MMO 深度定制，迁移成本高 |

</div>

## 后续研究入口

进程拓扑只是外壳。真正决定性能和扩展边界的是：

- 每个进程内部的事件循环模型。
- Mercury 网络栈如何把 socket 事件转成消息。
- EntityDef 如何把脚本类型、协议流和持久化绑定在一起。
- CellAppMgr 如何做 Cell 分区、负载均衡和实体迁移。
