# 通信抽象与 RPC

<div class="arch-hero">

Mercury 的通信层不是 gRPC 式 IDL 生成体系，而是 C++ 宏接口、`InterfaceElement` 消息描述、`Bundle` 消息流和 `InputMessageHandler` 回调共同组成的进程间 RPC 机制。它服务的是 MMO 引擎内部的高频、低延迟、可混合可靠性的消息通信。

</div>

## 先给结论

BigWorld 的通信模型可以概括为：

<div class="flow-strip">
  <span class="flow-node">Interface 定义</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">InterfaceElement</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">Bundle 写消息</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">Channel 发送</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">BundleProcessor 分发</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">Handler 执行</span>
</div>

关键源码：

- `InterfaceElement` 描述消息 ID、长度编码、handler，见 [interface_element.hpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/interface_element.hpp:78)。
- `InterfaceElement::headerSize()` 计算消息头大小，见 [interface_element.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/interface_element.cpp:103)。
- `Bundle` 是消息序列并继承 `BinaryOStream`，见 [bundle.hpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/bundle.hpp:64)。
- `UDPBundle` 负责 UDP packet 编排、可靠消息顺序和 piggyback，见 [udp_bundle.hpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/udp_bundle.hpp:37)。
- `UDPBundleProcessor` 按 message id 查 `InterfaceTable` 并分发，见 [udp_bundle_processor.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/udp_bundle_processor.cpp:70)。
- 接口注册宏在 [interface_macros.hpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/interface_macros.hpp:193)。

## InterfaceElement 是什么

`InterfaceElement` 是 Mercury 消息协议的最小元数据单元。它不是业务对象，而是消息头描述。

它包含：

- `MessageID id_`：消息编号。
- `lengthStyle_`：固定长度、变长、callback length 或无效消息。
- `lengthParam_`：固定长度大小，或变长长度字段占用字节数。
- `name_`：调试和统计用名称。
- `pHandler_`：收到消息后的处理器。
- `shouldProcessEarly_`：是否允许乱序提前处理。

长度类型定义在 [interface_element.hpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/interface_element.hpp:28)：

<div class="decision-table">

| 类型 | 含义 | 适用场景 |
| --- | --- | --- |
| `FIXED_LENGTH_MESSAGE` | 消息体长度固定 | 小型结构体消息，header 最短 |
| `VARIABLE_LENGTH_MESSAGE` | header 中携带长度字段 | 字符串、数组、Entity 数据等变长负载 |
| `CALLBACK_LENGTH_MESSAGE` | handler 根据来源动态决定长度 | 协议兼容、特殊握手或上下文相关消息 |
| `INVALID_MESSAGE` | 未初始化或非法消息 | 防御式状态 |

</div>

`updateLengthDetails()` 会在 callback length 场景中询问 handler 获取真实 stream size，见 [interface_element.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/interface_element.cpp:64)。

## 消息头编码

`InterfaceElement::headerSize()` 的逻辑很直接：

- 固定长度消息：只需要 `MessageID`。
- 变长消息：`MessageID + lengthParam_`。
- 其他类型：返回 `-1`。

源码见 [interface_element.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/interface_element.cpp:103)。

这说明 Mercury 的 wire format 是高度紧凑的。它没有 Protobuf 那种字段 tag + wire type 的自描述编码，而是依赖双方共享 interface table 和 EntityDef。

优点：

- 包头小，适合高频游戏消息。
- 固定长度消息编码非常快。
- 消息 ID 可直接索引 `InterfaceTable`。

代价：

- 协议自描述能力弱。
- 版本演进依赖部署纪律和 MD5/接口一致性检查。
- 抓包调试需要引擎侧解析工具。

## Bundle 是什么

`Bundle` 是 Mercury 的发送构造器。它继承 `BinaryOStream`，所以业务代码可以像写二进制流一样把参数写入消息。

核心接口见 [bundle.hpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/bundle.hpp:80)：

- `startMessage()`：开始普通消息。
- `startRequest()`：开始请求消息，并注册 reply handler、超时和可靠性。
- `startReply()`：开始回复消息。
- `sendMessage()` / `sendRequest()`：模板封装，把参数结构体写入 bundle。

