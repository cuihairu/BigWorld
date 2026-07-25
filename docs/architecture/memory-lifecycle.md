# 内存、对象生命周期与资源管理

<div class="arch-hero">

BigWorld 是典型的老牌 C++ 游戏引擎：对象生命周期由自定义引用计数、SmartPointer、Python 对象模型、Mercury Channel、Packet/Bundle、实体 real/ghost 转换和后台任务共同组成。学习它不能只看“有没有智能指针”，而要看谁拥有对象、何时销毁、是否跨线程、是否跨解释器、是否跨进程迁移。

</div>

## 先给结论

BigWorld 的生命周期管理有明显的工程体系：

- `ReferenceCount`：非线程安全引用计数，适合主线程拥有的对象。
- `SafeReferenceCount`：原子引用计数，适合跨线程或过滤器/任务等场景。
- `SmartPointer` / `ConstSmartPointer`：侵入式引用计数智能指针。
- `PacketFilter` 继承 `SafeReferenceCount`，说明过滤器可能跨异步路径共享。
- `Channel` 继承 `ReferenceCount`，更偏主线程/网络线程内管理。
- `BackgroundTask` 继承 `SafeReferenceCount`，后台线程与主线程交接时用 SmartPointer 保活。
- `MemoryOStream` 是可读写内存流，是网络、序列化、缓冲消息的重要基础结构。
- Entity 生命周期不是普通对象生命周期，还包含 real/ghost 转换、Witness 清理、Channel 转交和迁移流写入。

关键源码：

- `ReferenceCount` 在 [smartpointer.hpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/cstdmf/smartpointer.hpp:31)。
- `SafeReferenceCount` 在 [smartpointer.hpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/cstdmf/smartpointer.hpp:157)。
- `ConstSmartPointer` 在 [smartpointer.hpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/cstdmf/smartpointer.hpp:307)。
- `Channel` 继承 `ReferenceCount`，见 [channel.hpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/channel.hpp:35)。
- `PacketFilter` 继承 `SafeReferenceCount`，见 [packet_filter.hpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/packet_filter.hpp:21)。
- `BackgroundTask` 继承 `SafeReferenceCount`，见 [bgtask_manager.hpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/cstdmf/bgtask_manager.hpp:25)。
- `TaskManager::tick()` 在主线程处理 foreground tasks，见 [bgtask_manager.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/cstdmf/bgtask_manager.cpp:738)。
- `MemoryOStream` 定义在 [memory_stream.hpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/cstdmf/memory_stream.hpp:20)。
- `RealEntity::destroy()` 在 [real_entity.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/real_entity.cpp:246)。
- `Entity::convertRealToGhost()` 在 [entity.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/entity.cpp:2005)。

## 引用计数模型

BigWorld 使用的是侵入式引用计数：计数存放在被引用对象内部，而不是像 `std::shared_ptr` 那样有外部 control block。

`ReferenceCount` 的注释明确写着：

- 用于实现 SmartPointer 所需行为。
- 非线程安全。
- 引用计数存储在对象内部。

源码见 [smartpointer.hpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/cstdmf/smartpointer.hpp:31)。

`decRef()` 在计数归零时直接 `delete this`，见 [smartpointer.hpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/cstdmf/smartpointer.hpp:127)。

`SafeReferenceCount` 使用原子增减，计数归零时调用 `destroy()`，见 [smartpointer.hpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/cstdmf/smartpointer.hpp:214)。

两者差异不是小细节，而是所有权边界：

- `ReferenceCount` 表示“不要跨线程随便操作引用计数”。
- `SafeReferenceCount` 表示“对象可在多个执行路径间传递引用，但对象内部状态仍未必线程安全”。

## SmartPointer 语义

`ConstSmartPointer` 构造时调用 `incrementReferenceCount()`，析构时调用 `decrementReferenceCount()`。源码见 [smartpointer.hpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/cstdmf/smartpointer.hpp:343) 和 [smartpointer.hpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/cstdmf/smartpointer.hpp:426)。

