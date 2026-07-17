# 安全、限流与加密

<div class="arch-hero">

BigWorld 的安全模型不是现代零信任、服务网格或全链路 TLS 模型。它更像传统 MMO 引擎的分层防线：外部入口过滤、按地址限流、按客户端消息预算缓冲、Mercury Channel 加密、登录/初连特殊处理，以及依赖部署拓扑隔离内部组件。

</div>

## 先给结论

BigWorld 源码里能看到明确的安全与抗滥用设计，但它不是完整现代安全平台：

- `NetworkInterface` 提供 per-IP 和 per-IP:port rate limit。
- `PacketReceiver` 在进入 Mercury 解析前先执行地址限流。
- `BaseApp` 有按 tick 的客户端消息限速与缓冲回放机制。
- `InitialConnectionFilter` 对 BaseApp 外部接口的初连包做形状校验，只允许预期登录请求。
- `Channel::setEncryption()` 提供加密抽象，`UDPChannel` 会挂载 `EncryptionFilter`。
- `EncryptionFilter` 使用 BlockCipher，对包体加密并通过 magic 校验解密结果。

关键源码：

- `NetworkInterface` rate limit 配置在 [network_interface.hpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/network_interface.hpp:236)。
- `PacketReceiver::processPacket()` 首先调用限流检查，见 [packet_receiver.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/packet_receiver.cpp:434)。
- BaseApp 消息限速入口在 [rate_limit_message_filter.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/baseapp/rate_limit_message_filter.cpp:174)。
- 初始连接过滤在 [initial_connection_filter.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/baseapp/initial_connection_filter.cpp:23)。
- Channel 加密抽象在 [channel.hpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/channel.hpp:107)。
- UDP Channel 挂载加密 filter 在 [udp_channel.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/udp_channel.cpp:650)。
- 加密 filter 实现在 [encryption_filter.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/encryption_filter.cpp:51)。

## 防线分层

<MermaidDiagram title="BigWorld 外部流量防线">
flowchart TD
  A[External UDP Packet] --> B[NetworkInterface rate limit]
  B --> C{rate limited?}
  C -- yes --> D[drop before channel dispatch]
  C -- no --> E[find UDPChannel]
  E --> F{PacketFilter?}
  F -- encryption --> G[decrypt and validate magic]
  F -- initial connection --> H[allow only baseAppLogin shape]
  G --> I[Mercury message dispatch]
  H --> I
  I --> J[BaseApp RateLimitMessageFilter]
  J --> K{per tick budget}
  K -- enough --> L[dispatch now]
  K -- exceeded --> M[buffer or drop]
</MermaidDiagram>

这个分层很符合 MMO 服务器的现实需求：

- 在最早入口丢掉明显异常或超频来源。
- 在 Channel 层处理加密/解密、握手后的通信。
- 在业务入口控制客户端每 tick 可执行消息量，避免单个客户端拖垮 BaseApp。
- 对初始连接做更严格过滤，因为初连阶段还没有稳定 Channel 上下文。

## 地址级限流

`NetworkInterface` 保存了限流周期和两个粒度的计数：

- `rateLimitPerIPAddress_`
- `rateLimitedIPAddresses_`
- `rateLimitPerIPAddressPort_`
- `rateLimitedIPAddressPorts_`

公开接口包括：

- `rateLimitPeriod()`
- `perIPAddressRateLimit()`
- `perIPAddressPortRateLimit()`
- `incrementAndCheckRateLimit()`

源码见 [network_interface.hpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/network_interface.hpp:238)。

`PacketReceiver::processPacket()` 的第一步就是：

```cpp
if (networkInterface_.incrementAndCheckRateLimit( addr ))
{
    return Mercury::REASON_GENERAL_NETWORK;
}
```

源码见 [packet_receiver.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/packet_receiver.cpp:437)。

设计含义：

- 限流发生在包进入 Mercury 消息分派前。
- 这是网络入口保护，不依赖业务 handler。
- 按 IP 和 IP:port 两个维度可以同时处理 NAT 后多客户端与单 endpoint flood。

