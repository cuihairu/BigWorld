# Network Backpressure and Fault Injection

> **English** | [中文](network-backpressure-fault-injection.md)

<div class="arch-hero">

BigWorld's network layer is not merely epoll plus UDP. Server stability depends
on how receive budgets, full send queues, artificial loss/latency, channel
overflow, reliable resends, rate limiting, and tick fairness interact. This
chapter focuses on backpressure and fault injection.

</div>

## Key Takeaways

The network layer already has several mechanisms that protect the main loop:

- `PacketReceiver` processes multiple packets for one readiness notification, but `maxSocketProcessingTime` bounds the processing interval.
- `BWMessageForwarder` defaults its maximum socket-processing time to `1 / gameUpdateHertz`.
- `PacketSender` supports artificial packet loss and delay for reliable-UDP testing.
- `EAGAIN` / `ENOBUFS` on send waits about 10 ms through `select()` before retrying.
- The network interface exposes artificial loss, latency, and maximum socket-processing time through Watcher.
- Network unit tests use `dropNextSend()` extensively to exercise the reliable layer.

Key source code:

- Receive budget: packet_receiver.cpp.
- `maxSocketProcessingTime()` watcher: packet_receiver.cpp.
- `BWMessageForwarder` processing-budget configuration: bw_message_forwarder.cpp.
- Artificial loss/latency: packet_sender.cpp.
- Full-send-queue wait: packet_sender.cpp.
- Per-packet `sendto()`: packet_sender.cpp.
- `dropNextSend()` in network tests, for example: test_reliable.cpp.

## Receive-Side Backpressure

**Summary**: `PacketReceiver::handleInputNotification()` repeatedly calls
`processSocket()` after a readable-socket notification, but only within a time
budget. This is not kernel backpressure; it protects main-thread fairness and
prevents one UDP burst from starving timers, frequent tasks, scripts, and other
messages.

**Source entry**: packet_receiver.cpp:83

```cpp
// packet_receiver.cpp:83 - receive budget
void PacketReceiver::handleInputNotification()
{
    // 1. Record the start time
    uint64 startTime = timestamp();

    // 2. Drain packets from the socket
    while (true)
    {
        // 3. Process one packet
        if (!this->processSocket())
        {
            break;
        }

        // 4. Measure elapsed time
        uint64 elapsed = timestamp() - startTime;

        // 5. Stop when over budget
        if (elapsed > maxSocketProcessingTimeStamps_)
        {
            WARNING_MSG( "PacketReceiver::handleInputNotification: "
                "Spent too long processing socket (%.2fms), "
                "source=%s, processed=%d packets, "
                "remaining in queue=%d\n",
                float(elapsed) / 1000.0f,
                lastSourceAddr_.ipAsString(),
                numPacketsProcessed_,
                receiveQueueSize() );
            break;
        }
    }
}
```

<MermaidDiagram title="Receive-budget processing">
flowchart TD
    A[EPOLLIN event] --> B[handleInputNotification]
    B --> C[record start time]
    C --> D{socket has data?}
    D -- no --> E[return to dispatcher]
    D -- yes --> F[processSocket]
    F --> G[measure elapsed time]
    G --> H{over maxSocketProcessingTime?}
    H -- no --> D
    H -- yes --> I[log warning]
    I --> E
</MermaidDiagram>

Detailed interpretation:

1. **Time bound**: `maxSocketProcessingTimeStamps_` caps one socket-processing interval. Its default is derived from `gameUpdateHertz`.

2. **Loop**: within budget, `processSocket()` runs repeatedly. Time is checked after every packet.

3. **Timeout path**: the warning includes processing duration, source address, number of packets processed, and remaining queue size.

4. **Fairness**: a non-empty receive queue can remain for the next dispatcher round, giving timers, frequent tasks, scripts, and other messages a chance to run.

5. **Tick relationship**: `maxSocketProcessingTime` defaults to `1 / gameUpdateHertz`, the length of one tick. With `gameUpdateHertz = 10`, one tick is 100 ms.

