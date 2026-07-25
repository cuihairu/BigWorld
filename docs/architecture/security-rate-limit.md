# 安全、限流与加密

<div class="arch-hero">

BigWorld 的安全相关逻辑分布在网络入口、登录流程、Proxy 消息预算、Mercury Channel 加密、EntityDef 暴露方法和管理工具链里。源码分析这部分时，必须按组件边界拆开看，不能只看玩家 UDP 包入口。

</div>

## 先给结论

BigWorld 源码里能看到明确的安全与抗滥用设计：

- `NetworkInterface` 提供 per-IP 和 per-IP:port rate limit。
- `PacketReceiver` 在进入 Mercury 解析前先执行地址限流。
- `BaseApp` 有按 tick 的客户端消息限速与缓冲回放机制。
- `InitialConnectionFilter` 对 BaseApp 外部接口的初连包做形状校验，只允许预期登录请求。
- `LoginApp` 登录入口有独立防线：登录开关、IP ban、协议版本、pending/cache 重试、登录速率、DB ready、系统过载、challenge、消息长度和登录参数解码。
- `LoginChallenge` 支持 `delay`、`fail`、`cuckoo_cycle`，其中 `cuckoo_cycle` 是登录抗滥用 proof-of-work，不是账号鉴权本身。
- `Channel::setEncryption()` 提供加密抽象，`UDPChannel` 会挂载 `EncryptionFilter`。
- `EncryptionFilter` 使用 BlockCipher，对包体加密并通过 magic 校验解密结果。
- 客户端调用服务端实体方法不是任意 RPC，只能落到 `.def` 暴露出的 Base/Cell exposed method range，并且 `OWN_CLIENT` 有 source entity 校验。
- Watcher、probe、Message Logger 等管理面能力也属于安全边界，不能只分析玩家 UDP 入口。

关键源码：

- `NetworkInterface` rate limit 配置在 [network_interface.hpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/network_interface.hpp:236)。
- `PacketReceiver::processPacket()` 首先调用限流检查，见 [packet_receiver.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/packet_receiver.cpp:434)。
- BaseApp 消息限速入口在 [rate_limit_message_filter.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/baseapp/rate_limit_message_filter.cpp:174)。
- 初始连接过滤在 [initial_connection_filter.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/baseapp/initial_connection_filter.cpp:23)。
- LoginApp 配置项在 [loginapp_config.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/loginapp/loginapp_config.cpp:17)。
- LoginApp 登录主流程在 [loginapp.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/loginapp/loginapp.cpp:709)。
- LoginChallenge 处理在 [loginapp.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/loginapp/loginapp.cpp:1035)。
- Exposed 方法入口在 [proxy.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/baseapp/proxy.cpp:2491) 和 [entity.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/entity.cpp:5399)。
- Channel 加密抽象在 [channel.hpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/channel.hpp:107)。
- UDP Channel 挂载加密 filter 在 [udp_channel.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/udp_channel.cpp:650)。
- 加密 filter 实现在 [encryption_filter.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/encryption_filter.cpp:51)。

## 防线分层

<MermaidDiagram title="BigWorld 外部流量防线">
flowchart TD
  A[External UDP Packet] --> B[NetworkInterface rate limit]
  B --> C{rate limited?}
  C -- yes --> D[drop before channel dispatch]
  C -- no --> E{external entry}
  E -- LoginApp --> L0[allowLogin / IP ban / protocol]
  L0 --> L1[loginRateLimit / DB ready / overload]
  L1 --> L2[LoginChallenge / LogOnParams decode / length]
  L2 --> L3[DBApp logOn]
  E -- BaseApp --> F[find UDPChannel]
  F --> G{PacketFilter?}
  G -- encryption --> H[decrypt and validate magic]
  G -- initial connection --> I[allow only baseAppLogin shape]
  H --> J[Mercury message dispatch]
  I --> J
  J --> K[BaseApp RateLimitMessageFilter]
  K --> M{per tick budget}
  M -- enough --> N[dispatch now]
  M -- exceeded --> O[buffer or drop]
</MermaidDiagram>

这个分层很符合 MMO 服务器的现实需求：

