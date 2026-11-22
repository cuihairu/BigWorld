# BigWorld.ClientSdk (Unity / C#)

Engine-agnostic BigWorld Mercury TCP client protocol core
(`netstandard2.1`), with a `dotnet test` suite. There is **no
`UnityEngine` reference**: the transport is interface-driven, so the same DLL
embeds in Unity, a headless host or a mock server.

## Layout

```
unity/
  src/BigWorld.ClientSdk/          # the library (netstandard2.1)
    Streams/                       # BwWriter / BwReader (little-endian, packed ints, strings)
    Protocol/                      # MessageSpec, BundleWriter/Reader, TcpFraming,
                                   # PacketReceiver/Sender, ProtocolException
    Transport/                     # ITransport, TcpTransport
    Connection/                    # ConnectionState, LoginMessageIds, ClientConnection
  tests/BigWorld.ClientSdk.Tests/  # xunit suite (net10.0)
  PROTOCOL.md                      # source-mapped wire protocol table
```

## Quick start

```csharp
using BigWorld.ClientSdk.Connection;
using BigWorld.ClientSdk.Transport;

using var connection = new ClientConnection(new TcpTransport());

connection.StateChanged += (_, state) => Console.WriteLine($"state: {state}");
connection.EntityCreated += (_, e) =>
    Console.WriteLine($"entity {e.EntityId} (type {e.EntityTypeId}) created");

await connection.ConnectAsync("127.0.0.1", 40000, sessionKey: 0x11223344);
```

`ConnectAsync` runs the login handshake: TCP connect → client header frame
(`"TCP\r\n\r\n"`) → `authenticate { uint32 key }` → `onLoginCheckStatus`
→ `createEntity` → `Connected`.

## Build & test

```bash
dotnet test tests/BigWorld.ClientSdk.Tests/BigWorld.ClientSdk.Tests.csproj
```

Both `Debug` and `Release` are clean (`TreatWarningsAsErrors`), 72 tests.

## Wire facts

See `PROTOCOL.md`. Summary: little-endian wire order; frames are
`[uint16 LE length][payload]` with a `0xFFFF` large form; the client's first
send carries the `"TCP\r\n\r\n"` header frame; bundles are
`[flags][firstRequestOffset?][messages]`; message records are
`[id][length][replyId?][payload]` with a request-offset chain; length fields
are little-endian and oversized lengths use the all-ones + `uint32` form.

## Substituting the server→client message table

The login-flow message ids default to the kbengine shape (see
`PROTOCOL.md` §6). A host that needs a different server interface can pass a
custom `LoginMessageIds` to the `ClientConnection` constructor.
