# Mercury 可靠 UDP

<div class="arch-hero">

BigWorld 的网络核心不是“epoll + socket”这么薄的一层，而是 Mercury 在 UDP 之上实现的游戏协议运行时。它把消息聚合、可靠/不可靠语义、ACK、重发、乱序窗口、分片、piggyback、indexed channel 和实体迁移版本号放在同一个 Channel 模型里。

</div>

## 先给结论

Mercury 可靠 UDP 解决的不是普通 RPC 的可靠投递问题，而是 MMO 高频状态同步中的选择性可靠问题：

- 移动、AOI、属性同步等消息需要低延迟，很多数据过期后不值得重传。
- 登录、创建实体、跨进程迁移、关键生命周期消息必须可靠。
- 同一个 UDP 包里可以混合可靠消息和不可靠消息。
- 外部客户端链路带宽更紧，丢包时只重发可靠数据，不可靠数据会被剥离或丢弃。
- 内部服务器链路默认低延迟、高带宽、低丢包，可以采用更激进的可靠策略。

源码入口：

- `UDPChannel::Traits` 明确区分 `INTERNAL` 与 `EXTERNAL`，见 [udp_channel.hpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/udp_channel.hpp:57)。
- 可靠机制入口包括 `addResendTimer()`、`handleCumulativeAck()`、`handleAck()`、`checkResendTimers()`、`resend()`，见 [udp_channel.hpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/udp_channel.hpp:151)。
- 发送窗口状态在 `smallOutSeqAt_`、`largeOutSeqAt_`、`oldestUnackedSeq_`、`unackedPackets_`，见 [udp_channel.hpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/udp_channel.hpp:383)。
- 接收窗口状态在 `inSeqAt_`、`bufferedReceives_`、`pFragments_`、`highestAck_`，见 [udp_channel.hpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/udp_channel.hpp:440)。

## 为什么不用纯 TCP

TCP 提供字节流可靠有序传输，但游戏状态同步不总是需要“所有历史状态按顺序到达”。

BigWorld 需要的能力更接近：

<div class="decision-grid">
  <div class="decision-card">
    <h3>可靠消息</h3>
    <p>实体创建、销毁、迁移、请求/回复、关键生命周期事件必须到达。</p>
  </div>
  <div class="decision-card">
    <h3>不可靠消息</h3>
    <p>位置、朝向、部分 AOI 更新过期后没有重传价值，新状态覆盖旧状态。</p>
  </div>
  <div class="decision-card">
    <h3>混合 Bundle</h3>
    <p>同一个发送批次内同时携带可靠和不可靠消息，降低包头和 syscall 开销。</p>
  </div>
  <div class="decision-card">
    <h3>实体迁移</h3>
    <p>Channel 不只是网络连接，还要承载 Base/Cell 实体 offload 的版本语义。</p>
  </div>
</div>

如果全部使用 TCP，会获得简单可靠流，但会付出这些代价：

- head-of-line blocking 会让旧可靠数据阻塞后续新状态。
- 无法天然表达“这条消息可靠，那条消息不可靠”。
- 多条逻辑流要么多 TCP 连接，要么在应用层重新多路复用。
- MMO 状态同步常常需要按消息语义而不是按连接统一重传。

这也是当时很多游戏服务器选择 UDP + 自研可靠层的根本原因。

## Channel 分类

`UDPChannel::Traits` 的注释已经给出核心设计判断：

- `INTERNAL`：服务器到服务器，低延迟、高带宽、低丢包。
- `EXTERNAL`：客户端到服务器，高延迟、低带宽、高丢包。
- 外部通道只重发可靠数据，不可靠数据从丢包中剥离并丢弃。

这不是简单的网络类型标签，而是重发策略、带宽策略和包内容裁剪策略的输入。

