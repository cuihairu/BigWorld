# Mercury Protocol and Reliable UDP

> **English** | [中文](mercury-reliable-udp.md)

<div class="arch-hero">

BigWorld's network core is not a thin layer of "epoll plus socket". It is a game
protocol runtime that Mercury implements on top of UDP. Message aggregation,
reliable and unreliable semantics, ACKs, resend, the out-of-order window,
fragmentation, piggyback and the entity-migration version number all live in the
same Channel model.

</div>

## Conclusion First

Mercury reliable UDP does not solve the problem of reliable RPC delivery. It
solves selective reliability in the high-frequency state synchronisation of an
MMO:

- Movement, AoI and attribute synchronisation need low latency, and much of the
  data is not worth resending once it is stale.
- Login, entity creation, cross-process migration and key lifecycle messages
  must be reliable.
- One UDP packet may mix reliable and unreliable messages.
- The external client link has tighter bandwidth, so on loss only reliable data
  is resent while unreliable data is stripped or dropped.
- The internal server link assumes low latency, high bandwidth and low loss, and
  can use a more aggressive reliability policy.

Source entries:

- `UDPChannel::Traits` explicitly distinguishes `INTERNAL` from `EXTERNAL`, see
  [udp_channel.hpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/udp_channel.hpp:57).
- The reliability entry points are `addResendTimer()`, `handleCumulativeAck()`,
  `handleAck()`, `checkResendTimers()` and `resend()`, see
  [udp_channel.hpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/udp_channel.hpp:151).
- The send window state lives in `smallOutSeqAt_`, `largeOutSeqAt_`,
  `oldestUnackedSeq_` and `unackedPackets_`, see
  [udp_channel.hpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/udp_channel.hpp:383).
- The receive window state lives in `inSeqAt_`, `bufferedReceives_`,
  `pFragments_` and `highestAck_`, see
  [udp_channel.hpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/udp_channel.hpp:440).

## Why Not Pure TCP

TCP provides ordered, reliable byte-stream transfer, but game state
synchronisation does not always require that "every piece of historical state
arrives in order".

What BigWorld actually needs is closer to:

<div class="decision-grid">
  <div class="decision-card">
    <h3>Reliable messages</h3>
    <p>Entity creation, destruction, migration, request/reply and key lifecycle events must arrive.</p>
  </div>
  <div class="decision-card">
    <h3>Unreliable messages</h3>
    <p>Position and orientation, and part of the AoI updates, have no resend value once stale. New state supersedes old state.</p>
  </div>
  <div class="decision-card">
    <h3>Mixed bundle</h3>
    <p>One send batch carries reliable and unreliable messages at the same time, cutting packet-header and syscall cost.</p>
  </div>
  <div class="decision-card">
    <h3>Entity migration</h3>
    <p>The Channel is not just a network connection. It also carries the version semantics of Base/Cell entity offload.</p>
  </div>
</div>

Going all-in on TCP would buy a simple reliable stream, at these costs:

- Head-of-line blocking would let old reliable data stall newer state behind it.
- There is no natural way to say "this message is reliable, that one is not".
- Several logical flows mean either several TCP connections, or re-multiplexing
  at the application layer.
- MMO state synchronisation often needs to resend by message semantics rather
  than uniformly per connection.

This is also the fundamental reason many game servers at the time chose UDP plus
a homegrown reliability layer.

## Channel Classification

The comments on `UDPChannel::Traits` already give the core design judgment:

- `INTERNAL`: server to server, low latency, high bandwidth, low loss.
- `EXTERNAL`: client to server, high latency, low bandwidth, high loss.
- An external channel resends only reliable data; unreliable data is stripped
  out of a lost packet and discarded.

This is not a mere network-type label. It is an input to resend policy,
bandwidth policy and packet content trimming.

<MermaidDiagram title="Mercury Channel semantics">
flowchart TD
  A[UDP Socket] --> B[NetworkInterface]
  B --> C[UDPChannel]
  C --> D{Traits}
  D -- INTERNAL --> E[Server-Server Channel]
  D -- EXTERNAL --> F[Client-Server Channel]
  E --> G[Both reliable and unreliable messages lean toward keeping throughput]
  F --> H[Resend reliable messages, strip unreliable payload]
  C --> I[Bundle message aggregation]
  I --> J[ReliableOrder]
  I --> K[Piggyback]
</MermaidDiagram>

## Reliability Levels