<MermaidDiagram title="Bundle 写消息流程">
sequenceDiagram
  participant App as App Code
  participant Bundle as Mercury::Bundle
  participant Stream as BinaryOStream
  participant Channel as Channel
  participant Peer as Remote Process

  App->>Bundle: startMessage(InterfaceElement)
  App->>Stream: << args
  App->>Bundle: startRequest(handler, timeout)
  App->>Stream: << request args
  App->>Channel: send(bundle)
  Channel->>Peer: packets
</MermaidDiagram>

这种 API 设计贴合 C++ 游戏服务器代码：调用方不直接管理包头和长度字段，只按 interface 元数据写消息。

## UDPBundle 与 TCPBundle

Mercury 的 `Bundle` 是抽象基类，UDP 和 TCP 有不同实现：

- `UDPBundle`：面向 UDP packet、可靠顺序、piggyback、分片、ACK。
- `TCPBundle`：面向 TCP 流，按长度帧组织消息。

`UDPBundle` 额外维护：

- `ReliableVector reliableOrders_`：可靠消息顺序。
- `BundlePiggybacks piggybacks_`：重发搭车内容。
- `curIE_`、`msgLen_`、`msgBeg_`：当前消息编码状态。
- `ack_`：off-channel ACK。
- `numReliableMessages_`：可靠消息统计。

源码见 [udp_bundle.hpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/udp_bundle.hpp:143)。

这说明 RPC 抽象上层统一，但底层传输并不完全透明。可靠性、packet 边界和 piggyback 仍然是 UDP 语义的一部分。

## 请求与回复

`Bundle::startRequest()` 接收 `ReplyMessageHandler`、`arg`、`timeout` 和 `ReliableType`，默认超时是 5 秒，见 [bundle.hpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/bundle.hpp:21)。

这是一种轻量 request/reply：

- 请求被写入当前 Bundle。
- 本地 `RequestManager` 记录等待的 reply。
- 回复消息使用内部 `InterfaceElement::REPLY`，见 [interface_element.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/interface_element.cpp:24)。
- 超时后由 handler 接收失败结果。

它不是 HTTP/gRPC 那种独立连接请求模型，而是嵌入 Mercury Channel 的消息流。

## 接收分发链路

接收侧大致链路：

<MermaidDiagram title="Mercury 接收分发链路">
flowchart TD
  A[PacketReceiver recvfrom] --> B[NetworkInterface process packet]
  B --> C[UDPChannel addToReceiveWindow]
  C --> D[UDPBundleProcessor]
  D --> E[InterfaceTable message id lookup]
  E --> F[InterfaceElement length decode]
  F --> G[InputMessageHandler]
  G --> H[BaseApp/CellApp/DBApp handler]
</MermaidDiagram>

`UDPBundleProcessor` 会根据 `iter.msgID()` 从 `interfaceTable` 找到对应 `InterfaceElementWithStats`，再复制/更新长度细节，源码见 [udp_bundle_processor.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/udp_bundle_processor.cpp:70)。

这条链路有两个关键边界：

- 消息处理通常在 Reactor 主线程同步执行。
- handler 耗时会直接影响后续网络、Timer 和 Tick。

所以 Mercury 的 RPC 不是“异步任务自动并发”，而是“网络事件驱动的同步回调”。这点对线程模型和性能分析非常关键。

## 怎么和前端通信

如果把“前端”具体化为 BigWorld client，那么它不是直接连 CellApp，也不是直接连任意实体实例，而是经由 `Proxy` 所在的 BaseApp 通信。

关键入口可以直接从源码看出来：

- 客户端下行协议定义在 `ClientInterface`，包含 `createEntity`、`updateEntity`、`entityMethod`、`entityProperty`、`enterAoI`、`leaveAoI` 等消息，见 [client_interface.hpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/connection/client_interface.hpp:60)。
- 客户端上行到服务器的外部协议定义在 `BaseAppExtInterface`，包含 `baseEntityMethod`、`cellEntityMethod`、移动同步和登录相关消息，见 [baseapp_ext_interface.hpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/connection/baseapp_ext_interface.hpp:26)。
- `Proxy` 是挂着客户端连接的特殊 Base，见 [proxy.hpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/baseapp/proxy.hpp:49)。