<MermaidDiagram title="Mercury Channel 语义">
flowchart TD
  A[UDP Socket] --> B[NetworkInterface]
  B --> C[UDPChannel]
  C --> D{Traits}
  D -- INTERNAL --> E[Server-Server Channel]
  D -- EXTERNAL --> F[Client-Server Channel]
  E --> G[可靠与不可靠消息都更偏向保留吞吐]
  F --> H[重发可靠消息 剥离不可靠负载]
  C --> I[Bundle 消息聚合]
  I --> J[ReliableOrder]
  I --> K[Piggyback]
</MermaidDiagram>

## 可靠等级

可靠性不是 bool。`Bundle` 定义了四种可靠类型，见 [bundle.hpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/bundle.hpp:27)：

<div class="decision-table">

| 类型 | 含义 | 设计目的 |
| --- | --- | --- |
| `RELIABLE_NO` | 不可靠消息 | 允许丢弃，适合过期即无价值的数据 |
| `RELIABLE_DRIVER` | 驱动可靠包 | 让当前 UDPBundle 具备可靠投递基础 |
| `RELIABLE_PASSENGER` | 搭车可靠消息 | 只有同 Bundle 有 driver 时才可靠发送 |
| `RELIABLE_CRITICAL` | 关键可靠消息 | 等价 driver，并把 Bundle 标记为 critical |

</div>

这个设计的重点是“按消息和 Bundle 组合表达可靠性”。它比 TCP 的连接级可靠更细，也比简单 UDP RPC 的 per-message ACK 更贴近游戏协议。

`RELIABLE_CRITICAL` 会影响 `UDPChannel` 内的 `unackedCriticalSeq_`，源码注释说明 critical 的语义由应用层控制，见 [udp_channel.hpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/udp_channel.hpp:485)。

## 发送窗口

发送窗口由这些状态共同维护：

- `smallOutSeqAt_`：通常是下一个要发送的序号，不包含 overflow packets。
- `largeOutSeqAt_`：包含 overflow packets。
- `oldestUnackedSeq_`：最早未 ACK 的包。
- `unackedPackets_`：保存未确认包与可靠消息顺序。
- `windowSize_` 与 `maxWindowSize()`：控制发送窗口和溢出边界。

源码见 [udp_channel.hpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/udp_channel.hpp:383)。

这说明 Mercury 不是“发出去后等回调”的简单模型，而是维护自己的滑动窗口和未确认包集合。

## 接收窗口

接收路径的关键函数是 `UDPChannel::addToReceiveWindow()`，见 [udp_channel.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/udp_channel.cpp:1208)。

它做几件事：

1. 校验 sequence number 是否在合法范围。
2. 校验源地址；如果允许 `shouldAutoSwitchToSrcAddr_`，根据 channel version 切换地址。
3. 对非 piggyback 包加入 `acksToSend_`。
4. 如果 `seq == inSeqAt_`，推进接收窗口，并把已缓存的连续乱序包串起来。
5. 如果 `seq < inSeqAt_`，判为重复包。
6. 如果距离窗口太远，判为窗口外或异常。
7. 合法乱序包进入 `bufferedReceives_`。

<MermaidDiagram title="接收窗口状态机">
stateDiagram-v2
  [*] --> ValidateSeq
  ValidateSeq --> Corrupt: seq 非法
  ValidateSeq --> CheckAddress: seq 合法
  CheckAddress --> Corrupt: 源地址错误
  CheckAddress --> AckQueued: 地址合法
  AckQueued --> Next: seq == inSeqAt_
  AckQueued --> Duplicate: seq < inSeqAt_
  AckQueued --> Buffered: seq 在窗口内但乱序
  AckQueued --> OutOfWindow: seq 超过窗口
  Next --> AdvanceWindow
  AdvanceWindow --> AttachBuffered
  AttachBuffered --> [*]
  Duplicate --> [*]
  Buffered --> [*]
  OutOfWindow --> [*]
  Corrupt --> [*]
</MermaidDiagram>

这个状态机是可靠 UDP 的核心之一。它保证可靠消息能按 Channel 序号恢复顺序，同时允许短暂乱序进入缓冲。

