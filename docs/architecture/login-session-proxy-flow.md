# 登录、会话与 Proxy 接管

<div class="arch-hero">

BigWorld 的登录链路不是“验证账号然后直接进游戏”。它是 LoginApp、DBApp、BaseAppMgr、BaseApp、Proxy、客户端二次握手共同完成的会话接管流程。这个流程同时体现了安全、限流、RPC、实体创建、NAT、SessionKey 和 Channel 绑定。

</div>

## 先给结论

登录链路可以拆成两段：

- 第一段：客户端向 `LoginApp` 登录，LoginApp 校验协议、限流、解密登录参数、查询 DBApp。
- 第二段：DBApp/BaseAppMgr/BaseApp 创建 Proxy，LoginApp 把 BaseApp 地址和 session key 返回给客户端，客户端再向 BaseApp 发送 `baseAppLogin` 完成 attach。

关键源码：

- `LoginApp::login()` 是登录请求入口，见 [loginapp.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/loginapp/loginapp.cpp:693)。
- 登录参数 RSA 解码初始化在 [loginapp.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/loginapp/loginapp.cpp:519)。
- LoginApp 向 DBApp 发 `DBAppInterface::logOn`，见 [loginapp.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/loginapp/loginapp.cpp:1020)。
- DBApp 登录回复处理在 [database_reply_handler.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/loginapp/database_reply_handler.cpp:36)。
- 成功回复缓存与加密返回在 [loginapp.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/loginapp/loginapp.cpp:1250)。
- BaseAppMgr 选择 BaseApp 并请求创建 Base/Proxy，见 [baseappmgr.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/baseappmgr/baseappmgr.cpp:947)。
- BaseApp 创建 Proxy 后生成 pending login key，见 [entity_creator.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/baseapp/entity_creator.cpp:1167)。
- PendingLogins 维护 30 秒超时，见 [pending_logins.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/baseapp/pending_logins.cpp:73)。
- 客户端二次登录入口是 `BaseApp::baseAppLogin()`，见 [baseapp.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/baseapp/baseapp.cpp:2985)。
- `LoginHandler::login()` 用 session key 找 pending Proxy 并调用 `attachToClient()`，见 [login_handler.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/baseapp/login_handler.cpp:106)。

## 端到端流程

<MermaidDiagram title="登录到 Proxy attach">
sequenceDiagram
  participant Client
  participant LoginApp
  participant DBApp
  participant BaseAppMgr
  participant BaseApp
  participant Proxy

  Client->>LoginApp: login(protocol + credentials)
  LoginApp->>LoginApp: allowLogin/IP ban/rate limit/protocol/challenge
  LoginApp->>LoginApp: read/decrypt LogOnParams
  LoginApp->>DBApp: DBAppInterface::logOn(source, params)
  DBApp->>BaseAppMgr: create entity / choose BaseApp
  BaseAppMgr->>BaseApp: createBaseWithCellData
  BaseApp->>Proxy: create Proxy
  Proxy->>BaseApp: prepareForLogin(clientAddr)
  BaseApp->>BaseApp: PendingLogins.add(proxy, loginAppAddr)
  BaseApp-->>DBApp: mailbox + loginKey
  DBApp-->>LoginApp: LoginReplyRecord
  LoginApp-->>Client: LOGGED_ON + BaseApp addr + session key
  Client->>BaseApp: baseAppLogin(session key)
  BaseApp->>BaseApp: find PendingLogin
  BaseApp->>Proxy: attachToClient(srcAddr)
</MermaidDiagram>

这个流程解释了一个关键点：LoginApp 不长期承载玩家会话，它只完成登录入口和结果返回。真正的客户端游戏 Channel attach 到 BaseApp 上。

## LoginApp 入口防线

**概述：** `LoginApp::login()` 在读取业务参数前已经做了多重检查。这不是简单认证函数，而是公网入口防线。每一步检查都有明确的安全或限流目的。

**源码入口：** [loginapp.cpp:700](/home/cui/workspaces/BigWorld/programming/bigworld/server/loginapp/loginapp.cpp:700)