重要特点：

- 支持 `STEAL_REFERENCE` 与 `NEW_REFERENCE`。
- 支持隐式上转型。
- 对象必须提供 `incRef()` / `decRef()`。
- 删除动作由对象自己的 `decRef()` 完成。

这与现代 `shared_ptr` 的差异：

- 没有独立 control block。
- 循环引用更难自动处理。
- 不能天然使用 `weak_ptr` 语义。
- 需要非常清楚对象是否允许 `delete this`。
- 优点是低开销、易嵌入引擎对象。

## 生命周期分层

<MermaidDiagram title="对象生命周期分层">
flowchart TD
  A[C++ Object] --> B{ownership model}
  B --> C[ReferenceCount main-thread object]
  B --> D[SafeReferenceCount cross-thread shared object]
  B --> E[PyObjectPlus Python visible object]
  B --> F[raw ownership explicit delete]
  C --> G[Channel / TimeQueue / functors]
  D --> H[BackgroundTask / PacketFilter / Watcher]
  E --> I[Base / Entity / script exposed objects]
  F --> J[Witness / BufferedMessage / transient helpers]
  I --> K[real ghost migration]
  H --> L[background then main thread completion]
</MermaidDiagram>

这解释了为什么 BigWorld 不能简单“全局替换成 `shared_ptr`”：

- 有些对象依赖 Python 引用计数。
- 有些对象由 Mercury Channel 生命周期控制。
- 有些对象需要跨线程回主线程析构。
- 有些对象在实体迁移时从 real 变 ghost，而不是销毁重建。

## 后台任务生命周期

`BackgroundTask` 继承 `SafeReferenceCount`，并定义两个阶段：

- `doBackgroundTask()`：后台线程执行。
- `doMainThreadTask()`：主线程执行。

源码见 [bgtask_manager.hpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/cstdmf/bgtask_manager.hpp:32)。

`TaskManager::tick()` 把 foreground task 列表 swap 到本地，然后逐个用 `BackgroundTaskPtr` 保活并调用 `doMainThreadTask()`。源码见 [bgtask_manager.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/cstdmf/bgtask_manager.cpp:744)。

设计含义：

- 后台线程和主线程之间通过引用计数交接对象。
- 主线程执行期间 `BackgroundTaskPtr pTask = *iter` 保证任务不会被提前释放。
- 任务可以在后台做阻塞工作，但主线程提交阶段仍要谨慎控制耗时。

局限：

- `stop_` 是协作式取消，任务必须主动响应。
- `doMainThreadTask()` 如果过慢，会拖慢 Tick。
- `SafeReferenceCount` 只保护引用计数，不保护任务内部状态。

## 网络对象生命周期

`Channel` 继承 `ReferenceCount`，见 [channel.hpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/channel.hpp:35)。

这与 BigWorld 的网络模型匹配：

- Channel 是 Mercury 的逻辑连接。
- 大多数 Channel 状态由主 Reactor 线程处理。
- 可靠 UDP 的 resend、ack、bundle 状态需要确定顺序。

`PacketFilter` 继承 `SafeReferenceCount`，见 [packet_filter.hpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/packet_filter.hpp:25)。

这也合理：

- filter 可能被 Channel 持有。
- 加密 filter、initial connection filter 等可能在不同收发路径传递。
- 引用计数需要更安全，但 filter 的实际调用仍应遵守网络线程边界。

## MemoryOStream 与缓冲消息

`MemoryOStream` 同时继承 `BinaryOStream` 和 `BinaryIStream`，可先写后读。源码见 [memory_stream.hpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/cstdmf/memory_stream.hpp:20)。

它内部维护：

- `pBegin_`
- `pCurr_`
- `pEnd_`
- `pRead_`
- `shouldDelete_`
- `canRewind_`

