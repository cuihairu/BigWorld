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

**概述：** BigWorld 使用侵入式引用计数：计数存放在被引用对象内部，而不是像 `std::shared_ptr` 那样有外部 control block。这是老派 C++ 游戏引擎的典型做法，优点是低开销，缺点是没有 weak_ptr 语义。

**源码入口：** [smartpointer.hpp:31](/home/cui/workspaces/BigWorld/programming/bigworld/lib/cstdmf/smartpointer.hpp:31)

```cpp
// smartpointer.hpp:31 - ReferenceCount 定义
class ReferenceCount
{
public:
    ReferenceCount() : refCount_(0) {}
    
    // 增加引用计数
    void incRef() const { ++refCount_; }
    
    // 减少引用计数
    void decRef() const
    {
        if (--refCount_ == 0)
        {
            delete this;  // 计数归零时删除自身
        }
    }
    
    // 获取当前计数
    int refCount() const { return refCount_; }
    
protected:
    virtual ~ReferenceCount() {}
    
private:
    mutable int refCount_;
};
```

```cpp
// smartpointer.hpp:157 - SafeReferenceCount 定义
class SafeReferenceCount
{
public:
    SafeReferenceCount() : refCount_(0) {}
    
    // 原子增加引用计数
    void incRef() const
    {
        AtomicInt::add( refCount_, 1 );
    }
    
    // 原子减少引用计数
    void decRef() const
    {
        if (AtomicInt::add( refCount_, -1 ) == 1)
        {
            this->destroy();  // 计数归零时调用 destroy()
        }
    }
    
    // 获取当前计数
    int refCount() const { return AtomicInt::get( refCount_ ); }
    
protected:
    virtual ~SafeReferenceCount() {}
    virtual void destroy() const { delete this; }
    
private:
    mutable AtomicInt refCount_;
};
```

**流程图：**

<MermaidDiagram title=”引用计数生命周期”>
sequenceDiagram
    participant Obj as ReferenceCount 对象
    participant Ptr1 as SmartPointer 1
    participant Ptr2 as SmartPointer 2
    participant GC as 垃圾回收

    Obj->>Obj: refCount_ = 0
    
    Ptr1->>Obj: incRef()
    Obj->>Obj: refCount_ = 1
    
    Ptr2->>Obj: incRef()
    Obj->>Obj: refCount_ = 2
    
    Ptr1->>Obj: decRef()
    Obj->>Obj: refCount_ = 1
    
    Ptr2->>Obj: decRef()
    Obj->>Obj: refCount_ = 0
    Obj->>GC: delete this
</MermaidDiagram>

**详细讲解：**

1. **侵入式设计**：引用计数存储在对象内部，不需要额外的 control block。这减少了内存分配和间接访问。

2. **非线程安全**：`ReferenceCount` 的 `incRef()` / `decRef()` 不是原子操作，只能在单线程中使用。

3. **线程安全**：`SafeReferenceCount` 使用原子操作，可以在多线程中安全使用。

4. **销毁时机**：
   - `ReferenceCount::decRef()` 在计数归零时直接 `delete this`
   - `SafeReferenceCount::decRef()` 在计数归零时调用 `destroy()`，可以重载销毁逻辑

5. **虚析构函数**：`~ReferenceCount()` 和 `~SafeReferenceCount()` 都是虚函数，确保正确的析构顺序。

**为什么不用 std::shared_ptr：**

- 侵入式引用计数没有 control block 的额外开销
- 可以直接嵌入引擎对象，不需要额外分配
- 支持从原始指针构造（`STEAL_REFERENCE` 和 `NEW_REFERENCE`）
- 缺点是没有 `weak_ptr`，循环引用需要手动打破

### ReferenceCount 生命周期调用链

**概述：** 非线程安全引用计数的完整调用链，从对象创建到销毁。

**源码入口：** [smartpointer.hpp:117](/home/cui/workspaces/BigWorld/programming/bigworld/lib/cstdmf/smartpointer.hpp:117)

```cpp
// smartpointer.hpp:117 - SmartPointer 构造和析构
template<class T>
class SmartPointer
{
public:
    // 从原始指针构造（STEAL_REFERENCE）
    SmartPointer( T * pObject, int /*refType*/ )
        : pObject_( pObject )
    {
        // 不增加引用计数，接管所有权
    }
    
    // 拷贝构造
    SmartPointer( const SmartPointer & other )
        : pObject_( other.pObject_ )
    {
        if (pObject_)
        {
            pObject_->incRef();  // 增加引用计数
        }
    }
    
    // 析构
    ~SmartPointer()
    {
        if (pObject_)
        {
            pObject_->decRef();  // 减少引用计数
        }
    }
    
    // 赋值运算符
    SmartPointer & operator=( const SmartPointer & other )
    {
        if (this != &other)
        {
            if (pObject_)
            {
                pObject_->decRef();  // 减少旧对象引用计数
            }
            pObject_ = other.pObject_;
            if (pObject_)
            {
                pObject_->incRef();  // 增加新对象引用计数
            }
        }
        return *this;
    }
    
private:
    T * pObject_;
};
```

**关键细节：**

- `STEAL_REFERENCE`：从原始指针构造，不增加引用计数，接管所有权
- `NEW_REFERENCE`：从原始指针构造，增加引用计数
- 拷贝构造和赋值运算符正确管理引用计数
- 析构时减少引用计数，可能触发对象销毁

源码入口：[smartpointer.hpp:117](/home/cui/workspaces/BigWorld/programming/bigworld/lib/cstdmf/smartpointer.hpp:117)

<div class="flow-strip">
  <span class="flow-node">SmartPointer 构造</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">incRef() 原子增</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">count_++</span>
</div>

<div class="flow-strip">
  <span class="flow-node">SmartPointer 析构</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">decRef() 原子减</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">count_--</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">count == 0 ?</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">delete this</span>
</div>

### SafeReferenceCount 生命周期

线程安全引用计数的完整调用链：

源码入口：[smartpointer.hpp:191](/home/cui/workspaces/BigWorld/programming/bigworld/lib/cstdmf/smartpointer.hpp:191)

<div class="flow-strip">
  <span class="flow-node">SmartPointer 构造</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">incRef() 原子增</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">BW_ATOMIC32_INC_AND_FETCH</span>
</div>

<div class="flow-strip">
  <span class="flow-node">SmartPointer 析构</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">decRef() 原子减</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">BW_ATOMIC32_DEC_AND_FETCH</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">count == 0 ?</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">destroy()</span>
</div>

### Channel 生命周期

Channel 对象的完整生命周期调用链：

源码入口：[channel.hpp:35](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/channel.hpp:35)

<div class="flow-strip">
  <span class="flow-node">NetworkInterface 创建 Channel</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">incRef() 引用计数 +1</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">注册到 channelMap_</span>
</div>

<div class="flow-strip">
  <span class="flow-node">Channel 断开</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">从 channelMap_ 移除</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">decRef() 引用计数 -1</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">检查 condemnedChannels_</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">延迟销毁</span>
</div>

### BackgroundTask 生命周期

后台任务的完整生命周期调用链：

源码入口：[bgtask_manager.hpp:25](/home/cui/workspaces/BigWorld/programming/bigworld/lib/cstdmf/bgtask_manager.hpp:25)

<div class="flow-strip">
  <span class="flow-node">创建 BackgroundTask</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">addBackgroundTask() 入队</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">SmartPointer 保活</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">后台线程执行 doBackgroundTask()</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">addMainThreadTask() 回主线程</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">tick() 执行 doMainThreadTask()</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">SmartPointer 析构</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">decRef() -> destroy()</span>
</div>

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
