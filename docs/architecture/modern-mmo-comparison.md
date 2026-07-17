# 现代 MMO 架构对比

<div class="arch-hero">

BigWorld 已经老了，但它不是“过时到没有价值”。相反，它把 MMO 服务端最困难的几个问题做成了可学习的工程体系：多进程分片、Base/Cell 权威拆分、AOI、Ghost、实体迁移、负载均衡、可靠 UDP、脚本热更新和运维控制面。现代化的关键不是推倒重写，而是识别哪些设计仍然优秀，哪些需要替换。

</div>

## 先给结论

BigWorld 与现代 MMO/大型在线游戏架构相比：

- 在空间分区、实体迁移、AOI、Ghost 和进程级状态治理上仍然很有学习价值。
- 在构建、依赖、安全、观测标准化、云原生编排、测试可控时间和 Python 版本上明显老化。
- 网络层不应简单从 `epoll` 跳到 `io_uring`，更应该先衡量 Mercury UDP、批量收发、网关拆分和消息预算。
- 线程模型不应直接改成全并行 job system，应先保留状态归属，再局部并行 CPU 密集模块。
- Python 3.12 是必要现代化方向，但它不是架构入口，而是嵌入式脚本运行时迁移工程。

## BigWorld 的核心优势

<div class="decision-grid">
  <div class="decision-card">
    <h3>状态分片</h3>
    <p>BaseApp/CellApp/DBApp/Mgr 的职责拆分清晰，符合 MMO 长连接和世界状态需求。</p>
  </div>
  <div class="decision-card">
    <h3>空间权威</h3>
    <p>Cell 负责空间内实体权威，CellAppMgr 管理空间和负载，不是普通服务网关模型。</p>
  </div>
  <div class="decision-card">
    <h3>AOI 与 Ghost</h3>
    <p>Witness/Ghost/Haunt 机制把可见性和跨 Cell 状态复制作为一等架构问题。</p>
  </div>
  <div class="decision-card">
    <h3>可靠 UDP</h3>
    <p>Mercury 在 UDP 上实现可靠消息、ACK、重传、piggyback、indexed channel 和版本语义。</p>
  </div>
  <div class="decision-card">
    <h3>运行时调试</h3>
    <p>Watcher、Profiler、EntityProfiler 与 message_logger 深度嵌入引擎运行时。</p>
  </div>
  <div class="decision-card">
    <h3>动态扩展</h3>
    <p>扩容不是无状态副本，而是 App 接纳、Cell 重划、实体 offload 和状态迁移。</p>
  </div>
</div>

## BigWorld 的老化点

BigWorld 的老化主要不在“用了 C++”或“用了 epoll”，而在这些方面：

- Python 2.7 嵌入式运行时已不可维护。
- CMake/Makefile 和第三方依赖历史包袱重。
- 安全模型偏可信内网，缺少现代身份、授权和审计。
- Watcher 写能力强，但权限模型不适合现代生产暴露。
- 时间系统缺少统一可注入 clock，测试确定性不足。
- 网络收发没有 `recvmmsg/sendmmsg` 这类批量 UDP 优化。
- 没有现代标准 metrics/tracing/logging 集成。
- 部署模型不是 Kubernetes/Nomad 这类编排系统。

这些老化点应该分层处理，而不是打包成一次大重构。

## 网络模型对比

现代网络技术很多，但 BigWorld 的问题不能只按“哪个模型性能最高”排序。

<div class="decision-table">

| 技术 | 现代优势 | 对 BigWorld 的适配判断 |
| --- | --- | --- |
| LT epoll | 成熟、稳定、低风险 | 当前 Linux 路径，继续可用 |
| ET epoll | 减少重复通知 | 需要 handler 严格 drain，收益不一定高 |
| `recvmmsg/sendmmsg` | 降低 UDP syscall 成本 | 最值得优先实验 |
| `SO_REUSEPORT` | 多 socket 多核分流 | 需要解决 Channel 状态分片 |
| `io_uring` | 异步提交/完成，现代内核高吞吐 | 改动执行模型，非第一优先级 |
| QUIC | 加密、拥塞、连接迁移标准化 | 与 Mercury 可靠语义重叠，迁移成本高 |
| AF_XDP/DPDK | 极限吞吐和低延迟 | 运维复杂，适合网关特例，不适合核心逻辑进程 |

