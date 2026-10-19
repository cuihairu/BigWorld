# Communication Abstractions and RPC

> **English** | [中文](communication-rpc.md)

<div class="arch-hero">

Mercury's communication layer is not a gRPC-style IDL generation system. It is
an inter-process RPC mechanism composed of C++ macro interfaces,
`InterfaceElement` message descriptors, `Bundle` message streams and
`InputMessageHandler` callbacks. It serves high-frequency, low-latency,
mixed-reliability message communication inside an MMO engine.

</div>

## Conclusion First

BigWorld's communication model can be summarised as:

<div class="flow-strip">
  <span class="flow-node">Interface definition</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">InterfaceElement</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">Bundle writes messages</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">Channel sends</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">BundleProcessor dispatches</span>
  <span class="flow-arrow">-></span>
  <span class="flow-node">Handler executes</span>
</div>

Key sources:

- `InterfaceElement` describes the message id, length encoding and handler, see
  [interface_element.hpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/interface_element.hpp:78).
- `InterfaceElement::headerSize()` computes the message header size, see
  [interface_element.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/interface_element.cpp:103).
- `Bundle` is a message sequence and derives from `BinaryOStream`, see
  [bundle.hpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/bundle.hpp:64).
- `UDPBundle` handles UDP packet marshalling, reliable message ordering and
  piggybacking, see
  [udp_bundle.hpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/udp_bundle.hpp:37).
- `UDPBundleProcessor` looks up the `InterfaceTable` by message id and
  dispatches, see
  [udp_bundle_processor.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/udp_bundle_processor.cpp:70).
- The interface registration macros are at
  [interface_macros.hpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/interface_macros.hpp:193).

## What an InterfaceElement Is

`InterfaceElement` is the smallest metadata unit of the Mercury message
protocol. It is not a business object but a message header descriptor.

It contains:

- `MessageID id_`: the message number.
- `lengthStyle_`: fixed length, variable length, callback length, or invalid
  message.
- `lengthParam_`: the fixed length size, or how many bytes the variable-length
  field occupies.
- `name_`: a name for debugging and statistics.
- `pHandler_`: the handler invoked when the message arrives.
- `shouldProcessEarly_`: whether out-of-order early processing is allowed.

The length types are defined at
[interface_element.hpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/interface_element.hpp:28):

<div class="decision-table">

| Type | Meaning | Where it applies |
| --- | --- | --- |
| `FIXED_LENGTH_MESSAGE` | The message body length is fixed | Small struct messages, shortest header |
| `VARIABLE_LENGTH_MESSAGE` | The header carries a length field | Variable payloads such as strings, arrays, entity data |
| `CALLBACK_LENGTH_MESSAGE` | The handler decides the length dynamically from its source | Protocol compatibility, special handshakes, context-dependent messages |
| `INVALID_MESSAGE` | Uninitialised or illegal message | Defensive state |

</div>

`updateLengthDetails()` asks the handler for the real stream size in the
callback length case, see
[interface_element.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/interface_element.cpp:64).

## Message Header Encoding

The logic of `InterfaceElement::headerSize()` is straightforward:

- Fixed length message: only the `MessageID`.
- Variable length message: `MessageID + lengthParam_`.
- Other types: returns `-1`.

Source: [interface_element.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/interface_element.cpp:103).

This shows Mercury's wire format is highly compact. It has none of Protobuf's
self-describing field tag plus wire type encoding; instead it relies on both
sides sharing an interface table and EntityDef.

Advantages:

- Small headers, which suits high-frequency game messages.
- Very fast encoding for fixed length messages.
- The message id can index the `InterfaceTable` directly.

Costs:

- Weak self-describing capability for the protocol.
- Version evolution depends on deployment discipline and MD5/interface
  consistency checks.
- Packet-capture debugging needs an engine-side parser.

## What a Bundle Is

`Bundle` is Mercury's send-side builder. It derives from `BinaryOStream`, so
business code can write message arguments exactly like writing to a binary
stream.

The core interfaces are at
[bundle.hpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/bundle.hpp:80):

- `startMessage()`: start an ordinary message.
- `startRequest()`: start a request message, registering the reply handler,
  timeout and reliability.