链路不是“浏览器请求 API”，而是“客户端连接一个 Proxy，由 Proxy 转发实体语义消息”。

### 服务端到客户端

服务端给客户端发消息，核心不是任意实体直接持有 socket，而是：

1. 玩家连接后，客户端通道挂在某个 `Proxy` 上。
2. `Proxy` 维护 `pClientChannel_`，并把 Cell 或 Base 的可见消息转成 `ClientInterface` 消息发给客户端，见 [proxy.hpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/baseapp/proxy.hpp:79)。
3. `Proxy::createBasePlayer()` 会先发送 `ClientInterface::createBasePlayer`，再把 `FROM_BASE_TO_CLIENT_DATA` 属性流下发，见 [proxy.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/baseapp/proxy.cpp:1630)。
4. 来自 Cell 的 `tickSync`、`avatarUpdate*`、`createEntity`、`updateEntity`、`spaceData` 等消息，由 `Proxy` 转发给客户端，见 [proxy.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/baseapp/proxy.cpp:1653) 以及 [common_client_interface.hpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/connection/common_client_interface.hpp:34)。

客户端接收侧的入口在 `ServerConnection`：

- `createEntity()` / `createEntityDetailed()` 解出实体 ID、类型、位置和剩余属性流，见 [server_connection.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/connection/server_connection.cpp:2633)。
- `updateEntity()` 把属性流交给上层 handler，见 [server_connection.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/connection/server_connection.cpp:2697)。
- `entityMethod()` / `entityProperty()` 用 `ClientInterface::Range` 把消息号还原成 exposed method/property id，再交给客户端实体层，见 [server_connection.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/connection/server_connection.cpp:1032)。

所以前端看到的“实体创建、属性刷新、方法回调”，本质上都是 `ClientInterface` + EntityDef 共同解释出来的二进制流。

### 客户端到服务端

客户端上行同样不是直接找某个游戏逻辑对象，而是发给当前 `Proxy`：

- `ServerConnection::startBasePlayerMessage()` 用 `BaseAppExtInterface::baseEntityMethodRange` 组包，见 [server_connection.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/connection/server_connection.cpp:833)。
- `ServerConnection::startCellEntityMessage()` 用 `BaseAppExtInterface::cellEntityMethodRange` 组包，并把目标 `entityID` 写进流，见 [server_connection.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/connection/server_connection.cpp:875)。

到服务端后，`Proxy` 分两路处理：

- `Proxy::baseEntityMethod()` 直接在当前 Proxy/Base 上解析 exposed Base method 并调用脚本方法，见 [proxy.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/baseapp/proxy.cpp:2536)。
- `Proxy::cellEntityMethod()` 则把调用包装成 `CellAppInterface::runExposedMethod`，转发到对应 Cell 实体，见 [proxy.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/baseapp/proxy.cpp:2491)。

Cell 侧再由 `Entity::runExposedMethod()` 和 `runMethodHelper()` 完成真正的方法分发，见 [entity.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/entity.cpp:5399)。

这说明客户端和“前端逻辑实体”的通信抽象其实是：

- 先找到玩家所在的 Proxy。
- 再由 Proxy 把消息导向 Base 方法或 Cell 方法。
- 最后由 EntityDef 的 exposed method 定义完成脚本调用。

### 不是所有实例都直连前端

这里必须澄清一个容易误解的点。

BigWorld 里很多实体实例都“支持通信”，但含义并不是“每个实例都有自己的前端网络连接”：

- 对客户端来说，真正的物理连接挂在 `Proxy` 的 `Channel` 上。
- 其他实体实例对客户端的可见性，靠 AOI 和 `Proxy` / Witness 转发。
- 普通 Base/Cell 实体之间通信主要依赖 mailbox，不是客户端 socket。

比如脚本里调用 `otherEntity.base.someMethod()`，底层会走 `BaseEntityMailBox::getStreamEx()`，包装成 `BaseAppIntInterface::callBaseMethod`，见 [mailbox.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/baseapp/mailbox.cpp:1054)。

同理，`self.base`、`self.cell`、`self.client` 等通信对象，本质上都是 `PyEntityMailBox` 的不同实现，见 [mailbox_base.hpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/entitydef/mailbox_base.hpp:35)。