**Overview:** reliability is not a plain boolean. BigWorld defines four
reliability types and allows reliable and unreliable messages to be mixed in the
same Bundle. This is the key to MMO state synchronisation: a position update may
be dropped, but entity creation must be reliable.

**Source entry:** [bundle.hpp:27](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/bundle.hpp:27)

```cpp
// bundle.hpp:27 - reliability type definitions
enum ReliableType
{
    RELIABLE_NO,        // unreliable message
    RELIABLE_DRIVER,    // driving reliable packet
    RELIABLE_PASSENGER, // piggyback reliable message
    RELIABLE_CRITICAL   // critical reliable message
};
```

<div class="decision-table">

| Type | Meaning | Design purpose |
| --- | --- | --- |
| `RELIABLE_NO` | Unreliable message | May be dropped; suited to data that is worthless once stale |
| `RELIABLE_DRIVER` | Driving reliable packet | Gives the current UDPBundle a basis for reliable delivery |
| `RELIABLE_PASSENGER` | Piggyback reliable message | Sent reliably only when the same Bundle contains a driver |
| `RELIABLE_CRITICAL` | Critical reliable message | Equivalent to a driver, and marks the Bundle as critical |

</div>

**Flowchart:**

<MermaidDiagram title="Reliable message send flow">
sequenceDiagram
    participant Sender as Sender
    participant Bundle as UDPBundle
    participant Channel as UDPChannel
    participant Receiver as Receiver

    Sender->>Bundle: add message (RELIABLE_DRIVER)
    Bundle->>Bundle: mark as reliable
    Sender->>Bundle: add message (RELIABLE_PASSENGER)
    Bundle->>Bundle: piggyback reliable
    Sender->>Bundle: add message (RELIABLE_NO)
    Bundle->>Bundle: unreliable

    Bundle->>Channel: send Bundle
    Channel->>Channel: record in unackedPackets_
    Channel->>Receiver: UDP packet

    Receiver->>Receiver: parse messages
    Receiver->>Channel: send ACK
    Channel->>Channel: remove from unackedPackets_
</MermaidDiagram>

**Detailed walkthrough:**

1. **RELIABLE_DRIVER**: the first reliable message, giving the whole Bundle a
   basis for reliable delivery. Without a driver, passenger messages in the
   Bundle are not resent.

2. **RELIABLE_PASSENGER**: a piggyback message, sent reliably only when the
   Bundle contains a driver. This allows batching several reliable messages and
   cuts ACK overhead.

3. **RELIABLE_CRITICAL**: a critical message, equivalent to a driver, and marks
   the Bundle as critical. A critical Bundle occupies `unackedCriticalSeq_`, used
   to decide whether to wait for the critical message to be acknowledged.

4. **Mixed bundle**: one Bundle may mix reliable and unreliable messages. For
   example, a movement update (unreliable) and an attribute change (reliable) can
   be sent in the same packet.

**Why not TCP:**

- TCP is connection-level reliable and cannot distinguish "this message is
  reliable, that one is not"
- TCP head-of-line blocking would let old reliable data stall newer state behind
  it
- MMO state synchronisation needs to pick reliability per message semantics, not
  resend uniformly per connection

## Send Window

**Overview:** Mercury keeps its own sliding window and its own set of
unacknowledged packets. This is not a simple "send and wait for a callback"
model. It is a window mechanism similar to TCP's, optimised for a game protocol.

**Source entry:** [udp_channel.hpp:383](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/udp_channel.hpp:383)

```cpp
// udp_channel.hpp:383 - send window state
class UDPChannel
{
private:
    // send window state
    SeqNum smallOutSeqAt_;      // next sequence number to send (without overflow)
    SeqNum largeOutSeqAt_;      // next sequence number to send (with overflow)
    SeqNum oldestUnackedSeq_;   // oldest unacknowledged packet
    UnackedPackets unackedPackets_;  // set of unacknowledged packets
    int windowSize_;            // current window size
    int maxWindowSize_;         // maximum window size

    // resend related
    int roundTripTime_;         // RTT estimate
    int resendTimeout_;         // resend timeout
    int lastSendTime_;          // last send time
};
```

**Flowchart:**

