# 网络背压与故障注入

<div class="arch-hero">

BigWorld 的网络层不仅是 epoll + UDP。真正影响游戏服务器稳定性的，是接收预算、发送队列满、人工丢包/延迟、Channel overflow、可靠重发、rate limit 和 tick 公平性如何一起工作。这章专门分析背压和故障注入。

</div>

## 先给结论

BigWorld 网络层已经有多处“保护主循环”的机制：

- `PacketReceiver` 一次 socket 通知处理多个包，但可用 `maxSocketProcessingTime` 限制本轮处理时长。
- `BWMessageForwarder` 默认可把最大 socket 处理时间设为 `1 / gameUpdateHertz`。
- `PacketSender` 支持人工丢包和人工延迟，用于测试可靠 UDP。
- 发送遇到 `EAGAIN` / `ENOBUFS` 时，会用 `select()` 等待 10ms 再重试。
- 网络接口暴露 artificial loss、latency 和 max socket processing time 到 Watcher。
- 网络单测大量使用 `dropNextSend()` 验证可靠层。

关键源码：

- 接收预算在 [packet_receiver.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/packet_receiver.cpp:83)。
- `maxSocketProcessingTime()` watcher 在 [packet_receiver.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/packet_receiver.cpp:1367)。
- `BWMessageForwarder` 配置 socket 处理预算在 [bw_message_forwarder.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/bw_message_forwarder.cpp:47)。
- 人工丢包/延迟在 [packet_sender.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/packet_sender.cpp:137)。
- 发送队列满等待在 [packet_sender.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/packet_sender.cpp:304)。
- 逐包 `sendto()` 在 [packet_sender.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/packet_sender.cpp:347)。
- 网络测试中使用 `dropNextSend()`，例如 [test_reliable.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/unit_test/test_reliable.cpp:415)。

## 接收侧背压

`PacketReceiver::handleInputNotification()` 在收到 socket 可读通知后循环调用 `processSocket()`：

1. 记录开始时间。
2. 循环处理 socket 中的包。
3. 每处理一轮计算 elapsed。
4. 如果超过 `maxSocketProcessingTimeStamps_`，打印 warning 并停止本轮 socket 处理。

源码见 [packet_receiver.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/packet_receiver.cpp:83)。

这不是内核级背压，而是主线程公平性保护：

- 防止一次 UDP burst 把 Timer、FrequentTask、脚本和其他消息饿死。
- 允许 receive queue 留到下一轮处理。
- warning 中会打印最后来源地址、处理耗时、包数和 receive queue size。

<MermaidDiagram title="接收预算">
flowchart TD
  A[EPOLLIN] --> B[handleInputNotification]
  B --> C[processSocket one packet/batch path]
  C --> D{socket has more?}
  D -- no --> E[return to dispatcher]
  D -- yes --> F{elapsed > maxSocketProcessingTime?}
  F -- no --> C
  F -- yes --> G[warn + stop current socket processing]
  G --> E
</MermaidDiagram>

## 与 Tick 的关系

`BWMessageForwarder` 读取：

- `maxInternalSocketProcessingTime`
- `gameUpdateHertz`

如果配置值小于 0，则设置为 `1.f / gameUpdateHertz`，然后调用 `networkInterface.maxSocketProcessingTime()`。源码见 [bw_message_forwarder.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/bw_message_forwarder.cpp:47)。

这说明接收预算不是孤立参数，它和游戏 tick 频率绑定。

设计含义：

- 如果 `gameUpdateHertz = 10`，一个 tick 是 100ms。
- 内部 socket 单轮处理预算可按 tick 长度推导。
- 高峰网络包不会无限吃掉主循环。

源码观测应把这个指标拆成：

- 每 tick 网络处理耗时。
- 每次 socket notification 处理包数。
- 超预算次数。
- receive queue size。
- handler 分发耗时。

## 发送侧背压

发送路径使用 `sendto()`：

```cpp
int len = socket_.sendto( pPacket->data(), pPacket->totalSize(), addr.port, addr.ip );
```

源码见 [packet_sender.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/packet_sender.cpp:350)。

错误映射：

- `ECONNREFUSED` -> `REASON_NO_SUCH_PORT`
- `EAGAIN` -> `REASON_RESOURCE_UNAVAILABLE`
- `ENOBUFS` -> `REASON_TRANSMIT_QUEUE_FULL`
- `EMSGSIZE` -> `REASON_MESSAGE_TOO_LONG`

源码见 [packet_sender.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/packet_sender.cpp:372)。

遇到 `REASON_RESOURCE_UNAVAILABLE` 或 `REASON_TRANSMIT_QUEUE_FULL` 时，`basicSendWithRetries()` 会：

- 打印 `Transmit queue full` warning。
- 用 `select()` 等待 socket 可写 10ms。
- 然后继续重试。

源码见 [packet_sender.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/packet_sender.cpp:304)。

这是发送侧背压的源码取舍：

- 简单稳妥。
- 能处理短暂 kernel transmit queue 满。
- 但会在发送路径引入最多 10ms 等待，可能拖慢主线程。