### 前端拿到的不是 ORM 对象，而是连接模型

客户端侧也不是拿到“服务器对象引用”。它拿到的是一套连接模型：

- `ServerConnection` 负责网络收发。
- `BWEntities` / `BWServerMessageHandler` 把实体消息分发到本地客户端实体表示。
- `ServerEntityMailBox` 让客户端代码能按实体 ID 发回服务器方法调用，见 [server_entity_mail_box.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/connection_model/server_entity_mail_box.cpp:17)。

这更接近“远端实体连接模型”，而不是 Web 前端常见的 REST/GraphQL 数据拉取。

## 接口注册

服务进程启动时会把接口注册到 `NetworkInterface`。

已有源码证据：

- `CellAppInterface::registerWithInterface()` 在 [cellapp.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/cellapp.cpp:539)。
- `BaseAppMgrInterface::registerWithInterface()` 在 [baseappmgr.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/baseappmgr/baseappmgr.cpp:389)。
- `DBAppInterface::registerWithInterface()` 在 [dbapp.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/dbapp/dbapp.cpp:231)。

接口宏会生成注册函数和 `InterfaceElement` 引用，见 [interface_macros.hpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/interface_macros.hpp:193)。

这种宏体系的优点是：

- 运行时开销低。
- C++ 调用点简洁。
- 消息 ID、长度、handler 绑定在编译期附近。

代价是：

- IDE/静态分析体验弱。
- 接口变更不如独立 IDL 清晰。
- 跨语言生态差。
- 生成和注册逻辑隐藏在宏后，学习成本高。

## 与 EntityDef 的关系

Mercury RPC 解决“消息怎么发”，EntityDef 解决“实体属性和方法参数怎么编码”。

两者关系：

- Interface 定义进程级消息，例如 `createBaseWithCellData`、`setSharedData`。
- EntityDef 定义实体属性、方法、数据类型。
- `MethodArgs` 把脚本方法参数写入 `BinaryOStream`。
- `DataDescription` 把属性按 client/base/cell/persistent 语义写入 stream。
- Bundle 承载这些 stream，并交给 Channel 发送。

所以 BigWorld 的协议不是单一 IDL 文件能完全表达的。它是 Mercury interface + EntityDef 类型系统 + Python 脚本绑定共同形成的协议层。

## 和 Actor 的相似点与区别

你问“每个实例都支持通信的话，和 Actor 有什么区别”，这个问题是对的，但不能简单回答成“就是 Actor”。

### 相似点

BigWorld 实体和 Actor 确实有几处明显相似：

- 都强调“按实例持有状态”。
- 都主要通过消息/方法调用而不是共享内存交互。
- 都存在“地址化”的实例引用。
- 都天然适合分布到不同进程或机器。

BigWorld 里这种“可寻址实例引用”就是 mailbox：

- `PyEntityMailBox` 表示一个远端实体目标，见 [mailbox_base.hpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/entitydef/mailbox_base.hpp:35)。
- 调脚本方法时，`PyEntityMailBox::callMethod()` 会校验参数、申请流、编码参数并发送，见 [mailbox_base.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/entitydef/mailbox_base.cpp:125)。

从使用体验看，`entity.base.foo()`、`entity.cell.bar()`、`entity.client.baz()` 确实很像在给某个 Actor 发消息。

### 本质差异

但 BigWorld 不是通用 Actor runtime，至少有四个关键差异。

第一，实体不是单 mailbox 单线程执行体。

- Actor 模型通常强调一个 actor 只有一个 mailbox 和串行消费语义。
- BigWorld 一个逻辑实体会拆成 Base、Cell、Client 甚至 Ghost 多个视图，每个视图的职责不同。
- 客户端通信还要经过 `Proxy`，不是每个实例自己持有外部连接。

第二，消息不是唯一的一等公民，属性同步同样是一等机制。

- Actor 系统通常更强调显式消息。
- BigWorld 除了方法调用，还有属性复制、AOI enter/leave、volatile position、ghost 更新、DB 持久化这些专门路径。
- `PropertyChange` 是独立于方法调用的协议体系，见 [property_change.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/entitydef/property_change.cpp:139)。

第三，调度目标不是“通用并发模型”，而是“游戏世界语义”。