## ACK 与 cumulative ACK

`writeFlags()` 和 `writeFooter()` 负责把 Channel 元数据写入 packet，源码见：

- [udp_channel.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/udp_channel.cpp:1513)
- [udp_channel.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/udp_channel.cpp:1580)

关键策略：

- 所有 channel 包写 `FLAG_ON_CHANNEL`。
- indexed channel 额外写 `FLAG_INDEXED_CHANNEL`、`ChannelID`、`ChannelVersion`。
- 如果空间足够，写 cumulative ACK，表达某个序号之前全部收到。
- 如果还有空间，写有限数量的离散 ACK。
- 第一个内部可靠包会写 `FLAG_CREATE_CHANNEL`。

这是一种很典型的“累计确认 + 选择性确认”混合策略：

- cumulative ACK 压缩连续确认范围。
- 离散 ACK 处理乱序洞。
- ACK piggyback 到出站包，减少独立 ACK 包。
- ACK 数量受 packet 空间限制，避免 ACK 把负载挤爆。

## 重发路径

`UDPChannel::resend()` 的策略不是机械重发原包，见 [udp_channel.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/udp_channel.cpp:1156)。

对于外部通道，如果未确认包不是 fragment，并且当前发送窗口不会溢出，会尝试把可靠内容 piggyback 到下一个 outgoing bundle：

<MermaidDiagram title="重发策略">
flowchart TD
  A[unacked packet timeout or ack hole] --> B{External channel?}
  B -- no --> E[sendUnacked 原包重发]
  B -- yes --> C{非 fragment 且窗口可容纳?}
  C -- yes --> D[piggyback 到新 Bundle]
  C -- no --> E
  D --> F[handleAck 原序号]
  E --> G[sendPacket isResend=true]
</MermaidDiagram>

这体现了外部客户端链路的带宽优先策略：能搭车就不单独重发，能只重发可靠内容就不重复发送已经过期的不可靠数据。

## Indexed Channel

`UDPChannel` 支持 indexed channel，源码注释见 [udp_channel.hpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/udp_channel.hpp:364)。

indexed channel 的作用是：在同一对地址之间复用多个逻辑 channel。典型场景是 Base 和 Cell 实体之间需要多个独立流，仅靠地址无法区分。

它包含两个关键字段：

- `ChannelID id_`：逻辑 channel 标识。
- `ChannelVersion version_`：记录 indexed channel 被 offload 的次数，用于识别过期包和恢复场景下的最新实体信息。

这点对理解实体迁移非常重要。BigWorld 的网络连接不是纯传输概念，它嵌入了实体迁移和恢复语义。

## Regular 与 Irregular

`isLocalRegular_` 和 `isRemoteRegular_` 会影响 ACK 与重发策略，源码见 [udp_channel.hpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/udp_channel.hpp:462)。

含义：

- local irregular 时，本地会定期检查重发，并更倾向立即发送 ACK。
- remote regular 时，可以基于 ACK 洞推断丢包，而不是只靠静默超时。

这说明 Mercury 的可靠层利用了游戏服务器通信模式：有些 Channel 会持续发包，有些 Channel 是间歇发包。不同流量形态应使用不同 ACK/重发策略。

## 与 Bundle 的关系

`UDPChannel::newBundle()` 创建 `UDPBundle`，见 [udp_channel.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/udp_channel.cpp:1501)。

`UDPBundle` 负责：

- `startMessage()`、`startRequest()`、`startReply()`。
- 记录 `ReliableOrder`。
- `preparePackets()` 将消息序列切成 UDP packet。
- `piggyback()` 把重发内容挂到新包。
- `ack_` 处理 off-channel ack。

源码见 [udp_bundle.hpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/udp_bundle.hpp:37)。

所以 Channel 管连接状态和可靠窗口，Bundle 管消息聚合与 packet 编排。两者合起来才是 Mercury 的游戏网络协议。