- 在最早入口丢掉明显异常或超频来源。
- LoginApp 在账号验证前先做协议、限流、challenge 和消息大小约束。
- 在 Channel 层处理加密/解密、握手后的通信。
- 在业务入口控制客户端每 tick 可执行消息量，避免单个客户端拖垮 BaseApp。
- 对初始连接做更严格过滤，因为初连阶段还没有稳定 Channel 上下文。

## 地址级限流

**概述：** `NetworkInterface` 提供 per-IP 和 per-IP:port 两级限流。这是网络入口的第一道防线，在进入 Mercury 解析前先执行地址限流。

**源码入口：** [network_interface.hpp:236](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/network_interface.hpp:236)

```cpp
// network_interface.hpp:236 - NetworkInterface 限流配置
class NetworkInterface
{
public:
    // 限流配置
    int rateLimitPeriod() const { return rateLimitPeriod_; }
    void rateLimitPeriod( int period ) { rateLimitPeriod_ = period; }
    
    int rateLimitPerIPAddress() const { return rateLimitPerIPAddress_; }
    void rateLimitPerIPAddress( int limit ) { rateLimitPerIPAddress_ = limit; }
    
    int rateLimitPerIPAddressPort() const { return rateLimitPerIPAddressPort_; }
    void rateLimitPerIPAddressPort( int limit ) { rateLimitPerIPAddressPort_ = limit; }
    
    // 限流检查
    bool isIPAddressLimited( const Mercury::Address & addr );
    bool isIPAddressPortLimited( const Mercury::Address & addr );
    
    // 重置计数
    void resetRateLimitCounts();
    
private:
    int rateLimitPeriod_;              // 限流周期（秒）
    int rateLimitPerIPAddress_;        // 每 IP 限制
    int rateLimitPerIPAddressPort_;    // 每 IP:Port 限制
    
    // 限流计数器
    typedef std::map< Mercury::Address, int > RateLimitMap;
    RateLimitMap rateLimitedIPAddresses_;
    RateLimitMap rateLimitedIPAddressPorts_;
};
```

**流程图：**

<MermaidDiagram title="地址级限流流程">
flowchart TD
    A[收到 UDP 包] --> B{检查 IP 限流}
    B -- 超限 --> C[丢弃]
    B -- 未超限 --> D{检查 IP:Port 限流}
    D -- 超限 --> C
    D -- 未超限 --> E[更新计数器]
    E --> F[继续处理]
    
    G[定时器触发] --> H[重置计数器]