</div>

BigWorld 现代化更合理的网络路线：

1. 增加指标，定位 syscall、协议解析、脚本、AOI、DB 或 Tick 哪个是瓶颈。
2. 对 UDP 接收/发送做 `recvmmsg/sendmmsg` 实验。
3. 将公网入口拆成 gateway，核心 BaseApp/CellApp 保持状态模型稳定。
4. 只有确认 I/O wait 是主瓶颈后，再评估 `io_uring`。

## 线程与并发对比

BigWorld 的主模型是“单 Reactor 主线程 + 后台任务 + 多进程分片”。现代常见方案包括：

- Actor：每个 actor 串行处理 mailbox。
- ECS：数据导向、批量系统更新。
- Job System：work stealing，并行任务图。
- Async runtime：事件循环和协程。

不能直接说现代模型一定更好。

<div class="decision-table">

| 模型 | 适合场景 | 风险 |
| --- | --- | --- |
| BigWorld 单 Reactor | 状态复杂、脚本强、调试优先 | 单进程单核瓶颈 |
| Actor | 实体 mailbox、状态归属清晰 | 跨 actor 事务和调度复杂 |
| ECS | 大量同质实体、物理/技能批量计算 | MMO 业务脚本和动态对象不一定适配 |
| Job System | CPU 密集任务并行 | 对象生命周期和 Python GIL 冲突 |
| Async/Coroutine | I/O 并发和后台等待 | 不能解决 CPU 热点 |

</div>

对 BigWorld 来说，合理路线是：

- 保留 Cell/Base 状态归属。
- 后台化 DB、文件、压缩、加密、资源加载。
- 对 AOI、寻路、批量物理等 CPU 热点引入 job system。
- 所有实体状态提交仍回到权威线程或 actor。
- Python 3.12 后也不要假设脚本可以无锁并行。

## 状态模型对比

BigWorld 的 Base/Cell/Client 模型与现代 actor/shard 模型非常接近，但更偏 MMO 空间语义：

- Base：玩家长期状态、账号会话、跨空间身份。
- Cell：空间内权威实体和物理位置。
- Client：表现和输入端。
- Ghost：跨 Cell 可见副本。
- Witness：客户端观察窗口。

现代常见替代：

- Actor per entity。
- Zone/shard server。
- Interest management service。
- Event sourced entity。
- ECS world partition。

BigWorld 的优势是把空间 AOI 与实体迁移做成完整闭环。现代方案常在这块重新踩坑。

BigWorld 的不足是：

- 迁移协议复杂，测试难。
- 状态序列化与 EntityDef 强绑定。
- 热更新、迁移、DB 持久化耦合高。
- 很难做全局确定性 replay。

## 部署与扩容对比

Kubernetes 可以拉起 Pod，但不能自动解决 MMO 状态迁移。

<div class="decision-table">

| 问题 | Kubernetes 擅长 | BigWorld 擅长 |
| --- | --- | --- |
| 进程拉起 | 强 | machined/Reviver 较弱 |
| 健康检查 | 强 | Reviver ping 简单 |
| 资源隔离 | 强 | 依赖机器/进程配置 |
| 状态迁移 | 不直接解决 | Cell/Entity offload |
| 空间负载均衡 | 不直接解决 | CellAppMgr 内建 |
| 服务发现 | 强 | machined/birth death |
| 滚动升级 | 强 | 热更新和重启策略复杂 |

</div>

所以现代化不是“用 Kubernetes 替代 BigWorld 控制面”，而是：

- 用 Kubernetes/Nomad/systemd 替代进程拉起和基础健康检查。
- 保留或重写 MMO 状态控制面。
- 让 CellAppMgr/BaseAppMgr 与现代编排系统桥接。
- 把 Reviver 的职责收缩到游戏语义恢复，或由现代 orchestrator 接管进程级恢复。

## 序列化与协议对比

