# machined 控制面与进程发现

<div class="arch-hero">

BigWorld 的控制面不是 Kubernetes，也不是 etcd/RAFT。它依赖 bwmachined 提供机器级进程注册、接口发现、birth/death 通知、启动进程和发送信号等能力。理解 machined，才能理解为什么 BaseAppMgr、CellAppMgr、DBAppMgr、LoginApp 和 Reviver 能组成一个动态集群。

</div>

## 先给结论

machined 是 BigWorld 旧式集群控制面的核心基础设施：

- 进程启动后向 machined 注册 interface 名称、端口、版本和 ID。
- 其他组件通过 `findInterface()` 查找某类 interface 的地址。
- Manager 注册 birth/death listener，收到组件启动/死亡通知。
- Reviver 通过 `CreateMessage` 请求 machined 重启组件。
- 工具可以通过 machined 查询进程、发送信号、获取机器信息。

关键源码：

- MachineGuard 消息类型在 [machine_guard.hpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/machine_guard.hpp:121)。
- `ListenerMessage` 用于 birth/death listener，见 [machine_guard.hpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/machine_guard.hpp:600)。
- `CreateMessage` 用于请求 machined 启动进程，见 [machine_guard.hpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/machine_guard.hpp:634)。
- `registerWithMachined()` 在 [machined_utils.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/machined_utils.cpp:47)。
- `registerBirthListener()` / `registerDeathListener()` 在 [machined_utils.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/machined_utils.cpp:138)。
- `findInterface()` 在 [machined_utils.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/machined_utils.cpp:339)。
- BaseAppMgr 初始化时注册 death/birth listener，见 [baseappmgr.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/baseappmgr/baseappmgr.cpp:378)。
- Reviver 使用 `CreateMessage` 见 [reviver.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/reviver/reviver.cpp:440)。

## machined 提供什么

`MachineGuardMessage::Message` 枚举展示了 machined 的能力边界：

- `WHOLE_MACHINE_MESSAGE`
- `PROCESS_MESSAGE`
- `PROCESS_STATS_MESSAGE`
- `LISTENER_MESSAGE`
- `CREATE_MESSAGE`
- `SIGNAL_MESSAGE`
- `TAGS_MESSAGE`
- `USER_MESSAGE`
- `PID_MESSAGE`
- `RESET_MESSAGE`
- `QUERY_INTERFACE_MESSAGE`
- `CREATE_WITH_ARGS_MESSAGE`
- `MACHINE_PLATFORM_MESSAGE`

源码见 [machine_guard.hpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/machine_guard.hpp:124)。

这些能力组合起来，就是一个轻量控制面：

- 服务发现。
- 进程状态查询。
- 进程 birth/death 通知。
- 远程启动。
- 发送信号。
- 机器和用户信息查询。

它比普通 DNS/service discovery 更强，但比现代 orchestrator 弱。

## 注册流程

进程注册使用 `registerWithMachined()`：

- name 为空则直接成功。
- 构造 `ProcessMessage`。
- 设置 `REGISTER` 或 `DEREGISTER`。
- 设置 category 为 `SERVER_COMPONENT`。
- 写入端口、name、id、版本号。
- 发送到 localhost 的 machined 并等待回复。

源码见 [machined_utils.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/machined_utils.cpp:50)。

<MermaidDiagram title="进程注册到 machined">
sequenceDiagram
  participant App
  participant Machined
  App->>Machined: ProcessMessage REGISTER(name, id, port, version)
  Machined-->>App: ProcessMessage reply
  App->>App: interface visible to other components
</MermaidDiagram>

重要细节：注册后其他进程就可能找到并发送消息，所以源码里禁止在注册后调用 blocking reply handler。见 [machined_utils.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/machined_utils.cpp:58)。

## 接口发现

`findInterface()` 构造 `ProcessStatsMessage`，按条件查找：

- category。
- uid。
- name。
- id。

源码见 [machined_utils.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/machined_utils.cpp:339)。

收到 `ProcessStatsMessage` 后会检查：

- interface version 是否匹配 `MERCURY_INTERFACE_VERSION`。
- username 是否匹配当前用户。
- 组装 `Mercury::Address`。

源码见 [machined_utils.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/machined_utils.cpp:249)。

这说明 BigWorld 的服务发现不是裸地址发现，还带有版本和用户隔离检查。

## Birth / Death Listener

`ListenerMessage` 用于注册 birth/death listener，源码注释写明：listeners 会收到网络上进程 birth/death 通知。见 [machine_guard.hpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/machine_guard.hpp:600)。

`registerListener()` 会：

- 把要回调的 Mercury message 打包成 `UDPBundle`。
- 将 Address 占位符前后的二进制片段保存到 `preAddr_` / `postAddr_`。
- 构造 `ListenerMessage`。
- 设置 `ADD_BIRTH_LISTENER` 或 `ADD_DEATH_LISTENER`。
- 设置 uid、pid、port、interface name。
- 发送给 localhost machined。

源码见 [machined_utils.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/machined_utils.cpp:104)。