```

**详细讲解：**

1. **两级限流**：
   - **IP 级**：限制同一 IP 的请求总数，防止单个 IP 发送过多请求
   - **IP:Port 级**：限制同一 IP:Port 的请求总数，防止同一客户端发送过多请求

2. **限流周期**：`rateLimitPeriod_` 定义计数器重置周期（秒），默认 1 秒

3. **限流阈值**：
   - `rateLimitPerIPAddress_`：每 IP 每周期最大请求数
   - `rateLimitPerIPAddressPort_`：每 IP:Port 每周期最大请求数

4. **计数器管理**：
   - `rateLimitedIPAddresses_`：存储每个 IP 的当前计数
   - `rateLimitedIPAddressPorts_`：存储每个 IP:Port 的当前计数
   - 定时器触发时重置计数器

5. **限流检查**：`isIPAddressLimited()` 和 `isIPAddressPortLimited()` 检查是否超限

### PacketReceiver 限流

**源码入口：** [packet_receiver.cpp:434](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/packet_receiver.cpp:434)

```cpp
// packet_receiver.cpp:434 - PacketReceiver 限流检查
bool PacketReceiver::processPacket( const Mercury::Address & addr,
    const Packet * pPacket )
{
    // 1. 检查 IP 限流
    if (pNetworkInterface_->isIPAddressLimited( addr ))
    {
        WARNING_MSG( "PacketReceiver::processPacket: "
            "IP %s rate limited\n", addr.ipAsString() );
        return false;
    }
    
    // 2. 检查 IP:Port 限流
    if (pNetworkInterface_->isIPAddressPortLimited( addr ))
    {
        WARNING_MSG( "PacketReceiver::processPacket: "
            "IP:Port %s rate limited\n", addr.ipAsString() );
        return false;
    }
    
    // 3. 更新限流计数
    pNetworkInterface_->updateRateLimitCounts( addr );
    
    // 4. 继续处理
    return this->processPacketInternal( addr, pPacket );
}
```

**关键细节：**

- 限流检查在 Mercury 解析前执行，减少无效处理
- 超限时直接丢弃，不进入 Channel 分发
- 计数器在检查通过后更新，确保准确性
- 日志记录限流事件，便于监控和调试

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

## LoginApp 登录入口安全链路

LoginApp 的入口不是“收到用户名密码后直接查库”。`LoginAppConfig` 定义了一组专门用于登录入口的安全开关和限制：

- `allowLogin`：总登录开关。
- `allowProbe` / `logProbes`：探测消息开关和日志。
- `allowUnencryptedLogins`：是否允许登录参数明文回退。
- `maxRepliesOnFailPerSecond`：失败回复速率。
- `loginRateLimit` / `rateLimitDuration`：全局登录速率窗口。
- `ipAddressRateLimit` / `ipAddressPortRateLimit`：外部接口地址级限流。
- `maxUsernameLength` / `maxPasswordLength` / `maxLoginMessageSize`：登录参数尺寸边界。
- `passwordlessLoginsOnly`：只允许无密码登录。
- `challengeType`：登录 challenge 类型。

源码见 [loginapp_config.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/loginapp/loginapp_config.cpp:17)。

`LoginApp::init()` 会把这些配置接到实际运行时：

- 生产模式下如果启用 `allowProbe`，会报配置错误，见 [loginapp.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/loginapp/loginapp.cpp:301)。
- 外部接口设置人工延迟、丢包和 `maxExternalSocketProcessingTime`，见 [loginapp.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/loginapp/loginapp.cpp:308)。
- 外部接口设置 `rateLimitPeriod`、`perIPAddressRateLimit`、`perIPAddressPortRateLimit`，见 [loginapp.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/loginapp/loginapp.cpp:333)。
- challenge factory 从配置初始化，并校验 `challengeType` 是否存在，见 [loginapp.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/loginapp/loginapp.cpp:340)。
- `challengeType` 被暴露为 watcher，可运行时观察或修改，见 [loginapp.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/loginapp/loginapp.cpp:356)。

`LoginApp::login()` 的实际顺序更能说明安全边界：

1. 如果 `allowLogin` 为 false，直接拒绝，见 [loginapp.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/loginapp/loginapp.cpp:709)。
2. 检查 IP ban，并周期清理过期 ban，见 [loginapp.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/loginapp/loginapp.cpp:725)。
3. 拒绝 `source.ip == 0` 的伪造空地址，见 [loginapp.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/loginapp/loginapp.cpp:766)。
4. 读取并校验 `ClientServerProtocolVersion`，协议不兼容返回 `LOGIN_BAD_PROTOCOL_VERSION`，见 [loginapp.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/loginapp/loginapp.cpp:784)。
5. 优先处理 pending 重试，避免 UDP 重发导致重复登录流程，见 [loginapp.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/loginapp/loginapp.cpp:823)。
6. 检查 `loginRateLimit` 全局预算，耗尽则返回 `LOGIN_REJECTED_RATE_LIMITED`，见 [loginapp.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/loginapp/loginapp.cpp:833)。
7. 检查 DB 是否 ready，以及系统是否处于 overload 状态，见 [loginapp.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/loginapp/loginapp.cpp:850)。
8. 如果配置了 challenge，先进入 `processForLoginChallenge()`，见 [loginapp.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/loginapp/loginapp.cpp:884)。
9. 检查剩余登录消息长度是否超过 `maxLoginMessageSize`，见 [loginapp.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/loginapp/loginapp.cpp:893)。
10. 使用 `pLogOnParamsEncoder_` 解 `LogOnParams`，并校验用户名/密码长度，见 [loginapp.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/loginapp/loginapp.cpp:909)。
11. 处理 resolved cached attempt，避免成功回复丢失后重复创建会话，见 [loginapp.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/loginapp/loginapp.cpp:961)。
12. 成功解码后才扣减登录限速预算，见 [loginapp.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/loginapp/loginapp.cpp:969)。
13. 如果没有 `encryptionKey` 且不允许明文登录，拒绝，见 [loginapp.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/loginapp/loginapp.cpp:976)。
14. 如果 `passwordlessLoginsOnly` 开启但请求仍携带 password，拒绝，见 [loginapp.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/loginapp/loginapp.cpp:990)。
15. 通过 `DBAppInterface::logOn` 把登录请求交给 DBApp，见 [loginapp.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/loginapp/loginapp.cpp:1020)。

这里有两个容易误判的点：

- `loginRateLimit` 不是包级限流，包级限流在 `NetworkInterface`。`loginRateLimit` 是 LoginApp 在登录语义上的全局预算。
- 限速扣减发生在成功解码 `LogOnParams` 之后，源码注释明确这是因为已经完成了解密登录参数的重活，见 [loginapp.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/loginapp/loginapp.cpp:969)。

失败回复也被节流。`handleFailure()` 每 0.5 秒重置 `numFailRepliesLeft_`，预算来自 `maxRepliesOnFailPerSecond / 2`，见 [loginapp.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/loginapp/loginapp.cpp:607)。这避免攻击者用大量失败登录诱导 LoginApp 放大发包。

## LoginChallenge 与 proof-of-work

`LoginChallenge` 是登录入口前的可插拔挑战机制。抽象接口只有四个动作：

- `writeChallengeToStream()`：服务端写 challenge 数据。
- `readChallengeFromStream()`：客户端读 challenge 数据。
- `writeResponseToStream()`：客户端写 response。
- `readResponseFromStream()`：服务端验证 response。

源码见 [login_challenge.hpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/connection/login_challenge.hpp:25)。

默认 challenge 类型由 `LoginChallengeFactories::registerDefaultFactories()` 注册：

- `delay`
- `fail`
- `cuckoo_cycle`

源码见 [login_challenge_factory.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/connection/login_challenge_factory.cpp:265)。

`processForLoginChallenge()` 的行为是：

- 如果同一来源已有 challenge 且失败过，返回 `LOGIN_REJECTED_CHALLENGE_ERROR`。
- 如果已有 pending challenge，重复发送同一个 challenge，而不是新建。
- 如果 `challengeType` 为空，跳过 challenge。
- 如果 challenge factory 创建失败，拒绝登录。
- 新建 challenge 后保存到 `loginRequests_[source]`，再把 challenge 回复给客户端。

源码见 [loginapp.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/loginapp/loginapp.cpp:1035)。

`cuckoo_cycle` 的源码更具体：

- 构造 challenge 时用 `RAND_bytes` 生成随机 prefix，见 [cuckoo_cycle_login_challenge_factory.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/connection/cuckoo_cycle_login_challenge_factory.cpp:350)。
- challenge 数据是 `prefix_` 和 `maxNonce_`，见 [cuckoo_cycle_login_challenge_factory.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/connection/cuckoo_cycle_login_challenge_factory.cpp:372)。
- 客户端循环寻找 proof，源码 TODO 明确还没有最大时间或迭代限制，见 [cuckoo_cycle_login_challenge_factory.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/connection/cuckoo_cycle_login_challenge_factory.cpp:415)。
- 服务端验证 key 必须以前缀开头，剩余数据长度必须等于 `PROOFSIZE * sizeof(nonce_t)`，再调用 `Cuckoo::verify()`，见 [cuckoo_cycle_login_challenge_factory.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/connection/cuckoo_cycle_login_challenge_factory.cpp:451)。
- `easiness` 配置必须在 `(0, 100]`，见 [cuckoo_cycle_login_challenge_factory.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/connection/cuckoo_cycle_login_challenge_factory.cpp:520)。

客户端收到 challenge 后会创建 `LoginChallengeTask`。有 `TaskManager` 时放后台计算，否则在当前线程直接计算，见 [login_handler.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/connection/login_handler.cpp:339)。`LoginChallengeTask::perform()` 只调用 `writeResponseToStream()` 并记录耗时，见 [login_challenge_task.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/connection/login_challenge_task.cpp:39)。

设计含义：

- 这是一种登录入口抗滥用机制，目的是让客户端付出计算成本。
- 它不替代账号密码校验，最终账号逻辑仍在 DBApp 登录链路。
- `challengeType` 是运行时 watcher 暴露项，管理面写权限如果失控，可以改变登录入口行为。
- 客户端 proof 计算缺少最大耗时/迭代限制，调用侧不能假设该计算一定很快返回。

## 登录加密与明文回退边界

LoginApp 登录参数使用 `StreamEncoder` 解码。初始化时读取 `loginApp/privateKey`，创建 `RSAStreamEncoder`，见 [loginapp.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/loginapp/loginapp.cpp:519)。

关键边界在 `allowUnencryptedLogins`：

- 如果允许明文登录，私钥加载失败不会让 LoginApp 初始化失败，见 [loginapp.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/loginapp/loginapp.cpp:521)。
- 登录参数解码失败时，如果当前用了 encoder 且允许明文登录，会把 `pEncoder` 置空后再读一次，见 [loginapp.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/loginapp/loginapp.cpp:939)。
- 如果最终参数里没有 `encryptionKey` 且不允许明文登录，会拒绝请求，见 [loginapp.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/loginapp/loginapp.cpp:976)。

所以 `allowUnencryptedLogins` 不是一个普通兼容选项，而是高风险安全开关。它同时影响：

- 私钥缺失时 LoginApp 是否还能启动。
- 登录参数解析是否允许从加密回退到明文。
- 客户端是否必须提供后续 Channel 加密 key。

这也解释了为什么只看 `EncryptionFilter` 不够。登录阶段的安全链路还包括 RSA 登录参数、是否允许明文、登录返回里的 session key、客户端再到 BaseApp 的二次 attach。完整会话接管流程见 [登录、会话与 Proxy 接管](/architecture/login-session-proxy-flow)。

## Exposed 方法不是任意 RPC

客户端进入 BaseApp 后，上行外部协议定义在 `BaseAppExtInterface`。它包含 `baseAppLogin`、`authenticate`、移动同步、`requestEntityUpdate`、`enableEntities`、`disconnectClient` 以及 Base/Cell exposed method ranges，源码见 [baseapp_ext_interface.hpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/connection/baseapp_ext_interface.hpp:26)。

安全边界不在“客户端能发消息”这件事本身，而在消息号如何被映射：

- Base 实体方法走 `Proxy::baseEntityMethod()`。
- Cell 实体方法走 `Proxy::cellEntityMethod()`，再转发到 `CellAppInterface::runExposedMethod`。
- message id 必须落在 `BaseAppExtInterface::Range::baseEntityMethodRange` 或 `cellEntityMethodRange`。
- 方法描述来自 EntityDef 解析出的 exposed method 表。

`Proxy::baseEntityMethod()` 会用 `entityDesc.base().exposedMethodFromMsgID()` 查找方法。找不到就报错并返回，普通调用失败时日志包含 `CHEAT` 字样，见 [proxy.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/baseapp/proxy.cpp:2536)。

`Proxy::cellEntityMethod()` 会读取目标 `EntityID`，如果为 0 则替换为自己的 proxy entity id，然后写入目标 entity id、消息 id 和 source proxy id，转发到 CellApp，见 [proxy.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/baseapp/proxy.cpp:2491)。

CellApp 侧 `Entity::runExposedMethod()` 会读取 method id，再调用 `runMethodHelper(..., isExposed=true)`，见 [entity.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/entity.cpp:5399)。如果方法声明为 `OWN_CLIENT`，`runMethodHelper()` 会校验 source id 必须等于目标 entity id，不匹配则 block，见 [entity.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/entity.cpp:5440)。

结论：

- BigWorld exposed method 是 EntityDef 驱动的受限 RPC，不是客户端任意调用服务端 Python 函数。
- `.def` 里的 `Exposed` 标记就是攻击面声明，安全审计必须逐个看参数类型、调用频率、权限语义和服务端二次校验。
- `OWN_CLIENT` 解决的是“只能自己的客户端调用”这一类边界，不等于所有业务权限校验。
- RateLimitMessageFilter 只能限制消息数量和字节，不能替代 exposed method 内部的业务鉴权。

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

## PacketFilter 的安全边界

源码能证明 BigWorld 有加密 filter，但不能把它等同于完整传输层安全协议。

需要区分：

- PacketFilter 加密保护的是 Mercury Channel 包体。
- BigWorld 的内部组件通信很大程度上依赖可信网络拓扑。
- LoginApp challenge 是登录入口抗滥用逻辑，不等于所有 Mercury 内部 RPC 都有认证授权。
- Watcher、probe、Message Logger 属于管理面或工具链入口，安全边界不在 PacketFilter 内。

所以源码分析时不能只问“有没有加密”，要继续追问：

- 密钥从哪里来？
- 握手和 channel 绑定在哪一层完成？
- 是否有认证绑定？
- 是否防重放？
- 内部 RPC 是否有认证和授权？
- Watcher/管理接口是否暴露在不可信网络？

## 管理面也是攻击面

BigWorld 的安全分析不能只盯玩家客户端。源码里有多类管理面入口：

- Watcher 支持远程 GET/SET/TELL，且可以通过 `ForwardingWatcher` 转发到 BaseApp、CellApp、ServiceApp 集合。
- LoginApp 的 `challengeType` 被注册成 watcher，意味着管理面可以改变登录 challenge 行为。
- LoginApp `probe` 能返回主机名和进程 owner 信息，生产模式启用 `allowProbe` 会报配置错误。
- Message Logger 可以接收进程日志、重连、过滤和写入后端，如果暴露不当会泄露内部拓扑、账号名、错误堆栈和运行状态。

相关源码：

- Watcher 网络协议支持 SET，见 [watcher_nub.hpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/watcher_nub.hpp:43)。
- `ForwardingWatcher` 支持 `all/command/...`、`leastLoaded/command/...` 这类转发路径，见 [watcher_forwarding.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/server/watcher_forwarding.cpp:64)。
- LoginApp 注册 `challengeType` watcher，见 [loginapp.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/loginapp/loginapp.cpp:356)。
- `allowProbe` 的生产模式检查在 [loginapp.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/loginapp/loginapp.cpp:301)，probe 回复内容在 [loginapp.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/loginapp/loginapp.cpp:1131)。

管理面风险的本质是：Watcher 不是只读指标协议，probe 不是业务登录协议，Message Logger 也不是普通 stdout。它们都应放在独立管理网络，并按“只读观测”和“可写控制”拆权限。

## 源码取舍

安全能力在源码里是分层分布的，不是一个统一策略平面：

- `NetworkInterface` 和 filters 先处理包入口、初连包形状、channel 加密和限流。
- LoginApp 处理协议版本、登录参数大小、加密登录要求、challenge、pending login 和 DBApp 转发。
- BaseApp 的客户端消息预算按 tick 控制 Proxy 输入，避免单个客户端持续占用主循环。
- EntityDef 把 exposed 方法范围和 `OWN_CLIENT` 语义编进协议契约。
- Watcher、probe、Message Logger 形成管理面入口，需要按独立攻击面分析。

这套分层和 BigWorld 的进程模型匹配：外部登录、长期会话、实体 RPC、内部工具链分别在不同组件里处理。代价是安全判断必须跨 `NetworkInterface`、LoginApp、BaseApp、EntityDef 和工具协议一起看，不能只检查某个入口。

## 源码验证重点

安全章节对应的测试不应只做“正常登录成功”：

- `allowLogin=false` 时登录必须拒绝，且不进入 DBApp `logOn`。
- 协议版本不兼容时返回 `LOGIN_BAD_PROTOCOL_VERSION`。
- 超过 `maxLoginMessageSize`、`maxUsernameLength`、`maxPasswordLength` 时返回 malformed request。
- `allowUnencryptedLogins=false` 且缺少 `encryptionKey` 时必须拒绝。
- 开启 `challengeType=cuckoo_cycle` 后，错误 prefix、错误 proof 长度、`Cuckoo::verify()` 失败都必须拒绝。
- 重复 pending login 应复用 pending/challenge 状态，不能重复创建 Proxy。
- BaseApp 初连包不是 `baseAppLogin` 形状时应被 `InitialConnectionFilter` 丢弃。
- 非 exposed message id 不能调用 Base/Cell 实体方法。
- `OWN_CLIENT` 方法从其他 source proxy id 调用时必须被 block。
- Watcher SET、ForwardingWatcher command、LoginApp probe 和 Message Logger 连接必须只在受控管理网络可达。

## 本章边界

本章只分析安全、限流、登录入口和加密边界。Watcher、Profiler 和日志的完整机制在下一章展开；这里明确它们属于管理面攻击面，是为了避免把“可观测性”误认为天然安全。