- `startReply()`: start a reply message.
- `sendMessage()` / `sendRequest()`: templated wrappers that write an argument
  struct into the bundle.

<MermaidDiagram title="Bundle message writing flow">
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

This API design fits C++ game server code: the caller does not manage headers
and length fields directly, it writes messages according to the interface
metadata.

## UDPBundle and TCPBundle

Mercury's `Bundle` is an abstract base class, with different implementations for
UDP and TCP:

- `UDPBundle`: aimed at UDP packets, reliable ordering, piggybacking,
  fragmentation and ACKs.
- `TCPBundle`: aimed at a TCP stream, organising messages into length-framed
  units.

`UDPBundle` additionally maintains:

- `ReliableVector reliableOrders_`: reliable message ordering.
- `BundlePiggybacks piggybacks_`: resent piggyback content.
- `curIE_`, `msgLen_`, `msgBeg_`: current message encoding state.
- `ack_`: off-channel ACK.
- `numReliableMessages_`: reliable message statistics.

Source: [udp_bundle.hpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/udp_bundle.hpp:143).

This shows the RPC abstraction is unified on top while the transport below is
not fully transparent: reliability, packet boundaries and piggybacking remain
part of the UDP semantics.

## Request and Reply

`Bundle::startRequest()` takes a `ReplyMessageHandler`, an `arg`, a `timeout` and
a `ReliableType`, with a default timeout of 5 seconds, see
[bundle.hpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/bundle.hpp:21).

This is a lightweight request/reply:

- The request is written into the current Bundle.
- The local `RequestManager` records the awaited reply.
- The reply message uses the internal `InterfaceElement::REPLY`, see
  [interface_element.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/interface_element.cpp:24).
- After the timeout, the handler receives the failure result.

It is not an HTTP/gRPC-style independent connection request model; it is a
message stream embedded in a Mercury Channel.

## The Receive Dispatch Chain

The receive side roughly follows this chain:

<MermaidDiagram title="Mercury receive dispatch chain">
flowchart TD
  A[PacketReceiver recvfrom] --> B[NetworkInterface process packet]
  B --> C[UDPChannel addToReceiveWindow]
  C --> D[UDPBundleProcessor]
  D --> E[InterfaceTable message id lookup]
  E --> F[InterfaceElement length decode]
  F --> G[InputMessageHandler]
  G --> H[BaseApp/CellApp/DBApp handler]
</MermaidDiagram>

`UDPBundleProcessor` uses `iter.msgID()` to find the matching
`InterfaceElementWithStats` in `interfaceTable`, then copies/updates the length
details; source at
[udp_bundle_processor.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/udp_bundle_processor.cpp:70).

This chain has two key boundaries:

- Message handling normally runs synchronously on the Reactor main thread.
- Handler duration directly affects subsequent networking, Timers and Ticks.

So Mercury's RPC is not "async tasks that get concurrency for free", it is
"network-event-driven synchronous callbacks". That matters a lot for the
threading model and for performance analysis.

## How It Talks to the Front End

If the "front end" is made concrete as the BigWorld client, it neither connects
straight to CellApp nor to an arbitrary entity instance - it communicates through
the BaseApp that hosts the `Proxy`.

The key entry points are visible directly in the source:

- The client downstream protocol is defined in `ClientInterface`, containing
  `createEntity`, `updateEntity`, `entityMethod`, `entityProperty`, `enterAoI`,
  `leaveAoI` and similar messages, see
  [client_interface.hpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/connection/client_interface.hpp:60).
- The client's upstream external protocol is defined in `BaseAppExtInterface`,
  containing `baseEntityMethod`, `cellEntityMethod`, movement sync and login
  related messages, see
  [baseapp_ext_interface.hpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/connection/baseapp_ext_interface.hpp:26).
- `Proxy` is the special Base that holds the client connection, see
  [proxy.hpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/baseapp/proxy.hpp:49).

The chain is not "a browser requests an API" but "the client connects to a
Proxy, and the Proxy forwards entity-semantic messages".

### Server to Client