局限：

- 它不是分布式限流，单进程只看自己收到的包。
- 没有看到类似 token bucket 的复杂策略，粒度更粗。
- 如果前面有 L4 负载均衡或多 UDP socket，限流状态需要重新设计。

## BaseApp 消息限速

网络层限流解决“包太多”，BaseApp 的 `RateLimitMessageFilter` 解决“单客户端业务消息太多”。

`RateLimitConfig` 暴露两组指标：

- 每秒：`warnMessagesPerSecond`、`maxMessagesPerSecond`、`warnBytesPerSecond`、`maxBytesPerSecond`
- 每 tick：`warnMessagesPerTick`、`maxMessagesPerTick`、`warnBytesPerTick`、`maxBytesPerTick`
- 缓冲：`warnMessagesBuffered`、`maxMessagesBuffered`、`warnBytesBuffered`、`maxBytesBuffered`

源码见 [rate_limit_config.hpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/baseapp/rate_limit_config.hpp:15)。

`RateLimitMessageFilter::filterMessage()` 的策略是：

1. 确认消息来源地址仍属于当前 Proxy。
2. 更新接收消息数与字节数。
3. 如果当前队列为空且预算允许，立即 dispatch。
4. 否则复制消息到 `BufferedMessage` 队列。
5. 如果缓冲上限也超出，则调用 `Proxy::onFilterLimitsExceeded()` 并删除消息。

源码见 [rate_limit_message_filter.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/baseapp/rate_limit_message_filter.cpp:174)。

`tick()` 每 tick 会：

- 检查接收数量和字节数是否超过 warn 阈值。
- 调用 `replayAny()` 回放可执行的缓冲消息。
- 重置本 tick 统计。

源码见 [rate_limit_message_filter.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/baseapp/rate_limit_message_filter.cpp:227)。

这不是简单丢包，而是“预算内顺序回放”：

- 对瞬时突发更友好。
- 避免客户端单 tick 执行过多外部调用。
- 保持 Proxy 上下文，回放时检查当前 proxy 是否变化，见 [rate_limit_message_filter.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/baseapp/rate_limit_message_filter.cpp:421)。

## 初始连接过滤

BaseApp 外部接口在还没有完整 Channel 状态前，最容易被噪声和恶意包打扰。`InitialConnectionFilter::recv()` 明确只允许一种包形状：

- flags 必须是 `FLAG_HAS_REQUESTS`
- 包总长度必须匹配 `baseAppLogin` 请求结构
- message id 必须是 `BaseAppExtInterface::baseAppLogin`

不满足则 drop，源码见 [initial_connection_filter.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/baseapp/initial_connection_filter.cpp:27)。

设计含义：

- 初连阶段采用白名单，而不是把所有外部包都交给通用解析。
- 降低畸形包进入更深层解析的机会。
- 这类过滤对于 UDP 协议尤其重要，因为 UDP 没有 TCP 握手天然屏障。

## Channel 加密模型

`Channel` 把加密定义为抽象能力：

```cpp
virtual void setEncryption( Mercury::BlockCipherPtr pBlockCipher ) = 0;
```

源码见 [channel.hpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/channel.hpp:107)。

`UDPChannel::setEncryption()` 在有 cipher 时创建 `EncryptionFilter`，否则清空 filter。见 [udp_channel.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/udp_channel.cpp:650)。

`PacketFilter` 本身是可插拔接口：

- `send()` 可以在发送前处理 Packet。
- `recv()` 可以在接收后、Mercury 解析前处理 Packet。
- `maxSpareSize()` 告诉 Bundle 预留额外空间。

源码见 [packet_filter.hpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/packet_filter.hpp:21)。

`EncryptionFilter::send()` 会：

- 检查 key 是否为空。
- 创建新 Packet，避免原地破坏输入。
- 补齐 block size padding。
- 追加 `ENCRYPTION_MAGIC`。
- 加密后调用基础 `PacketFilter::send()`。