源码见 [memory_stream.hpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/cstdmf/memory_stream.hpp:61)。

这类流在 BigWorld 中非常核心：

- Mercury Bundle 构造。
- RPC 参数序列化。
- Entity 属性序列化。
- BaseApp 限流消息缓冲。
- DB 写入和迁移流。

例如 `RateLimitMessageFilter::BufferedMessage` 会把 incoming `BinaryIStream` 的剩余内容 transfer 到 `MemoryOStream`，用于稍后回放。见 [rate_limit_message_filter.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/baseapp/rate_limit_message_filter.cpp:38)。

这说明消息限流不是只保存指针，而是复制流内容，避免原始包生命周期结束后数据失效。

## Entity 生命周期

实体生命周期是 BigWorld 最复杂的部分。

`Entity::convertRealToGhost()` 做了几件关键事：

- 断言当前是 real。
- 禁止 callbacks。
- 如果有 Witness，先 `flushToClient()`。
- 如果有目标 CellApp Channel，则把 real 数据写入迁移流。

源码见 [entity.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/entity.cpp:2005)。

`RealEntity::destroy()` 在 offload 场景下会：

- 通知所有 ghost，real 即将迁移到下一个地址。
- 清理 Channel resend history，让责任转移到目标 app 创建的新 channel。

源码见 [real_entity.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/real_entity.cpp:246)。

这不是普通析构，而是状态迁移协议的一部分：

- 对象身份 `EntityID` 不变。
- real 权威位置变化。
- ghost 要更新 next real 地址。
- Channel resend history 的所有权要转移。
- Witness 需要先把客户端可见状态 flush。

## Python 对象生命周期

Base、Entity 等脚本暴露对象通常和 Python 对象模型耦合。

这带来几个额外约束：

- C++ 对象销毁前要考虑 Python 引用。
- Python callback 执行中不能随意删除底层 Entity。
- 热更新时旧解释器对象和新解释器对象的迁移更复杂。
- GIL 保护 Python 运行时，不等于保护 C++ 生命周期。

这也是为什么 BigWorld 很多地方使用“禁止 callbacks”“回主线程收尾”“迁移流”这些手段。

## 源码取舍

源码里存在多种所有权模型，不能用单一规则解释：

- `SmartPointer` / `ReferenceCount` 处理侵入式引用计数对象。
- `SafeReferenceCount` 用于跨线程引用计数安全，但不代表对象内部状态线程安全。
- `delete this` 模式依赖调用路径保证最后引用释放时机。
- Packet、Bundle、MemoryStream 等网络对象强调低分配和明确生命周期。
- PyObjectPlus、Base、Entity 同时受 C++ 引用和 Python 引用影响。

源码代价：

- 生命周期规则分散，学习成本高。
- `delete this` 模式调试困难。
- 线程安全容易被误解。
- 循环引用和 raw pointer 混用需要人工审计。
- Python/C++ 双重生命周期增加热更新和迁移风险。

## 源码验证重点

生命周期测试应覆盖所有权边界，而不是只查内存泄漏：

- Channel、Packet、Bundle、Base、Entity、Proxy、BackgroundTask、Watcher 应有清晰所有权图。
- 跨线程对象应区分“引用计数安全”和“内部状态安全”。
- `delete this` 路径应覆盖最后引用释放、回调重入和异常返回。
- `MemoryOStream`、`BinaryIStream` 反序列化应覆盖截断、长度溢出和嵌套过深。
- 热更新和实体迁移路径不应依赖隐式析构副作用完成业务状态转换。
- Python callback 执行期间销毁底层 Entity 的路径应被状态机或 callbacks guard 拦住。

## 本章边界

本章解释对象生命周期。下一章分析构建、平台和依赖：BigWorld 的技术债不只在源码，也在 CMake 版本、Makefile、Python 源码嵌入、OpenSSL、第三方库和平台基线。
