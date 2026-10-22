# Network I/O Model

> [Chinese](network-io-model.md) | **English**

<div class="arch-hero">

BigWorld uses a platform-aware readiness poller, a synchronous main-thread
Reactor, and Mercury's logical UDP Channels. The relevant question is therefore
not "does it use epoll?", but whether per-packet syscalls, protocol processing,
handlers, scripts, or Cell load dominate the tick budget.

</div>

## Conclusion

On Linux, Mercury uses level-triggered `epoll` through `EventPoller`:

- Linux: `EPoller`
- Emscripten: `PollPoller`
- Other platforms: `SelectPoller`

The abstraction starts at
event_poller.cpp.
`EPoller` uses `EPOLLIN/EPOLLOUT` but not `EPOLLET`; a source comment explicitly
keeps `select`-like behavior at
event_poller.cpp.
Each wait returns at most 10 events
(event_poller.cpp).
Handlers run synchronously on the Reactor thread. UDP uses per-packet
`recvfrom/sendto`, not `recvmmsg/sendmmsg`.

<MermaidDiagram title="EventPoller platform selection">
flowchart TD
  A[EventDispatcher] --> B[EventPoller]
  B --> C{Platform}
  C -- Linux, non-Emscripten --> D[EPoller epoll]
  C -- Emscripten --> E[PollPoller poll]
  C -- Other platforms --> F[SelectPoller select]
  D --> G[Input/Write Handler]
  E --> G
  F --> G
</MermaidDiagram>

The upper stack—Mercury Channels, Bundles, and reliable UDP—is the core model;
`epoll` is a platform optimization behind it.

## Why Level-Triggered epoll

Level triggering preserves `select` semantics and avoids the strict drain
requirements of edge-triggered I/O. The source acknowledges the possible
`EPOLLET` improvement but deliberately leaves the behavior unchanged. This
favors compatibility, predictable event delivery, and multi-platform behavior
over a more aggressive model.

<MermaidDiagram title="EPoller registration and dispatch">
sequenceDiagram
    participant Handler as InputNotificationHandler
    participant EPoller as EPoller
    participant Kernel as Linux Kernel

    Handler->>EPoller: registerFileDescriptor(fd, isRead)
    EPoller->>Kernel: epoll_ctl(EPOLL_CTL_ADD, fd, EPOLLIN)
    Kernel->>EPoller: epoll_wait() returns a ready event
    EPoller->>Handler: handleInputNotification()
</MermaidDiagram>

## epoll Is Not Automatically a Bottleneck Fix

Mercury is not a one-TCP-FD-per-player design. A `NetworkInterface` owns one
UDP socket while many `UDPChannel` objects represent logical peers. The likely
hotspots are:

- pressure on one socket's kernel receive queue;
- per-packet `recvfrom/sendto` syscall cost;
- Mercury parsing, ACKs, retransmission, fragmentation, and piggybacking;
- main-thread handler time;
- Entity/AOI/script work;
- tick budgets and tail latency.

If per-packet syscalls are proven to dominate, batching UDP I/O is the smaller
source-aligned change; replacing the execution model is not the first lever.

## Receive Path

<MermaidDiagram title="UDP receive path">
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

`PacketReceiver::handleInputNotification()` starts at
packet_receiver.cpp
and checks `maxSocketProcessingTimeStamps_`. The budget prevents one socket from
starving timers and other main-loop work.

## Send Path

Sending also uses per-packet `sendto()`
(packet_sender.cpp).
For `REASON_RESOURCE_UNAVAILABLE` or
`REASON_TRANSMIT_QUEUE_FULL`, the code waits with `select()` for 10 ms and
retries
(packet_sender.cpp).
Thus the socket is non-blocking, but the send path can still consume main-loop
time under load. Measure queue-full frequency and retry duration before
changing it.

## TCP Path

TCP follows the same Reactor model: non-blocking sockets, read/write
registration, write-event registration on partial sends, and unregistration on
completion. Framing uses a `uint16` length with an extended form for oversized
frames. Raw TCP, WebSocket, and encrypted `StreamFilter` are supported. TCP is
appropriate for login/tools/admin traffic, while the game channel depends on
Mercury reliable UDP.

## Backend Boundary

The source is a readiness-callback model, not a completion model. BigWorld has
few UDP sockets and many logical Channels, so `io_uring` is not a drop-in win.
It would add buffer lifetime, submission/completion queues, cancellation,
backpressure, and concurrency concerns. First identify whether the bottleneck
is syscalls, socket queues, protocol parsing, script execution, or Cell load.

## Verification Priorities

- Linux `EventPoller` remains level-triggered and dispatches to registered
  handlers.
- `PacketReceiver::handleInputNotification()` respects
  `maxSocketProcessingTime`.
- TCP registration, partial-send, and write-event removal stay non-blocking.
- `PacketSender::basicSendWithRetries()` cannot indefinitely block the main
  thread on EAGAIN/ENOBUFS.
- Handler time is treated as main-Reactor risk, not background I/O.
- Any backend change preserves Channel, Bundle, ACK, fragment, and
  request/reply semantics.

## Related Pages

- [Mercury reliable UDP](/architecture/mercury-reliable-udp)
- [Communication and RPC](/architecture/communication-rpc)
- [Network backpressure and fault injection](/architecture/network-backpressure-fault-injection)
- [Main loop, ticks, and event dispatch](/architecture/event-loop)
