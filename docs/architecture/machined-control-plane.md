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

## machined 的源码边界

machined 解决的是 BigWorld 进程发现和管理：

- 它知道 BigWorld interface。
- 它能按用户、版本、interface name 查找进程。
- 它能启动 BigWorld 组件。
- 它能通知组件 birth/death。

它不解决这些问题：

- Cell 分区和负载均衡。
- BaseApp/CellApp 实体状态恢复。
- DBApp 数据一致性。
- Watcher 权限隔离。
- 强一致 leader election。

因此 BigWorld 状态控制面仍然需要知道 BaseApp、CellApp、DBApp、Space、Entity、load、offload。machined 只提供底层发现、启动和通知能力。

## 源码取舍

machined、Manager、Reviver 的分工是：

- machined 负责本机进程注册、查询、启动、信号和 birth/death 广播。
- Manager 消费 birth/death，并维护 BigWorld 运行时状态。
- Reviver 监控组件是否存活，决定是否请求 machined 启动替代进程。

源码代价：

- 状态分散在 machined、Manager 和各 App 内部，排障需要串联多类日志和 watcher。
- 依赖本机 machined 正常运行。
- 安全模型偏内网可信。
- 没有强一致控制面，旧进程残留、重复 birth/death 和网络分区都需要上层防护。

## 源码验证重点

machined 控制面验证应覆盖发现、启动和通知：

- `ProcessMessage` 注册后，`findInterface()` 应能按 interface name、version、user 找到目标进程。
- watcher nub 注册信息应包含端口、进程 ID、简称和版本号。
- birth/death listener 应能收到组件创建和退出事件。
- `CreateMessage` 应按 component、config、user、recover flag 和 output forwarding 参数启动进程。
- Reviver 触发恢复时应只负责决策，实际执行应走 machined。
- machined 不可用时，依赖它的注册、发现和启动路径应给出明确失败状态。

## 本章边界

本章解释 machined 控制面。它与 [动态扩展与容灾](/architecture/scaling-fault-tolerance) 互补：后者关注 App 接纳、Cell 负载和 Reviver 恢复语义，本文关注底层进程发现和启动机制。