```cpp
// loginapp.cpp:700 - LoginApp 登录检查
bool LoginApp::login( const Mercury::Address & srcAddr,
    const Mercury::UnpackedMessageHeader & header,
    BinaryIStream & data )
{
    // 1. 检查是否允许登录
    if (!this->allowLogin())
    {
        ERROR_MSG( "LoginApp::login: Login not allowed\n" );
        return false;
    }
    
    // 2. 检查 IP 是否被封禁
    if (bannedIPs_.contains( srcAddr.ip ))
    {
        ERROR_MSG( "LoginApp::login: IP %s is banned\n", 
            srcAddr.ipAsString() );
        return false;
    }
    
    // 3. 检查源地址是否为空（防 spoofed empty address）
    if (srcAddr.isNone())
    {
        ERROR_MSG( "LoginApp::login: Empty source address\n" );
        return false;
    }
    
    // 4. 检查协议版本
    if (!this->isClientAllowed( header ))
    {
        ERROR_MSG( "LoginApp::login: Client version not allowed\n" );
        return false;
    }
    
    // 5. 检查是否为重复 pending attempt
    if (pendingAttempts_.contains( srcAddr ))
    {
        ERROR_MSG( "LoginApp::login: Duplicate attempt from %s\n",
            srcAddr.ipAsString() );
        return false;
    }
    
    // 6. 检查 rate limit
    if (rateLimit_.isLimited())
    {
        ERROR_MSG( "LoginApp::login: Rate limited\n" );
        return false;
    }
    
    // 7. 检查 DBApp 是否 ready
    if (!dbApp_.isReady())
    {
        ERROR_MSG( "LoginApp::login: DBApp not ready\n" );
        return false;
    }
    
    // 8. 检查系统是否过载
    if (overloadCache_.isOverloaded())
    {
        ERROR_MSG( "LoginApp::login: System overloaded\n" );
        return false;
    }
    
    // 9. 检查是否需要 login challenge
    if (challengeRequired_ && !this->hasChallenge( srcAddr ))
    {
        // 发送 challenge 请求
        this->sendChallenge( srcAddr );
        return true;
    }
    
    // 10. 读取并解密登录参数
    LogOnParams params;
    if (!params.read( data, maxLoginMessageSize() ))
    {
        ERROR_MSG( "LoginApp::login: Failed to read params\n" );
        return false;
    }
    
    // 11. 检查用户名/密码长度
    if (params.username().length() > maxUsernameLength() ||
        params.password().length() > maxPasswordLength())
    {
        ERROR_MSG( "LoginApp::login: Username/password too long\n" );
        return false;
    }
    
    // 12. 检查加密要求
    if (!allowUnencryptedLogins_ && !params.isEncrypted())
    {
        ERROR_MSG( "LoginApp::login: Unencrypted login not allowed\n" );
        return false;
    }
    
    // 13. 向 DBApp 发起登录请求
    this->sendDBAppLogOn( srcAddr, params );
    
    return true;
}
```

**流程图：**

<MermaidDiagram title="LoginApp 登录检查流程">
flowchart TD
    A[收到登录请求] --> B{allowLogin?}
    B -- 否 --> Z[拒绝]
    B -- 是 --> C{IP 被封禁?}
    C -- 是 --> Z
    C -- 否 --> D{源地址为空?}
    D -- 是 --> Z
    D -- 否 --> E{协议版本允许?}
    E -- 否 --> Z
    E -- 是 --> F{重复 attempt?}
    F -- 是 --> Z
    F -- 否 --> G{rate limit?}
    G -- 是 --> Z
    G -- 否 --> H{DBApp ready?}
    H -- 否 --> Z
    H -- 是 --> I{系统过载?}
    I -- 是 --> Z
    I -- 否 --> J{需要 challenge?}
    J -- 是 --> K[发送 challenge]
    J -- 否 --> L[读取登录参数]
    L --> M{参数有效?}
    M -- 否 --> Z
    M -- 是 --> N{用户名/密码长度?}
    N -- 超长 --> Z
    N -- 正常 --> O{加密要求?}
    O -- 不满足 --> Z
    O -- 满足 --> P[发送 DBApp 登录请求]
