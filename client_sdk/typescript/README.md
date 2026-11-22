# @bigworld/client-sdk (TypeScript)

Engine-agnostic BigWorld Mercury TCP client plugin core, published as an npm
package. The wire semantics are identical to the Unity/C# core
(`client_sdk/unity`), so a host can share one protocol understanding across
C# and TS — see `client_sdk/unity/PROTOCOL.md`, the source-mapped reference
(every row points at a `programming/bigworld/lib/network` source line).

## Layout

```
typescript/
  src/
    bwStream.ts        # BwWriter / BwReader (little-endian, packed ints, strings)
    bwReader.ts
    messageSpec.ts     # MessageSpec, MessageSpecTable, bundle flag constants
    bundle.ts          # BundleWriter + parseBundle (requests, oversize lengths)
    framing.ts         # frame prefix codec, PacketReceiver, PacketSender
    event.ts           # EventBus / Event (kbengine-aligned)
    entity.ts          # Entity, EntityCall, Vector3, EventTypes
    transport.ts       # ITransport, TcpTransport (node:net)
    connection.ts      # ConnectionState, LoginMessageIds, ClientConnection
    index.ts           # public exports
  test/                # vitest suite (codec + framing + event + login handshake)
```

## Install & test

```bash
pnpm install
pnpm build      # tsc → dist/
pnpm test       # vitest run (81 tests)
```

## Integration example

```ts
import { ClientConnection, ConnectionState, TcpTransport } from "@bigworld/client-sdk";

const connection = new ClientConnection(new TcpTransport());
connection.onStateChanged = (state) => console.log("state:", ConnectionState[state]);
connection.onEntityCreated = (e) =>
  console.log(`entity ${e.entityId} (type ${e.entityTypeId}) created`);

await connection.connect("127.0.0.1", 40000, 0x11223344);
```

`connect` runs the login handshake: TCP connect → client header frame
(`"TCP\r\n\r\n"`) → `authenticate { uint32 key }` → `onLoginCheckStatus` →
`createEntity` → `Connected`. On a non-zero status the connection moves to
`LoggedOff`.

## Wire facts (summary)

Byte order is **little-endian** (`binary_stream.ipp:8`). A frame is
`[uint16 LE len][payload]`, with `len == 0xFFFF` selecting the large form
`[uint16 0xFFFF][uint32 LE len][payload]`. The first client send carries the
header frame `[uint16 7]["TCP\r\n\r\n"]` (`tcp_channel.cpp:29`). A bundle
payload is `[flags u8][firstRequestOffset u32?][<records>]`; a record is
`[id u8][length field][replyId i32?][nextRequestOffset u32?][payload]`.
Message ids are assigned by interface declaration order
(`interface_minder.cpp:38-49`).

## Events

`Event` mirrors the `KBEngine.Event` singleton from kbengine's JS plugin
(`kbe/res/sdk_templates/client/js/kbengine.js`):

```ts
import { Event } from "@bigworld/client-sdk";

Event.register(EventTypes.onEnterWorld, this, "onEnterWorld"); // method name, owner as `this`
Event.fire(EventTypes.onEnterWorld, entity);
Event.deregister(EventTypes.onEnterWorld, this);
Event.deregisterAll(this);
```

`register` takes the owner and the *name* of a method on it; `fire` calls each
handler with the owner as `this` and the extra arguments. `pause()`/`resume()`
queue and replay fired events. Deviation from kbengine (deliberate, fail-fast):
kbengine logs an error and returns when the named method is missing; here
`register` throws.

## Entities

`Entity` (kbengine `KBEngine.Entity`) carries `id`, `className`, `position`,
`direction`, `velocity`, `inWorld`, `inited`, `isControlled`, plus
`__init__`/`onDestroy`/`onControlled`/`isPlayer` overridables.
`EntityCall` (kbengine `KBEngine.EntityCall`) builds an entity RPC: the host
supplies the outgoing message specs (`cellMessage`/`baseMessage`) in the
constructor, matching kbengine's `Baseapp_onRemoteCallCellMethodFromClient` /
`Entity_onRemoteMethodCall` — the generated client interface (CL-3) wires
these in.

```ts
import { EntityCall, EntityCallType } from "@bigworld/client-sdk";

const call = new EntityCall({ cellMessage: remoteCall, baseMessage: remoteMethod, send: (b) => connection.sendMessage(...) });
call.id = 1000;
call.type = EntityCallType.Cell;
const handle = call.newCall();
handle.writer.writeUInt32(methodId);
call.sendCall(handle);
```

## Scope notes

- **Tcp-over-WebSocket gateway is deferred** (out of scope for CL-2): the
  transport is interface-driven, so a WS transport can be added later without
  touching the protocol core.
- **Message ids** for `onLoginCheckStatus`/`createEntity` follow the kbengine
  login shape and are overridable via `LoginMessageIds` (see the PROTOCOL.md
  LGN rows and `client_sdk/README.md`).
- This package targets Node ≥ 18 (uses `node:net` for `TcpTransport`); a
  browser build would supply its own `ITransport`.
