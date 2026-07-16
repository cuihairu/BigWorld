# 网络 I/O 模型选择

<div class="arch-hero">

这章是 BigWorld 网络架构研究的第一块硬骨头。不能只说“用了 epoll”，也不能用现代 benchmark 直接否定旧方案。必须结合 Mercury 的 UDP Channel 模型、主线程 Reactor、Tick 预算、平台基线和当时技术可用性一起判断。

</div>

## 先给结论

Linux 下 BigWorld Mercury 网络栈使用 `epoll`，但不是 edge-triggered，也不是 `io_uring`。它通过 `EventPoller` 抽象不同平台：

- Linux：`EPoller`
- Emscripten：`PollPoller`
- 其他平台：`SelectPoller`

源码在 [event_poller.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/event_poller.cpp:19)。

关键点：

- `EPoller` 使用 `EPOLLIN/EPOLLOUT`，没有 `EPOLLET`。
- 源码注释明确写着暂时保持类似 `select` 的语义。见 [event_poller.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/event_poller.cpp:1079)。
- 每次 `epoll_wait()` 最多取 10 个事件。见 [event_poller.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/event_poller.cpp:1117)。
- handler 在 Reactor 主线程同步执行。
- UDP 收发使用逐包 `recvfrom` / `sendto`，没有 `recvmmsg/sendmmsg`。

## 平台抽象

<MermaidDiagram title="EventPoller 平台选择">
flowchart TD
  A[EventDispatcher] --> B[EventPoller]
  B --> C{平台}
  C -- Linux 非 Emscripten --> D[EPoller epoll]
  C -- Emscripten --> E[PollPoller poll]
  C -- 其他平台 --> F[SelectPoller select]
  D --> G[Input/Write Handler]
  E --> G
  F --> G
</MermaidDiagram>

设计含义：

- BigWorld 不是为单一 Linux 内核写死网络模型。
- `epoll` 是 Linux 优化路径，不是协议栈本体。
- 上层 Mercury Channel、Bundle、可靠 UDP 才是核心抽象。

## 为什么是 level-triggered epoll

源码在注册 FD 时只设置 `EPOLLIN` 或 `EPOLLOUT`：

```cpp
// TODO: Could be good to use EPOLLET (leave like select for now).
ev.events = isRead ? EPOLLIN : EPOLLOUT;
```

这条注释非常关键。它说明：

- 作者知道 ET 可能有收益。
- 但当时选择保持 `select` 行为一致。
- 这不是“不知道 epoll 高级用法”，而是明确保守取舍。

保守选择的合理性：

- LT 语义更接近 `select`，跨平台行为一致。
- 不要求每个 handler 都严格排空 FD，否则 ET 容易丢事件。
- 对老代码和多平台网络抽象更安全。
- 游戏服务器主循环更关心可预测性，不一定追求最激进 I/O 模型。

## 为什么 epoll 不是性能银弹

Mercury 的主游戏协议不是“一玩家一个 TCP FD”的模型。UDP 路径里，一个 `NetworkInterface` 持有 UDP socket，多个 `UDPChannel` 是逻辑连接。

因此，`epoll` 的收益不能简单按“百万连接 FD”模型评价。

真正热点更可能是：

- 单 UDP socket 的 receive queue 压力。
- 逐包 `recvfrom` / `sendto` 的 syscall 成本。
- Mercury 包解析、ACK、重传、分片和 piggyback。
- 主线程 handler 处理耗时。
- Entity/AOI/脚本逻辑消耗。
- Tick 预算和尾延迟。

这也是为什么现代化评估时，`recvmmsg/sendmmsg` 可能比 `io_uring` 更先值得实验。

## 接收路径

<MermaidDiagram title="UDP 接收路径简图">
sequenceDiagram
  participant Kernel as Kernel UDP Queue
  participant Poller as EPoller
  participant Receiver as PacketReceiver
  participant NI as NetworkInterface
  participant Mercury as Mercury Bundle/Channel
  participant Handler as Message Handler

  Kernel->>Poller: EPOLLIN
  Poller->>Receiver: handleInputNotification(fd)
  loop until EAGAIN or time budget
    Receiver->>Kernel: recvfrom
    Receiver->>NI: process packet
    NI->>Mercury: decode/channel/reliable state
    Mercury->>Handler: dispatch message
  end
</MermaidDiagram>

关键源码：

- `PacketReceiver::handleInputNotification()` 从 [packet_receiver.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/packet_receiver.cpp:83) 开始。
- 循环内会检查 `maxSocketProcessingTimeStamps_`，超时则停止本轮 socket 处理。

这个设计是吞吐与公平性的折中：

- 如果一次只读一个包，高峰期 syscall 和调度开销偏高。
- 如果无限排空 socket，主线程可能饿死 Timer 和其他任务。
- 加时间预算，说明引擎把 Tick 公平性作为一等约束。

## 发送路径

发送使用逐包 `sendto()`，源码见 [packet_sender.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/packet_sender.cpp:347)。

更值得注意的是发送队列满时：

