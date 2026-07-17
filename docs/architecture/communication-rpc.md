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

## 为什么不是 gRPC/Protobuf

这是现代读者最容易误判的地方。

BigWorld 没有选择 gRPC/Protobuf，不能简单归因于“旧”。更本质的原因是它的协议需要深度服务游戏运行时：

- 同一个类型系统要同时服务 C++、Python 脚本、实体属性、网络同步和持久化。
- 游戏消息需要可靠/不可靠混合，而 gRPC 基于 HTTP/2/TCP 可靠流。
- 高频内部消息需要非常短的 header 和低分配路径。
- Entity 方法调用不是普通服务 API，而是和实体生命周期、AOI、Base/Cell 归属绑定。
- 当时 gRPC/HTTP2/Protobuf 生态也不是 MMO 服务端的默认基础设施。

但现代对比也必须承认：

- Protobuf/FlatBuffers/Cap'n Proto 的 schema 演进和工具链更强。
- gRPC 生态在观测、负载均衡、跨语言调用上优势明显。
- Mercury 的宏接口和自定义二进制流对新人和外部工具不友好。

## 现代方案对比

<div class="decision-table">

| 方案 | 优点 | 代价 | 对 BigWorld 的适配性 |
| --- | --- | --- | --- |
| Mercury Interface | 紧凑、低开销、贴合引擎 | 宏复杂，生态弱 | 当前核心，不宜先替换 |
| Protobuf | schema 清晰，工具强 | 不直接表达 BigWorld 可靠/实体语义 | 适合外围服务，不适合直接替代 EntityDef |
| gRPC | 跨语言、观测和治理成熟 | TCP/HTTP2 语义重，游戏实时性弱 | 适合控制面、后台服务 |
| FlatBuffers | 零拷贝读取，适合实时数据 | schema 迁移和动态脚本结合复杂 | 可用于新网关或客户端资源协议 |
| Cap'n Proto | 高性能 RPC/序列化 | 生态和集成成本 | 可学习，不是低风险迁移目标 |
| Actor mailbox | 状态归属清晰 | 调度器和消息语义需重构 | 适合现代化新模块，不应直接套旧接口 |

</div>

## 正确性边界

通信层的正确性依赖几个前提：

- 双方 interface table 一致。
- 变长消息长度字段可信且经过校验。
- handler 对 stream 消费完整，不残留错误状态。
- request/reply 超时路径必须释放等待状态。
- Reactor handler 不能阻塞。
- 对外部客户端消息必须考虑 flood、超长 payload、非法 message id、伪造 channel 元数据。

后续安全和测试章节需要围绕这些前提做故障注入。

## 现代化建议

短期不建议把 Mercury RPC 全部替换为 gRPC 或 Protobuf。更稳妥的路线：

1. 给 interface table 导出可读 schema，先解决可观测和文档化。
2. 对消息 ID、长度、handler 耗时、错误 stream 增加统计。
3. 为关键接口补 request/reply 超时测试和非法 payload 测试。
4. 外围控制面可以引入现代 RPC，但不要穿透实时游戏主通道。
5. 若要引入 Protobuf，应先用于新边界，例如运维 API、日志事件、离线工具，而不是替换 Entity 同步。

## 本章边界

本章解释 Mercury 的消息和 RPC 抽象。下一章进入 EntityDef 序列化：BigWorld 如何把 Python 对象、实体属性、方法参数和二进制网络流连接起来。