When the server sends messages to the client, the core is not that any entity
holds a socket directly, but:

1. After the player connects, the client channel is attached to some `Proxy`.
2. `Proxy` maintains `pClientChannel_` and converts Cell or Base visible
   messages into `ClientInterface` messages for the client, see
   [proxy.hpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/baseapp/proxy.hpp:79).
3. `Proxy::createBasePlayer()` first sends `ClientInterface::createBasePlayer`,
   then streams down the `FROM_BASE_TO_CLIENT_DATA` properties, see
   [proxy.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/baseapp/proxy.cpp:1630).
4. Messages from the Cell such as `tickSync`, `avatarUpdate*`, `createEntity`,
   `updateEntity`, `spaceData` are forwarded by `Proxy` to the client, see
   [proxy.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/baseapp/proxy.cpp:1653)
   and
   [common_client_interface.hpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/connection/common_client_interface.hpp:34).

The receive entry point on the client side is `ServerConnection`:

- `createEntity()` / `createEntityDetailed()` decode the entity id, type,
  position and remaining property stream, see
  [server_connection.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/connection/server_connection.cpp:2633).
- `updateEntity()` hands the property stream to the upper handler, see
  [server_connection.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/connection/server_connection.cpp:2697).
- `entityMethod()` / `entityProperty()` use `ClientInterface::Range` to map the
  message number back to an exposed method/property id, then pass it to the
  client entity layer, see
  [server_connection.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/connection/server_connection.cpp:1032).

So the "entity creation, property refresh, method callback" that the front end
sees are essentially binary streams interpreted together by `ClientInterface` and
EntityDef.

### Client to Server

Client traffic likewise does not address a game logic object directly - it goes
to the current `Proxy`:

- `ServerConnection::startBasePlayerMessage()` packs with
  `BaseAppExtInterface::baseEntityMethodRange`, see
  [server_connection.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/connection/server_connection.cpp:833).
- `ServerConnection::startCellEntityMessage()` packs with
  `BaseAppExtInterface::cellEntityMethodRange` and writes the target `entityID`
  into the stream, see
  [server_connection.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/connection/server_connection.cpp:875).

On the server side, `Proxy` splits it into two paths:

- `Proxy::baseEntityMethod()` resolves the exposed Base method on the current
  Proxy/Base and calls the script method, see
  [proxy.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/baseapp/proxy.cpp:2536).
- `Proxy::cellEntityMethod()` wraps the call as
  `CellAppInterface::runExposedMethod` and forwards it to the matching Cell
  entity, see
  [proxy.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/baseapp/proxy.cpp:2491).

The Cell side then completes the real method dispatch in
`Entity::runExposedMethod()` and `runMethodHelper()`, see
[entity.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/entity.cpp:5399).

This means the communication abstraction between the client and the "front end
logic entity" is:

- First find the Proxy the player is on.
- Then let the Proxy steer the message to a Base method or a Cell method.
- Finally let the EntityDef exposed method definition complete the script call.

### Not Every Instance Connects to the Front End

One easily misread point has to be clarified here.

Many entity instances in BigWorld "support communication", but that does not
mean "every instance has its own front-end network connection":

- For the client, the real physical connection hangs off the `Proxy`'s
  `Channel`.
- Other entity instances reach the client through AOI and `Proxy` / Witness
  forwarding.
- Ordinary Base/Cell entity-to-entity communication relies on mailboxes, not
  client sockets.

For example, a script call to `otherEntity.base.someMethod()` goes down to
`BaseEntityMailBox::getStreamEx()` and is wrapped as
`BaseAppIntInterface::callBaseMethod`, see
[mailbox.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/baseapp/mailbox.cpp:1054).

Likewise the communication objects `self.base`, `self.cell`, `self.client` are in
essence different implementations of `PyEntityMailBox`, see
[mailbox_base.hpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/entitydef/mailbox_base.hpp:55).

### The Front End Gets a Connection Model, Not an ORM Object

On the client side too, it does not get a "reference to a server object". What
it gets is a connection model:

- `ServerConnection` handles network send and receive.
- `BWEntities` / `BWServerMessageHandler` dispatch entity messages to the local
  client entity representation.