<MermaidDiagram title="Send window sliding mechanism">
stateDiagram-v2
    [*] --> Idle
    Idle --> Sending: data to send
    Sending --> WindowFull: window full
    WindowFull --> Sending: ACK received
    Sending --> Resending: timed out unacknowledged
    Resending --> Sending: ACK received
    Sending --> Idle: no data

    state Sending {
        [*] --> CheckWindow
        CheckWindow --> SendPacket: window has room
        CheckWindow --> WaitACK: window full
        SendPacket --> RecordUnacked
        RecordUnacked --> CheckWindow
    }

    state Resending {
        [*] --> FindOldest
        FindOldest --> ResendPacket
        ResendPacket --> UpdateTimeout
        UpdateTimeout --> [*]
    }
</MermaidDiagram>

**Detailed walkthrough:**

1. **Sequence management**: `smallOutSeqAt_` and `largeOutSeqAt_` distinguish
   ordinary packets from overflow packets. Overflow packets exist for special
   handling when the window is full.

2. **Set of unacknowledged packets**: `unackedPackets_` holds every packet sent
   but not yet acknowledged. Each packet records its send time and its list of
   reliable messages.

3. **Window size**: `windowSize_` adjusts dynamically with network conditions and
   ACK speed. `maxWindowSize_` is the ceiling.

4. **Resend mechanism**: if a packet has not been acknowledged within
   `resendTimeout_`, resend is triggered. The resend timeout is based on the RTT
   estimate.

5. **RTT estimation**: `roundTripTime_` is estimated from ACK time differences,
   and is used to adjust the resend timeout and the window size.

### Send Flow

**Source entry:** [udp_channel.cpp:1513](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/udp_channel.cpp:1513)

```cpp
// udp_channel.cpp:1513 - send a Bundle
void UDPChannel::send( Bundle * pBundle )
{
    UDPBundle * pUDPBundle = static_cast<UDPBundle *>(pBundle);

    // 1. Check whether the window has room
    if (this->windowSize() >= this->maxWindowSize())
    {
        // window full, wait for ACK
        return;
    }

    // 2. Allocate a sequence number
    SeqNum seq = largeOutSeqAt_++;

    // 3. Record it in the set of unacknowledged packets
    unackedPackets_.add( seq, pUDPBundle );

    // 4. Write the packet header
    this->writeFlags( pUDPBundle );
    this->writeFooter( pUDPBundle );

    // 5. Send
    pUDPBundle->send();
}
```

**Key details:**

- When the window is full, new messages queue and wait; they are not dropped
- Sequence numbers increase monotonically, so the receiver can detect
  out-of-order and duplicate packets
- The set of unacknowledged packets supports fast lookup and resend
- The packet header carries ACK information, used to acknowledge packets sent
  earlier

## Receive Window

**Overview:** the receive window handles out-of-order packets, detects duplicates
and maintains receive order. This is the core of reliable UDP: brief reordering
is allowed, but delivery ends up in order.

**Source entry:** [udp_channel.cpp:1208](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/udp_channel.cpp:1208)

```cpp
// udp_channel.cpp:1208 - add to the receive window
UDPChannel::AddToReceiveWindowResult UDPChannel::addToReceiveWindow(
    SeqNum seq, Packet * pPacket, bool isReliable )
{
    // 1. Check that the sequence number is in the valid range
    if (seq >= inSeqAt_ + windowSize_ || seq < oldestUnackedSeq_)
    {
        return ADD_TO_RECEIVE_WINDOW_RESULT_OUT_OF_WINDOW;
    }

    // 2. Check the source address
    if (!this->checkSourceAddress( pPacket ))
    {
        return ADD_TO_RECEIVE_WINDOW_RESULT_CORRUPT;
    }

    // 3. For non-piggyback packets, enqueue an ACK
    if (!pPacket->isPiggyback())
    {
        acksToSend_.push_back( seq );
    }

    // 4. Check whether this is the next expected packet
    if (seq == inSeqAt_)
    {
        // advance the receive window
        inSeqAt_++;

        // check whether buffered out-of-order packets can be chained
        while (bufferedReceives_.contains( inSeqAt_ ))
        {
            pPacket = bufferedReceives_.get( inSeqAt_ );
            this->processPacket( pPacket );
            inSeqAt_++;
        }

        return ADD_TO_RECEIVE_WINDOW_RESULT_NEXT;
    }

    // 5. Check whether this is a duplicate packet
    if (seq < inSeqAt_)
    {
        return ADD_TO_RECEIVE_WINDOW_RESULT_DUPLICATE;
    }

    // 6. Out-of-order packet, buffer it in the receive window
    bufferedReceives_.add( seq, pPacket );
    return ADD_TO_RECEIVE_WINDOW_RESULT_BUFFERED;
}
```