- 如果遇到 `REASON_RESOURCE_UNAVAILABLE` 或 `REASON_TRANSMIT_QUEUE_FULL`
- 代码会使用 `select()` 等待 10ms 后重试
- 源码见 [packet_sender.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/packet_sender.cpp:304)

这意味着：

- 虽然 socket 是非阻塞模型，但发送路径并非全链路完全无等待。
- 高峰期主线程可能在发送重试上消耗时间。
- 现代化时应优先观测发送队列满的频率和耗时。

## TCP 路径

TCP 路径也在 Reactor 下工作：

- 非阻塞 socket。
- 读写事件注册到 dispatcher。
- 部分发送时注册写事件，发送完成后注销。
- 帧格式使用 `uint16` 长度，超长帧使用扩展长度。
- 支持 raw TCP、WebSocket、加密 StreamFilter。

TCP 更适合登录、工具、管理、WebSocket 等场景；游戏主通道仍需要结合 Mercury 的 UDP 可靠层理解。

## 当时为什么没选 io_uring

这里必须严谨：源码无法证明原作者“开会时怎么想”。但可以做高置信工程判断。

`io_uring` 直到 Linux 5.1 才出现，而 BigWorld 14.4.1 的平台痕迹包含 CentOS 5/6/7、老 CMake、Python 2.7、老 GCC/MSVC 兼容。也就是说，`io_uring` 不在它的原始可选方案集合内。

即使放到今天，也不能直接说 `io_uring` 必然更好：

- BigWorld 的游戏 UDP 模型不是海量 FD，而是少量 UDP socket + 大量逻辑 Channel。
- 现有 handler 是同步回调，迁移到 completion queue 会改变执行模型。
- `io_uring` 要处理 buffer 生命周期、提交队列、完成队列、取消、背压和多线程消费。
- 如果热点在协议解析、脚本、AOI 或 Cell 负载，换 I/O 后端收益有限。

所以更合理的现代化顺序是先测量，再实验批量 UDP，再考虑更深层 I/O 重构。

## 横向对比

<div class="decision-table">

| 模型 | 当时可用性 | 优点 | 代价 | 对 BigWorld 的判断 |
| --- | --- | --- | --- | --- |
| `select` | 跨平台成熟 | 简单、兼容性强 | FD 集合扫描、FD 数限制 | 适合回退路径，不适合 Linux 高负载主路径 |
| `poll` | 成熟 | 无固定 FD_SETSIZE | 仍需线性扫描 | Emscripten 路径使用，非 Linux 最优 |
| LT `epoll` | Linux 成熟 | 高效、语义接近 select | readiness 重复通知 | 当前 Linux 方案，稳妥合理 |
| ET `epoll` | Linux 可用 | 减少重复通知 | handler 必须严格排空，bug 风险高 | 源码注释提到但未采用，符合保守取舍 |
| `recvmmsg/sendmmsg` | Linux 2.6.33+ | 批量 UDP，直接降低 syscall | 代码改动中等，平台分支增加 | 现代化优先实验对象 |
| `SO_REUSEPORT` | Linux 3.9+ | 多 socket 分流，配合 RSS | Channel 状态分片复杂 | 需要重构地址/Channel 归属 |
| `io_uring` | Linux 5.1+ | 异步提交/完成、高吞吐潜力 | 状态机和生命周期重构大 | 不适合第一步迁移 |
| AF_XDP / DPDK | 现代高性能网络 | 极低延迟、高吞吐 | 运维、内核、网卡、协议栈成本极高 | 对 MMO 引擎一般过重，除非有极端网关场景 |

</div>

## 为什么不是“越现代越好”

游戏服务器网络模型的目标不是单一 QPS：

- Tick 稳定性比平均吞吐更重要。
- 尾延迟比 syscall benchmark 更重要。
- 状态一致性比 I/O 后端先进性更重要。
- 可调试性和运维稳定性比局部极限性能更重要。

BigWorld 的选择体现了当时商业 MMO 引擎的务实倾向：使用成熟内核能力，保持跨平台抽象稳定，把复杂度放在 Mercury 协议、实体模型和 Cell 负载治理上。

## 现代化建议

优先级建议：

1. 给 `PacketReceiver`、`PacketSender`、handler 分发、Channel 重传增加指标。
2. 统计 `maxSocketProcessingTime` 命中率、receive queue size、发送队列满等待次数。
3. 对 UDP 接收实验 `recvmmsg`，先作为 Linux 可选路径。
4. 对 UDP 发送实验 `sendmmsg`，观察 bundle 聚合是否能受益。
5. 再评估 `SO_REUSEPORT` 是否适合按 shard 分多个 UDP socket。
6. `io_uring` 只适合在确认 syscall 和 I/O wait 是主瓶颈后作为专项重构。

## 后续研究入口

下一批网络专题应继续深入：

- Mercury 可靠 UDP：ACK、重传、窗口、乱序、分片、piggyback。
- Bundle 与消息定义：宏接口、消息 ID、可变长流。
- UDP Channel：indexed、anonymous、irregular、once-off reliability。
- 网络故障注入：丢包、延迟、Flood、篡改测试。