- `ServerEntityMailBox` lets client code send method calls back to the server by
  entity id, see
  [server_entity_mail_box.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/connection_model/server_entity_mail_box.cpp:17).

This is closer to a "remote entity connection model" than to the REST/GraphQL
data fetching common in web front ends.

## Interface Registration

When a server process starts, it registers its interfaces with the
`NetworkInterface`.

Existing source evidence:

- `CellAppInterface::registerWithInterface()` at
  [cellapp.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/cellapp.cpp:539).
- `BaseAppMgrInterface::registerWithInterface()` at
  [baseappmgr.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/baseappmgr/baseappmgr.cpp:389).
- `DBAppInterface::registerWithInterface()` at
  [dbapp.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/dbapp/dbapp.cpp:231).

The interface macros generate the registration function and the
`InterfaceElement` references, see
[interface_macros.hpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/network/interface_macros.hpp:193).

The advantages of this macro system:

- Low runtime overhead.
- Concise C++ call sites.
- Message id, length and handler bindings sit next to the compile-time code.

The costs are:

- Weak IDE/static analysis experience.
- Interface changes are less obvious than in a standalone IDL.
- Poor cross-language ecosystem.
- Generation and registration logic hides behind macros, which is expensive to
  learn.

## The Relationship with EntityDef

Mercury RPC solves "how messages are sent"; EntityDef solves "how entity
properties and method arguments are encoded".

How the two relate:

- Interfaces define process-level messages, such as `createBaseWithCellData` and
  `setSharedData`.
- EntityDef defines entity properties, methods and data types.
- `MethodArgs` writes script method arguments into a `BinaryOStream`.
- `DataDescription` writes properties into the stream according to
  client/base/cell/persistent semantics.
- The Bundle carries these streams and hands them to the Channel for sending.

So BigWorld's protocol cannot be fully expressed by a single IDL file. It is a
protocol layer formed jointly by Mercury interfaces, the EntityDef type system
and Python script bindings.

## Similarities and Differences with Actors

The question "if every instance supports communication, what is the difference
from an Actor" is a good one, but it cannot be answered simply with "it is just
an Actor".

### Similarities

BigWorld entities and Actors do share several obvious similarities:

- Both emphasise state held per instance.
- Both interact mainly through messages/method calls rather than shared memory.
- Both have "addressable" instance references.
- Both naturally suit distribution across processes or machines.

In BigWorld this "addressable instance reference" is the mailbox:

- `PyEntityMailBox` represents a remote entity target, see
  [mailbox_base.hpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/entitydef/mailbox_base.hpp:55).
- When calling a script method, `PyEntityMailBox::callMethod()` validates the
  arguments, acquires a stream, encodes the arguments and sends, see
  [mailbox_base.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/entitydef/mailbox_base.cpp:125).

From a usage point of view, `entity.base.foo()`, `entity.cell.bar()` and
`entity.client.baz()` do indeed look a lot like sending a message to some Actor.

### Essential Differences

But BigWorld is not a general-purpose Actor runtime; there are at least four
key differences.

First, an entity is not a single-mailbox, single-threaded execution body.

- Actor models usually stress that an actor has exactly one mailbox and serial
  consumption semantics.
- A BigWorld logical entity splits into Base, Cell, Client and even Ghost views,
  each with different responsibilities.
- Client communication still has to go through `Proxy`; not every instance holds
  an external connection itself.

Second, messages are not the only first-class citizen - property synchronisation
is a first-class mechanism too.

- Actor systems usually stress explicit messages more.
- BigWorld has, besides method calls, dedicated paths for property replication,
  AOI enter/leave, volatile position, ghost updates and DB persistence.
- `PropertyChange` is a protocol system independent of method calls, see
  [property_change.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/lib/entitydef/property_change.cpp:139).

Third, the scheduling goal is not "a general concurrency model" but "game world
semantics".

- Actor runtimes care about isolation, scheduling, fairness and fault-tolerance
  supervision trees.
- BigWorld cares more about spatial partitioning, AOI, Witness, the Base/Cell
  split, migration, backup and DB writes.