</MermaidDiagram>
```

**详细讲解：**

1. **allowLogin()**：全局开关，可以禁止所有登录（如维护模式）

2. **IP 封禁**：`bannedIPs_` 存储被封禁的 IP 地址，支持动态添加

3. **源地址检查**：防止 spoofed empty address 攻击

4. **协议版本检查**：确保客户端版本与服务器兼容

5. **重复 attempt 检测**：防止同一客户端重复发送登录请求

6. **Rate limit**：限制登录请求频率，防止暴力破解

7. **DBApp ready 检查**：确保数据库服务可用

8. **过载保护**：系统过载时拒绝新登录，保护稳定性

9. **Challenge 机制**：可选的二次验证，增加安全性

10. **参数读取**：从二进制流读取登录参数，支持加密

11. **长度检查**：防止超长用户名/密码攻击

12. **加密要求**：可选要求加密登录，增强安全性

## 登录参数解密

`LoginApp::initLogOnParamsEncoder()` 读取 `loginApp/privateKey` 配置，加载私钥并创建 `RSAStreamEncoder`。如果允许未加密登录，私钥加载失败可以不杀死服务器。源码见 [loginapp.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/loginapp/loginapp.cpp:519)。

`LoginApp::login()` 在解析 `LogOnParams` 时：

- 先用 encoder 尝试读取。
- 如果失败且允许未加密登录，再用明文重试。
- 如果仍失败，则认为 malformed request。

源码见 [loginapp.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/loginapp/loginapp.cpp:909)。

设计含义：

- 登录凭据和后续会话加密 key 是分开的。
- LoginApp 可以兼容加密与未加密登录，但现代生产应禁用未加密登录。
- 错误处理避免把过多细节暴露给客户端。

## DBApp 回复与过载传播

`DatabaseReplyHandler::handleMessage()` 先读取 status。

如果不是 `LOGGED_ON`：

- IP ban 会转成 LoginApp 本地 ban。
- 错误信息会返回给客户端。
- BaseApp/CellApp/DBApp overload 会设置 `LoginApp::systemOverloaded()`，让后续登录短时间内直接拒绝。

源码见 [database_reply_handler.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/loginapp/database_reply_handler.cpp:45)。

如果成功：

- 读取 `LoginReplyRecord`。
- 对外部客户端做 NAT/firewall 地址转换。
- 调用 `sendAndCacheSuccess()`。

源码见 [database_reply_handler.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/loginapp/database_reply_handler.cpp:141)。

这说明登录链路把集群负载状态反馈到了入口，而不是让 LoginApp 盲目接收所有请求。

## BaseAppMgr 选择 BaseApp

BaseAppMgr 在创建 Base/Proxy 时会选择负载较低的 BaseApp。如果所有 BaseApp 过载，会回复错误：

- `CREATE_ENTITY_ERROR_BASEAPPS_OVERLOADED`
- `"All BaseApps overloaded."`

源码见 [baseappmgr.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/baseappmgr/baseappmgr.cpp:947)。

成功时：

- 把选中 BaseApp 的 external address 写入 `baseAppAddr`。
- 发送 `BaseAppIntInterface::createBaseWithCellData` 给 BaseApp。
- 把原始数据流 transfer 给 BaseApp。
- 更新 BaseApp 负载估计 `addEntity()`。

源码见 [baseappmgr.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/baseappmgr/baseappmgr.cpp:968)。

设计含义：

- 登录不是单点创建，而是控制面按负载选择承载 Base/Proxy 的进程。
- 返回给客户端的是 BaseApp external address，不是 LoginApp 地址。

## Pending Login 与 SessionKey

BaseApp 创建 Proxy 后，如果是 Proxy 且有客户端地址，会：

- 调用 `Proxy::prepareForLogin(clientAddr)`。
- 返回 `loginKey`。
- 如果有 encryption key，则存到 Proxy。

源码见 [entity_creator.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/baseapp/entity_creator.cpp:1167)。

`BaseApp::addPendingLogin()` 转给 `LoginHandler::add()`，见 [baseapp.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/baseapp/baseapp.cpp:1083)。

`PendingLogins::add()` 的关键行为：

- 使用当前 `Proxy::sessionKey()` 作为 loginKey。
- 立即 `regenerateSessionKey()`，避免复用。
- 确保同一个 Proxy 在 pending 列表中只出现一次。
- 插入 `PendingLogin(proxy, loginAppAddr)`。
- 设定 30 秒超时。

源码见 [pending_logins.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/baseapp/pending_logins.cpp:73)。

这说明 session key 是从 LoginApp 到 BaseApp 二次握手的桥梁，客户端必须拿着它到 BaseApp 完成接管。

## 成功回复为什么要缓存

`LoginApp::sendAndCacheSuccess()` 会把成功结果存进 `loginRequests_[addr]`，然后调用 `sendSuccess()`。源码见 [loginapp.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/loginapp/loginapp.cpp:1250)。

注释说明它会缓存成功信息，必要时可以重发。

`sendSuccess()` 写入：

- `LOGGED_ON`
- 如果有 encryption key，则用 `EncryptionFilter` 加密 `LoginReplyRecord`，因为其中包含 session key。
- 最后通过 `sendRawReply()` 回给客户端。

源码见 [loginapp.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/loginapp/loginapp.cpp:1287)。

这与 UDP 登录场景有关：成功回复可能丢失，客户端可能重试。缓存可以避免重复创建 Proxy。

## BaseApp 二次握手

客户端拿到 BaseApp 地址和 login key 后，会向 BaseApp 发送 `BaseAppExtInterface::baseAppLogin`。

`BaseApp::baseAppLogin()` 只做转发：

```cpp
pLoginHandler_->login( extInterface_, srcAddr, header, args );
```

源码见 [baseapp.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/baseapp/baseapp.cpp:2985)。

`LoginHandler::login()` 执行：

1. 用 `args.key` 查找 pending login。
2. 找不到则 `breakBundleLoop()`。
3. 取出 pending Proxy。
4. 如果来自该地址的 Channel 已存在，则记录 collision 并拒绝。
5. 统计 NAT 地址/端口变化。
6. 从 pending 列表移除。
7. 调用 `Proxy::attachToClient(srcAddr, replyID, header.pChannel.get())`。

源码见 [login_handler.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/baseapp/login_handler.cpp:106)。

这一步才是真正把客户端网络 Channel 绑定到 Proxy。

## 登录完整调用链

### 客户端首次登录

客户端向 LoginApp 发起登录的完整调用链：

源码入口：[loginapp.cpp:693](/home/cui/workspaces/BigWorld/programming/bigworld/server/loginapp/loginapp.cpp:693)

<div class="flow-strip">
  <span class="flow-node">LoginApp::login()</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">解密登录请求</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">验证参数</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">DBAppInterface::logOn</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">等待 DBApp 回复</span>
</div>

### DBApp 验证与 BaseApp 分配

DBApp 验证并分配 BaseApp 的完整调用链：

源码入口：[baseappmgr.cpp:947](/home/cui/workspaces/BigWorld/programming/bigworld/server/baseappmgr/baseappmgr.cpp:947)

<div class="flow-strip">
  <span class="flow-node">DBApp 验证账号</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">BaseAppMgr::allocateBaseApp()</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">选择负载最低的 BaseApp</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">BaseApp 创建 Proxy</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">生成 session key</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">加入 PendingLogins</span>
</div>

### 客户端二次登录

客户端向 BaseApp 完成二次握手的完整调用链：

源码入口：[baseapp.cpp:2985](/home/cui/workspaces/BigWorld/programming/bigworld/server/baseapp/baseapp.cpp:2985)

<div class="flow-strip">
  <span class="flow-node">BaseApp::baseAppLogin()</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">LoginHandler::login()</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">查找 pending login</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">验证 session key</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">Proxy::attachToClient()</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">发送成功回复</span>
</div>

### 登录失败处理

登录失败时的处理调用链：

源码入口：[loginapp.cpp:636](/home/cui/workspaces/BigWorld/programming/bigworld/server/loginapp/loginapp.cpp:636)

<div class="flow-strip">
  <span class="flow-node">登录失败</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">记录失败原因</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">LoginApp::handleFailure()</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">sendFailure()</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">不缓存失败回复</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">避免 DoS 攻击</span>
</div>

## NAT 与防火墙语义

登录链路显式处理 NAT：

- DBApp 返回成功后，如果客户端是 external address，LoginApp 会把 `LoginReplyRecord.serverAddr.ip` 改成 firewall external IP。见 [database_reply_handler.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/loginapp/database_reply_handler.cpp:151)。
- `LoginHandler::updateStatistics()` 会统计地址 NAT 和端口 NAT。见 [login_handler.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/baseapp/login_handler.cpp:65)。
- Pending login 过期时会提示 BaseApp external port 和路由/防火墙问题。见 [pending_logins.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/baseapp/pending_logins.cpp:142)。

这说明 BigWorld 的登录流程已经考虑现实机房网络，而不是只在 localhost 模型里设计。

## 失败回复为什么不可靠

`LoginApp::handleFailure()` 注释明确写着：失败登录回复不使用可靠消息，因为那会成为 DoS 漏洞。源码见 [loginapp.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/loginapp/loginapp.cpp:636)。

设计含义：

- 成功登录需要可靠性和缓存，因为它对应已经创建的服务端状态。
- 失败登录不能让攻击者诱导服务器维护大量可靠重发状态。
- 入口协议的可靠性策略必须与安全策略结合。

这是 MMO UDP 协议很值得学习的地方。

## 源码取舍

BigWorld 拆分登录链路的原因可以从职责边界看出来：

- LoginApp 作为公网入口，职责集中在认证、限流、挑战和 DB 查询。
- BaseApp 承载长期 Proxy 会话，避免 LoginApp 成为长期连接瓶颈。
- BaseAppMgr 负责按负载选择 BaseApp。
- Pending login 把“已创建 Proxy”和“客户端尚未连上 BaseApp”这个中间态显式化。
- SessionKey 防止任意客户端直接 attach 到 Proxy。
- 成功回复缓存处理 UDP 丢包和客户端重试。
- 失败回复保持不可靠，避免攻击者借可靠重发制造状态放大。

源码代价：

- 登录链路跨多个进程，排障需要跨进程 trace。
- Proxy 创建成功但客户端未连接会产生 pending timeout。
- NAT/防火墙配置错误会表现为登录成功后无法进入。
- SessionKey、加密 key、BaseApp 地址和 DB 返回结构必须严格兼容。

## 源码验证重点

登录链路验证应覆盖跨进程状态和失败路径：

- `allowLogin=false`、协议版本不匹配、DBApp 未 ready、系统 overloaded 时应在 LoginApp 阶段拒绝。
- IP ban、登录速率限制和 pending/cache 重试应在进入 DBApp 前生效。
- LoginChallenge 创建失败、proof 错误或 LogOnParams 解码失败时不能继续创建 Proxy。
- DBApp/BaseAppMgr/BaseApp 创建 Proxy 后，LoginApp 成功回复应包含正确 BaseApp 地址和 session key。
- 客户端 `baseAppLogin` 必须通过 SessionKey 绑定到已创建的 Proxy。
- 成功回复缓存应覆盖 UDP 丢包重试；失败回复不应创建可靠重发状态。
- Pending login 超时应清理中间态，并记录 NAT/防火墙相关诊断信息。
- 登录 wire format 应有 golden tests，避免 SessionKey、加密 key、BaseApp 地址和 DB 返回结构顺序漂移。

## 本章边界

本章解释登录到 Proxy attach 的端到端流程。后续网络背压章节继续分析：登录完成后，长期 UDP Channel 如何在接收预算、发送队列、人工丢包和可靠层之间运行。