应优先统计这条路径出现频率和等待耗时，再判断发送侧背压是否是主线程抖动来源。

## 人工丢包与延迟

`PacketSender::rescheduleSend()` 支持两类故障注入：

- `dropNextSend()` 或按 `dropPerMillion()` 概率人工丢包。
- 按 `minLatencyMillion()` / `maxLatencyMillion()` 人工延迟发送。

源码见 [packet_sender.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/packet_sender.cpp:158)。

如果延迟大于等于 2ms，会创建 `RescheduledSender` 稍后发送。见 [packet_sender.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/packet_sender.cpp:221)。

设计价值：

- 可测试 Mercury 可靠 UDP 重传。
- 可模拟公网延迟和丢包。
- 可验证 ACK、piggyback、fragment、receive window。
- 可在不依赖外部 netem 的情况下做单元测试。

网络单测中可以看到大量 `dropNextSend()` 用法，例如可靠层测试见 [test_reliable.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/unit_test/test_reliable.cpp:415)。

## 故障注入与测试的边界

BigWorld 的人工故障注入偏网络层：

- 丢下一包。
- 按概率丢包。
- 延迟包。
- flood/mangle/fragment 等单测覆盖。

但它不是完整 chaos framework：

- 不能直接模拟 DBApp 卡死。
- 不能完整模拟 clock jump。
- 不能自动验证 CellApp death 后实体恢复。
- 不能跨多进程注入部分网络分区。
- 缺少统一 scenario 描述和自动报告。

所以它很适合可靠 UDP 单元测试，但不等于现代混沌测试平台。

## 背压链路

<MermaidDiagram title="网络背压链路">
flowchart LR
  A[Kernel RX Queue] --> B[PacketReceiver budget]
  B --> C[Mercury decode]
  C --> D[Channel reliable state]
  D --> E[Message handler]
  E --> F[Python/Entity logic]
  F --> G[Bundle output]
  G --> H[PacketSender]
  H --> I{sendto result}
  I -- success --> J[Kernel TX Queue]
  I -- EAGAIN/ENOBUFS --> K[select wait 10ms retry]
  I -- EMSGSIZE --> L[MTU/message too long]
  H --> M[artificial drop/latency]
</MermaidDiagram>

这条链说明：真正的背压不只是 socket。

可能瓶颈包括：

- Kernel receive queue。
- 单轮 socket 处理预算。
- Mercury 包解析。
- 可靠层重发和 ACK。
- handler 执行。
- Python 脚本。
- Entity/AOI 更新。
- Bundle 聚合。
- Kernel transmit queue。

必须先观测每一段，否则容易把所有问题都归因于 epoll 或 io_uring。

## 为什么没有 io_uring 背压模型

BigWorld 设计年代没有 `io_uring`。更关键的是，当前模型是 readiness + 同步 handler：

- epoll 通知可读。
- 主线程同步处理包。
- handler 立即执行。
- Timer 和 network 在同一 dispatcher 中调度。

`io_uring` 是 completion 模型，引入：

- submit queue。
- completion queue。
- buffer 生命周期。
- 多 outstanding I/O。
- 完成事件与 handler 调度解耦。

这会改变执行模型，不只是替换 `recvfrom()`。

## 源码取舍

背压和故障注入的源码取舍是把网络压力显式暴露到主循环：

- `PacketReceiver::maxSocketProcessingTime` 限制一次可读事件处理时间，保护 Tick 公平性。
- `PacketSender::basicSendWithRetries()` 在发送失败时有重试和短等待路径，但仍可能占用主线程。
- artificial loss/latency、`dropNextSend()` 等测试钩子让可靠层可复现丢包和乱序。
- Channel overflow 配置把可靠窗口压力转成可观测/可处理状态。

源码代价：

- 发送队列满路径仍可能让主线程等待。
- 故障注入主要在引擎测试钩子内，不能覆盖所有真实网络抖动。
- 接收预算只能限制单次 socket 处理，不限制 handler 内部业务耗时。
- I/O 后端变化会牵动 buffer 生命周期和 handler 调度，不只是替换系统调用。

## 源码验证重点

背压和故障注入测试应覆盖网络压力传播：

- `PacketReceiver::handleInputNotification()` 应记录处理包数、耗时和超预算退出。
- `PacketSender::basicSendWithRetries()` 应覆盖 EAGAIN、ENOBUFS、select 等待和最终失败。
- `dropNextSend()`、artificial loss/latency 应能复现 ACK 丢失、数据包丢失、回复丢失和乱序。
- Channel overflow 达到阈值后应按配置丢弃、阻塞或报错，不能静默损坏可靠状态。
- 登录 flood、畸形包、超大 Bundle 和非法 message id 应在进入重业务逻辑前被拦截。
- 故障注入场景应同时覆盖 external client channel 和 internal server channel。

## 本章边界

本章分析网络背压和故障注入。它与 [网络 I/O 模型选择](/architecture/network-io-model) 互补：前者解释选择 epoll/recvfrom 的模型，本文解释运行时如何保护 Tick 和验证可靠层。