- It has the character of a distributed entity runtime, but no general
  supervisor tree semantics like Erlang/Akka can be found.

Fourth, much message handling is still bound to the main-thread Reactor rather
than to an independent actor scheduler.

- `Mercury` handlers usually execute on the Reactor main thread.
- `Proxy::baseEntityMethod()` and `Entity::runMethodHelper()` ultimately call
  script logic directly, see
  [proxy.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/baseapp/proxy.cpp:2536)
  and
  [entity.cpp](/home/cui/workspaces/BigWorld/programming/bigworld/server/cellapp/entity.cpp:5412).

So the more accurate judgement is:

- BigWorld has clear actor-like messaging characteristics.
- But it is a distributed entity runtime tailored to MMO entity semantics, not a
  general Actor framework.

### A Closer Analogy

If an analogy must be drawn, BigWorld is closer to:

- "Actor + replicated entity + AOI + authoritative simulation + custom
  persistence"

rather than simply:

- "every entity is an Actor"

The difference matters. The former explains why it needs mailboxes, incremental
property sync, Proxy/Witness, the Base/Cell split and a dedicated DBApp all at
once; the latter explains none of these engine-level constraints.

## Trade-offs in the Source

Mercury interfaces and EntityDef together carry the protocol contract. The
trade-offs concentrate in these places:

- Process-level messages are registered by C++ interface macros into
  `InterfaceElement`, dispatching quickly by message id and length style.
- Entity-level methods and properties get their index, exposed id,
  client-server property id and digest assigned by EntityDef.
- `Bundle` / `BinaryOStream` let business code write the stream in argument
  order, avoiding construction of a general object model on the hot path.
- The UDP Channel preserves mixed reliable/unreliable delivery, piggybacking,
  ACKs and request/reply timeout semantics.
- The front-end client only connects to a `Proxy`; ordinary entities
  communicate indirectly through mailboxes, AOI, Witness and `ClientInterface`.

The costs come from the same source structure:

- The protocol is not self-describing, so packet captures and cross-language
  tools need the same interface table and EntityDef.
- Macro registration hides part of the message numbering and handler bindings, so
  the reading entry points are scattered.
- Handlers usually execute synchronously on the Reactor main thread; an RPC call
  is not an automatically concurrent task.
- Entity methods, property sync, AOI messages, the login handshake and DB
  persistence all share the BinaryStream system, so a single compatibility
  change has a wide blast radius.

## Correctness Boundaries in the Source

Correctness of the communication layer depends on several premises:

- Both sides agree on the interface table.
- Variable-length message length fields are trustworthy and validated.
- Handlers consume the stream completely and leave no error state behind.
- The request/reply timeout path must release the waiting state.
- Reactor handlers must not block.
- Messages from external clients have to account for flooding, oversized
  payloads, illegal message ids and forged channel metadata.

## What Source-Verified Tests Should Cover

Communication layer tests should cover message encoding, dispatch and defences
at external entry points:

- `InterfaceElement::headerSize()` should return the expected header size for
  fixed length, variable length and illegal messages.
- Callback length messages must obtain the real stream size through the
  handler.
- A request created by `Bundle::startRequest()` should release its waiting state
  after a reply or a timeout.
- `UDPBundleProcessor` should take the error path on an unknown message id, a
  wrong length, or a stream that is not fully consumed.
- `Proxy::baseEntityMethod()` and `Proxy::cellEntityMethod()` must only allow
  methods inside the EntityDef exposed range.
- `OWN_CLIENT` methods should validate the source entity and must not be
  forgeable by another client.
- When `ServerConnection` parses `entityMethod` / `entityProperty` it must use
  the same `ClientInterface::Range` and EntityDef.
- Front-end entity creation ordering should cover `createBasePlayer`, `enterAoI`,
  `createEntity`, `updateEntity` and `leaveAoI`.
- Mailbox calls should validate argument types, stream size and the target
  Base/Cell/Client path.

## Scope of This Chapter

This chapter explains Mercury's message and RPC abstractions. The next chapter
moves into EntityDef serialisation: how BigWorld connects Python objects, entity
properties, method arguments and the binary network stream.