- Actor runtime 关注隔离、调度、公平性、容错监督树。
- BigWorld 更关注空间分区、AOI、Witness、Base/Cell 分工、迁移、备份和 DB 写入。
- 它有分布式实体运行时特征，但没有看到类似 Erlang/Akka 那种通用 supervisor tree 语义。

第四，很多消息处理仍然绑定主线程 Reactor，而不是独立 actor scheduler。

- `Mercury` handler 通常在 Reactor 主线程执行。
- `Proxy::baseEntityMethod()`、`Entity::runMethodHelper()` 最终直接调用脚本逻辑，见 [proxy.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/baseapp/proxy.cpp:2536) 和 [entity.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/entity.cpp:5412)。

所以更准确的判断是：

- BigWorld 具有明显的 actor-like messaging 特征。
- 但它是面向 MMO 实体语义定制的分布式实体运行时，不是通用 Actor 框架。

### 一个更准确的类比

如果一定要类比，BigWorld 更接近：

- “Actor + replicated entity + AOI + authoritative simulation + custom persistence”

而不是单纯：

- “每个实体就是一个 Actor”

这个区别很关键。前者解释了为什么它同时需要 mailbox、属性增量同步、Proxy/Witness、Base/Cell 切分和专用 DBApp；后者解释不了这些引擎级约束。

## 源码取舍

Mercury Interface 和 EntityDef 共同承担协议契约，源码取舍集中在这几处：

- 进程级消息由 C++ interface 宏注册成 `InterfaceElement`，用 message id 和 length style 快速分发。
- 实体级方法和属性由 EntityDef 分配 index、exposed id、client-server property id 和 digest。
- `Bundle` / `BinaryOStream` 让业务代码按参数顺序写流，避免在热路径构造通用对象模型。
- UDP Channel 保留可靠/不可靠混合、piggyback、ACK 和 request/reply 超时语义。
- 前端客户端只连接 `Proxy`，普通实体通过 mailbox、AOI、Witness 和 `ClientInterface` 间接通信。

代价也同样来自源码结构：

- 协议不自描述，抓包和跨语言工具必须拿到同一份 interface table 和 EntityDef。
- 宏注册隐藏了部分消息编号和 handler 绑定，阅读入口分散。
- Handler 通常在 Reactor 主线程同步执行，RPC 调用不是自动并发任务。
- Entity method、property sync、AOI 消息、登录握手和 DB 持久化共用 BinaryStream 体系，单点兼容性变更影响面大。

## 源码正确性边界

通信层的正确性依赖几个前提：

- 双方 interface table 一致。
- 变长消息长度字段可信且经过校验。
- handler 对 stream 消费完整，不残留错误状态。
- request/reply 超时路径必须释放等待状态。
- Reactor handler 不能阻塞。
- 对外部客户端消息必须考虑 flood、超长 payload、非法 message id、伪造 channel 元数据。

## 源码验证重点

通信层测试应覆盖消息编码、分发和外部入口防御：

- `InterfaceElement::headerSize()` 对固定长度、变长和非法消息应返回预期头大小。
- callback length 消息必须通过 handler 得到真实 stream size。
- `Bundle::startRequest()` 创建的 request 应在 reply 或 timeout 后释放等待状态。
- `UDPBundleProcessor` 遇到未知 message id、错误长度或未消费完 stream 时应走错误路径。
- `Proxy::baseEntityMethod()` 和 `Proxy::cellEntityMethod()` 必须只允许 EntityDef exposed range 内的方法。
- `OWN_CLIENT` 方法应校验 source entity，不能由其他客户端伪造调用。
- `ServerConnection` 解析 `entityMethod` / `entityProperty` 时必须用同一份 `ClientInterface::Range` 和 EntityDef。
- 前端实体创建顺序应覆盖 `createBasePlayer`、`enterAoI`、`createEntity`、`updateEntity` 和 `leaveAoI`。
- mailbox 调用应验证参数类型、stream size 和目标 Base/Cell/Client 路径。

## 本章边界

本章解释 Mercury 的消息和 RPC 抽象。下一章进入 EntityDef 序列化：BigWorld 如何把 Python 对象、实体属性、方法参数和二进制网络流连接起来。