### `BWMessageForwarder` configuration

**Source entry**: bw_message_forwarder.cpp:47

```cpp
// bw_message_forwarder.cpp:47 - socket-processing budget
void BWMessageForwarder::init( NetworkInterface & networkInterface )
{
    // 1. Read configuration
    float maxInternalSocketProcessingTime = 
        Config::maxInternalSocketProcessingTime();
    float gameUpdateHertz = Config::gameUpdateHertz();

    // 2. Use the tick-derived default when the configured value is negative
    if (maxInternalSocketProcessingTime < 0)
    {
        maxInternalSocketProcessingTime = 1.f / gameUpdateHertz;
    }

    // 3. Install it on the NetworkInterface
    networkInterface.maxSocketProcessingTime( 
        maxInternalSocketProcessingTime );
}
```

Key details:

- configuration can override the default, providing flexibility;
- the default is tied to game-tick frequency to preserve fairness;
- Watcher can adjust it dynamically for debugging.

## Relationship to the Tick

`BWMessageForwarder` reads `maxInternalSocketProcessingTime` and
`gameUpdateHertz`. If the configured value is negative, it uses
`1.f / gameUpdateHertz` and calls
`networkInterface.maxSocketProcessingTime()`:
bw_message_forwarder.cpp.

The receive budget is therefore not an isolated parameter; it is tied to the
game-tick frequency.

Design implications:

- with `gameUpdateHertz = 10`, one tick is 100 ms;
- the internal socket budget can be inferred from tick length;
- a burst of packets cannot consume the main loop indefinitely.

Observability should split this metric into:

- network-processing time per tick;
- packets processed per socket notification;
- number of over-budget exits;
- receive-queue size;
- handler dispatch duration.

## Send-Side Backpressure

The send path uses `sendto()`:

```cpp
int len = socket_.sendto( pPacket->data(), pPacket->totalSize(), addr.port, addr.ip );
```

Source: packet_sender.cpp.

Error mapping:

- `ECONNREFUSED` -> `REASON_NO_SUCH_PORT`
- `EAGAIN` -> `REASON_RESOURCE_UNAVAILABLE`
- `ENOBUFS` -> `REASON_TRANSMIT_QUEUE_FULL`
- `EMSGSIZE` -> `REASON_MESSAGE_TOO_LONG`

Source: packet_sender.cpp.

On `REASON_RESOURCE_UNAVAILABLE` or `REASON_TRANSMIT_QUEUE_FULL`,
`basicSendWithRetries()`:

- logs a `Transmit queue full` warning;
- uses `select()` to wait up to 10 ms for socket writability;
- retries afterward.

Source: packet_sender.cpp.

This is the source-level trade-off on the send side:

- simple and robust;
- absorbs transient kernel transmit-queue pressure;
- but can stall the main thread by up to 10 ms in the send path.

Measure this path's frequency and wait duration before concluding that send-side
backpressure is the source of main-thread jitter.

## Artificial Loss and Latency

`PacketSender::rescheduleSend()` supports two fault-injection modes:

- `dropNextSend()` or probability loss via `dropPerMillion()`;
- artificial delay via `minLatencyMillion()` / `maxLatencyMillion()`.

Source: packet_sender.cpp.

A delay of at least 2 ms creates a `RescheduledSender`:
packet_sender.cpp.

Design value:

- makes Mercury reliable-UDP retransmission testable;
- simulates public-network loss and delay;
- verifies ACKs, piggybacks, fragments, and receive windows;
- enables unit tests without external netem.

Network tests contain many `dropNextSend()` uses; the reliable-layer test begins
at test_reliable.cpp.

## Fault-Injection and Test Boundaries

BigWorld's artificial injection is network-focused:

- drop the next packet;
- probabilistic packet loss;
- delayed packets;
- flood/mangle/fragment unit-test coverage.

It is not a complete chaos framework:

- cannot directly simulate a stalled DBApp;
- cannot fully simulate clock jumps;
- cannot automatically validate entity recovery after CellApp death;
- cannot inject partial network partitions across processes;
- lacks a unified scenario description and automatic report.