**Flowchart:**

<MermaidDiagram title="Receive window processing flow">
sequenceDiagram
    participant Receiver as Receiver
    participant Window as Receive Window
    participant Buffer as Buffer
    participant Handler as Message Handler

    Receiver->>Window: addToReceiveWindow(seq)

    alt invalid seq
        Window->>Receiver: OUT_OF_WINDOW
    else seq == inSeqAt_
        Window->>Window: inSeqAt_++
        loop while there are contiguous buffered packets
            Window->>Buffer: get(inSeqAt_)
            Buffer->>Handler: processPacket()
            Window->>Window: inSeqAt_++
        end
        Window->>Receiver: NEXT
    else seq < inSeqAt_
        Window->>Receiver: DUPLICATE
    else seq is inside the window
        Window->>Buffer: add(seq, packet)
        Window->>Receiver: BUFFERED
    end
</MermaidDiagram>

**Detailed walkthrough:**

1. **Window range**: `inSeqAt_` is the expected next packet sequence number, and
   `oldestUnackedSeq_` is the oldest unacknowledged packet. The window size limits
   how many out-of-order packets may be buffered.

2. **Duplicate detection**: `seq < inSeqAt_` means the packet has already been
   received and is dropped. This prevents a resent packet from being processed
   twice.

3. **Out-of-order buffering**: `bufferedReceives_` buffers out-of-order packets.
   When `inSeqAt_` advances, the code checks whether contiguous buffered packets
   can be chained together.

4. **ACK generation**: after receiving a packet, its sequence number is appended
   to the `acksToSend_` queue and carried along on the next send.

5. **Address check**: `checkSourceAddress()` prevents forged packets. If
   `shouldAutoSwitchToSrcAddr_` is allowed, the address can switch according to
   the channel version.

### Receive Window State Machine

<MermaidDiagram title="Receive window state machine">
stateDiagram-v2
    [*] --> ValidateSeq
    ValidateSeq --> Corrupt: invalid seq
    ValidateSeq --> CheckAddress: seq valid
    CheckAddress --> Corrupt: wrong source address
    CheckAddress --> AckQueued: address valid
    AckQueued --> Next: seq == inSeqAt_
    AckQueued --> Duplicate: seq < inSeqAt_
    AckQueued --> Buffered: seq inside window but out of order
    AckQueued --> OutOfWindow: seq beyond the window
    Next --> AdvanceWindow
    AdvanceWindow --> AttachBuffered
    AttachBuffered --> [*]
    Duplicate --> [*]
    Buffered --> [*]
    OutOfWindow --> [*]
    Corrupt --> [*]
</MermaidDiagram>

This state machine is one of the cores of reliable UDP. It guarantees that
reliable messages recover their order along the Channel sequence number, while
still letting brief reordering enter the buffer.

## ACK and Cumulative ACK

`writeFlags()` and `writeFooter()` write Channel metadata into the packet, see:

- [udp_channel.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/udp_channel.cpp:1513)
- [udp_channel.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/udp_channel.cpp:1580)

Key policy:

- Every Channel packet writes `FLAG_ON_CHANNEL`.
- An indexed Channel additionally writes `FLAG_INDEXED_CHANNEL`, `ChannelID` and
  `ChannelVersion`.
- If there is room, a cumulative ACK is written, expressing that everything before
  a given sequence number has been received.
- If there is still room, a limited number of discrete ACKs is written.
- The first internal reliable packet writes `FLAG_CREATE_CHANNEL`.

This is a very typical "cumulative acknowledgement plus selective
acknowledgement" hybrid strategy:

- The cumulative ACK compresses a contiguous acknowledged range.
- Discrete ACKs handle holes from reordering.
- ACKs are piggybacked onto outbound packets, cutting standalone ACK packets.
- The number of ACKs is bounded by packet space, so ACKs cannot squeeze the
  payload out.

## Resend Path

The policy in `UDPChannel::resend()` is not a mechanical resend of the original
packet, see
[udp_channel.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/udp_channel.cpp:1156).

For an external channel, if the unacknowledged packet is not a fragment and the
current send window will not overflow, it tries to piggyback the reliable content
onto the next outgoing Bundle:

