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

**概述：** BigWorld 使用 level-triggered epoll，而不是 edge-triggered。这不是技术落后，而是明确的保守取舍。源码注释说明作者知道 ET 可能有收益，但选择保持 `select` 行为一致。

**源码入口：** [event_poller.cpp:1079](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/event_poller.cpp:1079)

```cpp
// event_poller.cpp:1079 - EPoller 注册 FD
int EPoller::registerFileDescriptor( int fd, InputNotificationHandler * pHandler,
    bool isRead )
{
    // 创建 epoll_event
    struct epoll_event ev;
    ev.data.fd = fd;
    ev.data.ptr = pHandler;
    
    // TODO: Could be good to use EPOLLET (leave like select for now).
    ev.events = isRead ? EPOLLIN : EPOLLOUT;
    
    // 注册到 epoll
    if (epoll_ctl( epfd_, EPOLL_CTL_ADD, fd, &ev ) == -1)
    {
        ERROR_MSG( “EPoller::registerFileDescriptor: “
            “epoll_ctl failed for fd %d: %s\n”, fd, strerror(errno) );
        return -1;
    }
    
    return 0;
}
```

**流程图：**

<MermaidDiagram title=”EPoller 注册与事件处理”>
sequenceDiagram
    participant Handler as InputNotificationHandler
    participant EPoller as EPoller
    participant Kernel as Linux Kernel

    Handler->>EPoller: registerFileDescriptor(fd, isRead)
    EPoller->>Kernel: epoll_ctl(EPOLL_CTL_ADD, fd, EPOLLIN)
    
    Note over Kernel: FD 就绪...
    
    Kernel->>EPoller: epoll_wait() 返回事件
    EPoller->>Handler: handleInputNotification()
    Handler->>Handler: 处理数据
</MermaidDiagram>
```

**详细讲解：**

1. **Level-triggered 语义**：只要 FD 就绪，`epoll_wait()` 就会返回事件。这与 `select` 行为一致。

2. **Edge-triggered 语义**：只有 FD 状态变化时才返回事件。需要严格排空 FD，否则可能丢失事件。

3. **为什么选择 LT**：
   - **跨平台一致性**：LT 语义更接近 `select`，跨平台行为一致
   - **安全性**：不要求每个 handler 都严格排空 FD，避免 ET 容易丢事件
   - **老代码兼容**：对老代码和多平台网络抽象更安全
   - **可预测性**：游戏服务器主循环更关心可预测性，不一定追求最激进 I/O 模型

4. **为什么不用 io_uring**：BigWorld 开发时 io_uring 还不存在（2019 年才引入），选择 epoll 是当时最佳实践

### EPoller 事件处理

**源码入口：** [event_poller.cpp:1115](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/event_poller.cpp:1115)

```cpp
// event_poller.cpp:1115 - EPoller 事件处理
int EPoller::processPendingEvents( double maxWait )
{
    // 1. 计算等待时间
    int maxWaitMs = int(maxWait * 1000);
    
    // 2. 调用 epoll_wait
    struct epoll_event events[10];
    int numEvents = epoll_wait( epfd_, events, 10, maxWaitMs );
    
    // 3. 处理事件
    for (int i = 0; i < numEvents; i++)
    {
        InputNotificationHandler * pHandler = 
            static_cast<InputNotificationHandler *>(events[i].data.ptr);
        
        // 4. 调用 handler
        if (events[i].events & EPOLLIN)
        {
            pHandler->handleInputNotification();
        }
        
        if (events[i].events & EPOLLOUT)
        {
            pHandler->handleOutputNotification();
        }
    }
    
    return numEvents;
}
```

**关键细节：**

- 每次 `epoll_wait()` 最多取 10 个事件，避免一次处理太多
- `maxWait` 控制最大等待时间，与 Timer 配合实现精确调度
- handler 在主线程同步执行，不支持异步 I/O
- 这是 Reactor 模式的核心：事件驱动、同步处理

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

这说明如果瓶颈是逐包 syscall，批量 UDP 收发会比更换整个执行模型更贴近当前源码边界。

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
- 应优先观测发送队列满的频率和耗时，再判断它是否影响主线程 Tick。

## TCP 路径

TCP 路径也在 Reactor 下工作：

- 非阻塞 socket。
- 读写事件注册到 dispatcher。
- 部分发送时注册写事件，发送完成后注销。
- 帧格式使用 `uint16` 长度，超长帧使用扩展长度。
- 支持 raw TCP、WebSocket、加密 StreamFilter。

TCP 更适合登录、工具、管理、WebSocket 等场景；游戏主通道仍需要结合 Mercury 的 UDP 可靠层理解。

## I/O 后端边界

源码当前是 readiness callback 模型，而不是异步 completion 模型：

- BigWorld 的游戏 UDP 模型不是海量 FD，而是少量 UDP socket + 大量逻辑 Channel。
- 现有 handler 是同步回调，迁移到 completion queue 会改变执行模型。
- `io_uring` 要处理 buffer 生命周期、提交队列、完成队列、取消、背压和多线程消费。
- 如果热点在协议解析、脚本、AOI 或 Cell 负载，换 I/O 后端收益有限。

所以评估 I/O 后端时必须先确认瓶颈在 syscall、socket queue、协议解析、脚本执行还是 Cell 负载。

## 源码取舍

游戏服务器网络模型的目标不是单一 QPS：

- Tick 稳定性比平均吞吐更重要。
- 尾延迟比 syscall benchmark 更重要。
- 状态一致性比 I/O 后端先进性更重要。
- 可调试性和运维稳定性比局部极限性能更重要。

源码选择是使用成熟 readiness poller，保持跨平台抽象稳定，把复杂度放在 Mercury 协议、实体模型和 Cell 负载治理上。

## 源码验证重点

网络 I/O 测试应覆盖 poller 和 handler 边界：

- `EventPoller` 在 Linux 下应用 level-triggered epoll，并按注册 handler 分发。
- `PacketReceiver::handleInputNotification()` 应受 `maxSocketProcessingTime` 保护。
- TCP 读写事件注册、部分发送和注销写事件应保持非阻塞。
- `PacketSender::basicSendWithRetries()` 的 EAGAIN/ENOBUFS 路径不能无限阻塞主线程。
- 网络 handler 执行时间应计入主 Reactor 风险，而不是被当成后台 I/O。
- 修改 I/O 后端时，Channel、Bundle、ACK、fragment、request/reply 语义必须保持一致。

## 后续研究入口

下一批网络专题应继续深入：

- Mercury 可靠 UDP：ACK、重传、窗口、乱序、分片、piggyback，详见 [Mercury 可靠 UDP](/architecture/mercury-reliable-udp)。
- Bundle 与消息定义：宏接口、消息 ID、可变长流，详见 [通信抽象与 RPC](/architecture/communication-rpc)。
- UDP Channel：indexed、anonymous、irregular、once-off reliability，详见 [Mercury 可靠 UDP](/architecture/mercury-reliable-udp)。
- 网络故障注入：丢包、延迟、Flood、篡改测试，详见 [网络背压与故障注入](/architecture/network-backpressure-fault-injection)。
- 主循环与事件分发模型，详见 [主循环、Tick 与事件分发](/architecture/event-loop)。