It is excellent for reliable-UDP unit tests, but not equivalent to a modern
chaos-testing platform.

## Backpressure Chain

<MermaidDiagram title="Network backpressure chain">
flowchart LR
  A[Kernel RX Queue] --> B[PacketReceiver budget]
  B --> C[Mercury decode]
  C --> D[Channel reliable state]
  D --> E[Message handler]
  E --> F[Python/Entity logic]
  F --> G[Bundle output]
  G --> H[PacketSender]
  H --> I{sendto result}
  I -- success --> J[Kernel TX Queue]
  I -- EAGAIN/ENOBUFS --> K[select wait 10ms retry]
  I -- EMSGSIZE --> L[MTU/message too long]
  H --> M[artificial drop/latency]
</MermaidDiagram>

Backpressure is therefore not just the socket. Possible bottlenecks include:

- kernel receive queue;
- per-notification processing budget;
- Mercury packet parsing;
- reliable-layer resend and ACK logic;
- handler execution;
- Python scripts;
- Entity/AOI updates;
- bundle aggregation;
- kernel transmit queue.

Observe each stage first; otherwise every problem is easily misattributed to
epoll or the absence of io_uring.

## Why There Is No io_uring Backpressure Model

`io_uring` did not exist in BigWorld's design era. More importantly, the current
model is readiness events plus synchronous handlers:

- epoll announces readability;
- the main thread synchronously processes packets;
- handlers run immediately;
- timers and network work share one dispatcher.

`io_uring` is a completion model and introduces:

- submit queues;
- completion queues;
- buffer lifetimes;
- multiple outstanding I/O operations;
- decoupling between completion events and handler scheduling.

It changes the execution model, not merely the `recvfrom()` implementation.

## Source Trade-Offs

The source intentionally exposes network pressure to the main loop:

- `PacketReceiver::maxSocketProcessingTime` bounds one readiness drain and protects tick fairness.
- `PacketSender::basicSendWithRetries()` retries with a short wait on send failure, but can still occupy the main thread.
- Artificial loss/latency and `dropNextSend()` make reliable-layer loss and reordering reproducible.
- Channel-overflow configuration turns reliable-window pressure into an observable, handleable state.

Costs:

- a full send queue can still make the main thread wait;
- fault injection is largely confined to engine test hooks and cannot cover all real network variance;
- the receive budget bounds socket processing, not business time inside handlers;
- changing the I/O backend affects buffer lifetimes and handler scheduling, not just a syscall replacement.

## Source-Code Verification Focus

Backpressure and fault-injection tests should cover pressure propagation:

- `PacketReceiver::handleInputNotification()` should record packet count, duration, and over-budget exits.
- `PacketSender::basicSendWithRetries()` should cover EAGAIN, ENOBUFS, select waiting, and final failure.
- `dropNextSend()` and artificial loss/latency should reproduce lost ACKs, lost data, lost replies, and reordering.
- When a channel reaches its overflow threshold, configuration should drop, block, or report—not silently corrupt reliable state.
- Login floods, malformed packets, oversized bundles, and invalid message IDs should be rejected before entering heavy business logic.
- Fault scenarios should cover both external client channels and internal server channels.

## Chapter Boundary

This chapter analyzes network backpressure and fault injection. It complements
[Network I/O model selection](/architecture/network-io-model): that chapter
explains the epoll/recvfrom model, while this one explains runtime tick
protection and reliable-layer validation.

## Related Documentation

- Network I/O model: [Network I/O model selection](/architecture/network-io-model).
- Mercury reliable UDP: [Mercury reliable UDP](/architecture/mercury-reliable-udp).
- Communication and RPC: [Communication abstraction and RPC](/architecture/communication-rpc).
- Main loop and dispatch: [Main loop, ticks, and event dispatch](/architecture/event-loop).
- Testing and fault injection: [Testing, fault injection, and coverage boundaries](/architecture/testing-fault-injection-coverage).