## 当时方案取舍

BigWorld 选择自研可靠 UDP，在当时有充分工程理由：

- ENet、RakNet 等库虽然存在，但商业 MMO 引擎需要深度绑定 Entity、AOI、Base/Cell 迁移和内部服务协议。
- TCP 无法表达同一逻辑流内可靠/不可靠混合语义。
- QUIC 尚未成熟，不在当时技术选项中。
- 服务器内部多进程 MMO 通信需要可控协议头、可观测统计和引擎级调试能力。
- Python 脚本层和 EntityDef 类型系统需要与网络协议紧密结合。

代价同样明确：

- 协议复杂度高，正确性依赖大量边界测试。
- 拥塞控制、路径 MTU、NAT、加密、抗攻击能力弱于现代成熟协议栈。
- 与通用生态脱节，外部工具难以直接解析。
- 现代平台上的批量收发、内核 offload、QUIC/TLS 生态无法直接复用。

## 现代方案对比

<div class="decision-table">

| 方案 | 优点 | 代价 | 对 BigWorld 的判断 |
| --- | --- | --- | --- |
| TCP | 成熟、可靠、有序、生态强 | 队头阻塞，可靠粒度太粗 | 适合工具、登录、管理，不适合主状态同步 |
| 自研 UDP | 语义可完全贴合游戏 | 正确性和运维成本高 | 当前实际方案，需补测试和观测 |
| ENet/KCP | 可靠 UDP 成熟组件 | 与 Entity/Channel 语义不完全匹配 | 可作为对照，不宜直接替换 |
| RakNet | 游戏网络功能丰富 | 维护状态和授权生态需评估 | 可借鉴接口，不是低成本迁移 |
| QUIC | 多路复用、加密、拥塞控制成熟 | 协议栈重，可靠流语义仍需映射 | 适合外部网关实验，不适合直接替换全部 Mercury |
| io_uring UDP | 提升异步 I/O 和批量潜力 | 不解决可靠协议语义 | 是 I/O 后端问题，不是 Channel 替代品 |
| AF_XDP/DPDK | 极致吞吐和低延迟 | 运维、网卡、内核旁路成本极高 | MMO 常规服过重，网关专项才考虑 |

</div>

## 测试重点

可靠 UDP 的测试不能只测“能收发”。至少要覆盖：

- 连续 ACK、离散 ACK、ACK 丢失、ACK 延迟。
- 乱序包、重复包、窗口外包、损坏包。
- fragment 包重发与 piggyback 的边界。
- `INTERNAL` 与 `EXTERNAL` 重发策略差异。
- indexed channel 的 version 过期包丢弃。
- `RELIABLE_CRITICAL` 未确认时的恢复行为。
- 发送窗口溢出和 `maxWindowSize()` 边界。

源码已经有网络单元测试目录，例如 `lib/network/unit_test/test_receive_window.cpp`、`test_reliable.cpp`、`test_fragment.cpp`、`test_channel_version.cpp`。后续测试章节会专门分析覆盖范围与缺口。

## 现代化建议

优先级不应是“马上换 QUIC”：

1. 先给 ACK、重发、窗口占用、piggyback 命中率、fragment 重发增加指标。
2. 对 `INTERNAL` 和 `EXTERNAL` 分开统计丢包、RTT、重发次数和窗口积压。
3. 用故障注入复现乱序、延迟、丢 ACK、重复包、版本过期包。
4. 对外部客户端链路评估 KCP/QUIC 网关方案，但不要直接穿透 Entity 层。
5. 对内部服务器链路优先实验批量 UDP 收发，而不是替换可靠协议。
6. 任何协议替换都必须先证明 Entity 迁移、Channel version、critical reliable 语义可等价表达。

## 本章边界

本章只解释 Channel 可靠层。下一章继续分析 Mercury 的通信抽象：`InterfaceElement` 如何定义消息头，`Bundle` 如何组织 RPC，请求/回复如何与可靠层衔接。