<MermaidDiagram title="Resend strategy">
flowchart TD
  A[unacked packet timeout or ack hole] --> B{External channel?}
  B -- no --> E[sendUnacked resend original packet]
  B -- yes --> C{Non-fragment and window has room?}
  C -- yes --> D[piggyback onto a new Bundle]
  C -- no --> E
  D --> F[handleAck original sequence]
  E --> G[sendPacket isResend=true]
</MermaidDiagram>

This reflects the bandwidth-first policy of the external client link: piggyback
when you can instead of resending standalone, and resend only the reliable content
rather than repeating unreliable data that has already gone stale.

## Indexed Channel

`UDPChannel` supports indexed channels, see the source comments at
[udp_channel.hpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/udp_channel.hpp:364).

The point of an indexed Channel is to reuse several logical channels between the
same pair of addresses. The typical case is Base and Cell entities, which need
several independent flows that an address alone cannot distinguish.

It has two key fields:

- `ChannelID id_`: the logical channel identifier.
- `ChannelVersion version_`: records how many times the indexed Channel has been
  offloaded, used to recognise stale packets and to recover the latest entity
  information in a recovery scenario.

This matters a lot for understanding entity migration. BigWorld's network
connection is not a pure transport concept. It embeds entity migration and
recovery semantics.

## Regular and Irregular

`isLocalRegular_` and `isRemoteRegular_` affect ACK and resend policy, see
[udp_channel.hpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/udp_channel.hpp:462).

Meaning:

- When the local side is irregular, the local side periodically checks for
  resends and leans toward sending ACKs immediately.
- When the remote side is regular, packet loss can be inferred from ACK holes
  instead of relying on silence timeout alone.

This shows that Mercury's reliability layer exploits game server communication
patterns: some Channels send continuously, while others send intermittently.
Different traffic shapes should use different ACK and resend policies.

## Relationship with Bundle

`UDPChannel::newBundle()` creates a `UDPBundle`, see
[udp_channel.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/udp_channel.cpp:1501).

`UDPBundle` is responsible for:

- `startMessage()`, `startRequest()`, `startReply()`.
- Recording `ReliableOrder`.
- `preparePackets()` slicing the message sequence into UDP packets.
- `piggyback()` hanging resent content onto a new packet.
- `ack_` handling off-channel ack.

Source: [udp_bundle.hpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/udp_bundle.hpp:37).

So the Channel owns connection state and the reliable window, while the Bundle
owns message aggregation and packet arrangement. Only together are they Mercury's
game network protocol.

## Trade-offs in the Source

Mercury reliable UDP is deeply coupled to the engine state model:

- TCP cannot express mixed reliable/unreliable semantics within the same logical
  flow.
- Multi-process MMO communication inside the server needs controllable protocol
  headers, observable statistics and engine-level debugging.
- The Python scripting layer and the EntityDef type system need to stay tightly
  bound to the network protocol.
- Channel version, critical reliability, piggyback, fragmentation and ACKs all
  take part in the correctness of entity migration, recovery and internal RPC.

The source costs are just as explicit:

- Protocol complexity is high, and correctness depends on heavy boundary testing.
- It is disconnected from the general ecosystem, so external tooling cannot parse
  it directly.
- Congestion, path MTU, NAT, encryption and attack surface need to be analysed
  together with the external entry point, PacketFilter and deployment topology.
- An I/O backend can only change how data is sent and received. It cannot replace
  the Channel's reliable ordering and entity semantics.

## Source-Code Verification Focus

Testing reliable UDP cannot stop at "can send and receive". At minimum it must
cover:

- Contiguous ACK, discrete ACK, ACK loss and ACK delay.
- Out-of-order packets, duplicate packets, out-of-window packets and corrupt
  packets.
- The boundaries of fragment packet resend and piggyback.
- The resend policy difference between `INTERNAL` and `EXTERNAL`.
- Dropping of stale-version packets on an indexed Channel.
- Recovery behaviour when `RELIABLE_CRITICAL` is unacknowledged.
- Send window overflow and the `maxWindowSize()` boundary.

The source already has a network unit-test directory, for example
`lib/network/unit_test/test_receive_window.cpp`, `test_reliable.cpp`,
`test_fragment.cpp`, `test_channel_version.cpp`. A later testing chapter will
analyse the coverage and the gaps.

## Chapter Boundary

This chapter covers only the Channel reliability layer. The next chapter continues
with Mercury's communication abstractions: how `InterfaceElement` defines a
message header, how `Bundle` organises RPC, and how request/reply connects with
the reliability layer.