BigWorld 的 EntityDef + BinaryStream 同时服务：

- 网络 RPC。
- 脚本方法。
- 属性同步。
- DB 持久化。
- 实体迁移。
- 热更新。

现代 IDL 常见选择：

- Protobuf。
- FlatBuffers。
- Cap'n Proto。
- JSON/MsgPack。
- 自定义 bitstream。

直接替换 EntityDef 并不现实，因为 EntityDef 是“游戏对象契约”，不是单纯 wire format。

更合理的路线：

- 保留 EntityDef 作为游戏语义层。
- 为 wire format 增加 golden tests。
- 对外部 gateway 可使用 Protobuf/FlatBuffers。
- 内部迁移流和热更新先建立兼容矩阵。
- 长期再考虑把 EntityDef 编译成现代 IDL 或 schema。

## 热更新对比

BigWorld 有 `reloadScript`，但源码和文档都指向一个结论：这是开发调试导向的高风险机制，不应当被当作现代生产热更新平台。

现代生产热更新通常分几类：

- 配置热更新。
- 数据表热更新。
- 脚本灰度。
- 进程滚动升级。
- actor/entity 状态迁移。
- 双版本协议兼容。

BigWorld 的学习价值是它已经触及最难点：实体对象和脚本解释器迁移。

现代化建议：

- 保留开发期 reload。
- 生产期优先用滚动升级 + 实体迁移 + 双版本协议。
- EntityDef 变化必须有兼容策略。
- 热更新前做 dry-run 和迁移验证。

## 安全和可观测性对比

BigWorld 内建 Watcher/Profiler 很强，但不是现代生产标准。

现代目标应是：

- Watcher 继续作为调试和控制面。
- Metrics 导出到 Prometheus 或兼容系统。
- Logs 结构化并带 trace/entity/channel 上下文。
- Trace 覆盖登录、RPC、DB、迁移、热更新和 death recovery。
- 管理面有 RBAC、审计、网络隔离。
- 加密和密钥管理符合现代要求。

也就是说，保留引擎内省能力，但把生产观测接到标准平台。

## 现代化优先级

建议分为三条线：

### P0：先让旧系统可测可观测

- 固化构建基线。
- 跑通现有单元测试。
- 增加核心指标导出。
- 建立 Wire format golden tests。
- 建立 fake clock seam。
- 审计外部接口和 Watcher 暴露面。

### P1：低风险性能与工程现代化

- 批量 UDP 收发实验。
- 后台任务队列观测和预算。
- CMake 兼容现代版本。
- OpenSSL 和 Python 依赖清单化。
- 日志结构化。
- Gateway 拆分可行性研究。

### P2：高风险架构演进

- Python 3.12 嵌入式运行时迁移。
- Actor/job system 局部引入。
- SO_REUSEPORT 或多网关分片。
- 控制面与 Kubernetes/Nomad 桥接。
- EntityDef 现代 schema 编译。
- 热更新生产化。

## 不应照搬的地方

- 不要继续使用 Python 2.7。
- 不要把 Watcher 写接口暴露到公网或普通内网。
- 不要把 `reloadScript` 当生产热更新方案。
- 不要认为 Reviver 等价于现代容器编排。
- 不要用 raw pointer 风格写新代码。
- 不要在无指标前替换网络后端。
- 不要把 EntityDef 简化成普通 JSON schema。

## 仍然值得学习的地方

- 多进程角色拆分。
- Base/Cell/Client 权威域。
- AOI/Witness/Ghost。
- Cell 负载均衡。
- 实体迁移与 offload。
- 可靠 UDP 协议设计。
- 运行时 Watcher introspection。
- EntityProfiler 驱动负载治理。
- 脚本系统与 C++ 引擎边界。

## 最终判断

BigWorld 不是现代模板，但它是 MMO 服务端架构的优秀研究样本。

正确学习方式是：

1. 先理解它为什么这样设计。
2. 再识别哪些选择受时代限制。
3. 最后决定哪些现代技术真正解决当前问题。

如果直接用现代术语覆盖它，会错过最有价值的部分。如果完全照搬它，也会继承过时依赖、安全边界和测试短板。