源码见 [encryption_filter.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/encryption_filter.cpp:51)。

`EncryptionFilter::recv()` 会：

- 检查 key。
- 原地解密。
- 检查 magic。
- 校验 padding。
- 调整解密后的包长度。

源码见 [encryption_filter.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/encryption_filter.cpp:119)。

## 这不是现代 TLS

源码能证明 BigWorld 有加密 filter，但不能把它等同于现代 TLS/QUIC 安全模型。

需要区分：

- PacketFilter 加密保护的是 Mercury Channel 包体。
- TLS 同时包含证书、握手、密钥协商、完整性、重放防护、版本协商和大量安全工程细节。
- BigWorld 的内部组件通信很大程度上依赖可信网络拓扑。
- 现代公网入口还需要 DDoS 清洗、WAF/网关、bot 风控、账号风控和审计链路。

所以现代化时不能只问“有没有加密”，要问：

- 密钥从哪里来？
- 握手是否防中间人？
- 是否有认证绑定？
- 是否防重放？
- 内部 RPC 是否有认证和授权？
- Watcher/管理接口是否暴露在不可信网络？

## 当时为什么这样选

高置信工程判断：

- MMO 服务端常部署在受控机房内，内部组件默认在可信网络。
- 客户端外部入口是最主要攻击面，所以 BaseApp/LoginApp 侧防线更明显。
- UDP 游戏协议需要轻量加密，直接套 TLS 在当时成本和可用性都不理想。
- 业务消息按 tick 限速比普通 HTTP QPS 限流更适合游戏服务器。
- PacketFilter 机制能在不改变 Mercury 主协议的情况下接入加密或过滤逻辑，符合 KISS。

代价：

- 安全能力分散在网络、BaseApp、LoginApp、部署约定里，不是统一策略平面。
- 管理接口和 Watcher 如果暴露不当，风险很高。
- 加密算法、密钥交换、依赖库版本都需要现代审计。
- 只靠 IP 限流对 NAT、代理、云网络和大规模攻击都不够。

## 现代方案对比

<div class="decision-table">

| 维度 | BigWorld 方案 | 现代常见方案 | 判断 |
| --- | --- | --- | --- |
| 包入口限流 | per-IP / per-IP:port | 网关限流、eBPF、DDoS 清洗 | 保留进程内限流，但前置网络防线 |
| 客户端消息预算 | 每 tick 消息/字节预算 | actor mailbox quota、令牌桶、动作冷却 | BigWorld 思路仍然正确 |
| UDP 加密 | PacketFilter + BlockCipher | DTLS、QUIC、Noise、自研握手 | 需审计密钥协商和完整性 |
| 初连过滤 | baseAppLogin 白名单 | gateway handshake、challenge、bot 风控 | 白名单思想可保留 |
| 内部通信安全 | 默认可信网络 | mTLS、ACL、service identity | 现代部署必须补认证授权 |
| 管理面 | Watcher/工具协议 | RBAC、审计、只读指标导出 | Watcher 写能力需严格隔离 |

</div>

## 现代化建议

优先级建议：

1. 盘点所有外部 interface、Watcher 端口、工具端口和管理命令。
2. 把 Watcher 写能力和高危管理命令默认限制在管理网段。
3. 给 `NetworkInterface` 限流命中、BaseApp 消息缓冲、丢弃原因增加指标。
4. 对加密算法、密钥交换、OpenSSL 版本做专项审计。
5. 如果保留 Mercury UDP，优先补 challenge/握手和重放防护，再考虑替换 I/O 后端。
6. 将公网入口前置到专用 gateway，BigWorld 内部进程只暴露必要端口。
7. 对客户端消息建立 schema 级校验和 fuzz 测试，尤其是 BinaryStream 反序列化边界。

## 本章边界

本章只分析安全、限流和加密。下一章分析 Watcher、Profiler 和日志：这些设施是理解 BigWorld 运行时状态的关键，但它们本身也会影响安全边界。