设计很有意思：machined 收到进程事件后，可以把目标地址填入预先保存的 Mercury 消息片段，再发回监听者。

## BaseAppMgr 的使用方式

BaseAppMgr 初始化时：

- 初始化 `ReviverSubject`。
- 检查是否 `-recover`。
- 注册 BaseApp 和 ServiceApp death listener。
- 注册自身 interface 到 machined。
- 注册 CellAppMgr birth listener。
- 尝试 `findInterface("CellAppMgrInterface")`。
- 注册 BaseAppMgr birth listener。

源码见 [baseappmgr.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/baseappmgr/baseappmgr.cpp:365)。

<MermaidDiagram title="BaseAppMgr 控制面初始化">
flowchart TD
  A[BaseAppMgr init] --> B[ReviverSubject init]
  B --> C[registerDeathListener BaseAppIntInterface]
  C --> D[registerDeathListener ServiceAppInterface]
  D --> E[registerWithMachined BaseAppMgrInterface]
  E --> F[registerBirthListener CellAppMgrInterface]
  F --> G[findInterface CellAppMgrInterface]
  G --> H[registerBirthListener BaseAppMgrInterface]
</MermaidDiagram>

这说明 Manager 进程不是通过静态配置互相认识，而是通过 machined 发现和事件通知建立控制面连接。

## Reviver 与 machined

Reviver 的恢复动作最终是向 machined 发送 `CreateMessage`：

- `cm.uid_ = getUserId()`
- `cm.recover_ = 1`
- `cm.name_ = createComponent`
- `cm.config_ = BW_COMPILE_TIME_CONFIG`
- 发送到 `127.0.0.1`

源码见 [reviver.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/reviver/reviver.cpp:440)。

`CreateMessage` 字段包括：

- executable name。
- config。
- user id。
- recover flag。
- output forwarding ip/port。

源码见 [machine_guard.hpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/machine_guard.hpp:638)。

这说明 Reviver 本身不是进程执行器，它是监控和恢复决策者；真正启动进程的是 machined。

## 与 Kubernetes 的根本差异

machined 解决的是旧式机房里的 BigWorld 进程发现和管理：

- 它知道 BigWorld interface。
- 它能按用户、版本、interface name 查找进程。
- 它能启动 BigWorld 组件。
- 它能通知组件 birth/death。

Kubernetes 解决的是容器编排：

- 调度 Pod。
- 健康检查。
- Service/DNS。
- 资源限制。
- 滚动升级。
- Secret/ConfigMap。
- 节点管理。

两者不是同一层。

BigWorld 状态控制面仍然需要知道 BaseApp、CellApp、DBApp、Space、Entity、load、offload。Kubernetes 不会自动理解这些游戏语义。

## 当时为什么这样选

高置信工程判断：

- BigWorld 设计年代还没有 Kubernetes 这类通用容器编排。
- MMO 集群需要 BigWorld 自己的进程角色、interface 版本和用户隔离。
- 物理机时代，用本机 daemon 管理进程和广播发现很自然。
- Manager 需要 birth/death 事件驱动状态恢复，machined 提供了轻量机制。
- Reviver 与 machined 分工清晰：一个判断，一个执行。

代价：

- 缺少现代资源调度和隔离。
- 依赖本机 machined 正常运行。
- 安全模型偏内网可信。
- 没有 etcd/RAFT 那样的强一致控制面。
- 与云原生平台集成需要桥接。

## 现代方案对比

<div class="decision-table">

| 能力 | machined | 现代方案 | 判断 |
| --- | --- | --- | --- |
| 进程注册 | ProcessMessage | service registry / sidecar | 可桥接 |
| 服务发现 | `findInterface()` | DNS / registry / xDS | 需保留 interface 语义 |
| birth/death | ListenerMessage | watch API / events | 机制类似，但安全和审计弱 |
| 进程启动 | CreateMessage | Kubernetes/Nomad/systemd | 可替换资源层 |
| 信号控制 | SignalMessage | orchestrator exec/signal | 现代平台更完整 |
| 版本检查 | Mercury interface version | deployment version / protocol version | BigWorld 版本检查仍必要 |
| 状态恢复 | Reviver + Manager | operator/controller | 可用 operator 模式重写 |

</div>

## 现代化建议

1. 先文档化所有 interface name、id、port、birth/death listener 关系。
2. 将 machined 依赖从“隐式必须存在”变成启动前检查和清晰错误。
3. 对 watcher、machined、reviver 管理面增加网络隔离和认证策略。
4. 如果迁移到 Kubernetes，不要直接删除 Manager/Machined 语义；先做 adapter。
5. 进程启动可以交给 systemd/Kubernetes，但 birth/death 事件要回流给 BigWorld 控制面。
6. 对 Reviver 恢复动作增加结果确认、失败原因记录和重试上限。
7. 长期可把 machined 能力拆成 service discovery、process supervisor、event bus 三部分。

## 本章边界

本章解释 machined 控制面。它与 [动态扩展与容灾](/architecture/scaling-fault-tolerance) 互补：后者关注 App 接纳、Cell 负载和 Reviver 恢复语义，本文关注底层进程发现和启动机制。
