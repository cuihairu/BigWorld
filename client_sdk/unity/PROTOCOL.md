# BigWorld Mercury TCP wire protocol — source-mapped table

CL-1 acceptance artifact. Every wire fact implemented in
`BigWorld.ClientSdk` is mapped to the in-repo authority
(`programming/bigworld/lib/network`, read-only), the C# implementation, and
the test that locks it in.

Repository-relative source paths below are under `programming/bigworld/lib/`.

## Authority and byte order

The authoritative source for the wire format is `lib/network` (Mercury). The
in-repo source is **little-endian** on the wire:

> `cstdmf/binary_stream.ipp:8` — "BigWorld network byte order is
> little-endian, not big-endian".

`BW_HTON*` / `BW_PACK3` / `BW_NTOH*` / `BW_UNPACK3` are therefore identity on
little-endian hosts, and every multi-byte field below is stored
least-significant byte first.

## 1. Streams (STM)

| # | Source authority | Wire fact | C# implementation | Test |
|---|---|---|---|---|
| STM-1 | `cstdmf/binary_stream.ipp:8` | All scalars little-endian | `BwWriter.WriteUInt16/32/64`, `BwReader.ReadUInt16/32/64` | `BwStreamTests.UInt16/32/64_IsLittleEndian`, `Int32_MinusOne_IsAllOnes` |
| STM-2 | `cstdmf/binary_stream.hpp` `operator<<(float/double)` | IEEE-754 in wire byte order | `BwWriter.WriteFloat/WriteDouble`, `BwReader.ReadFloat/ReadDouble` | `BwStreamTests.Float_IsLittleEndianIeee754`, `Float_RoundTrips`, `Double_RoundTrips` |
| STM-3 | `cstdmf/binary_stream.hpp:257-270` `writePackedInt` | value `< 255` → 1 byte; else `0xFF` + 3 LE bytes; max `2^24` | `BwWriter.WritePackedInt` | `BwStreamTests.PackedInt_Encoding`, `PackedInt_RoundTrips`, `PackedInt_RejectsTwoToThe24` |
| STM-4 | `cstdmf/binary_stream.hpp:287-300` `readPackedInt` | `0xFF` prefix → `BW_UNPACK3` | `BwReader.ReadPackedInt` | `BwStreamTests.PackedInt_RoundTrips` |
| STM-5 | `cstdmf/binary_stream.hpp:382-392` `appendString` | packed-int length + UTF-8 bytes | `BwWriter.WriteString`, `BwReader.ReadString` | `BwStreamTests.String_Encoding_IsPackedLengthPlusUtf8`, `String_MultibyteUtf8_UsesByteLength`, `String_Long_UsesThreeBytePackedLength` |
| STM-6 | `cstdmf/binary_stream.hpp` `operator<<(Vector3)` | 3 little-endian floats x,y,z (12 bytes) | `BwWriter.WriteVector3`, `BwReader` component reads | `BwStreamTests.Vector3_IsTwelveBytes`, `Vector3_RoundTrips` |
| STM-7 | `BwWriter.PatchUInt32` contract | in-place 32-bit LE patch (request chain) | `BwWriter.PatchUInt32` | `BwStreamTests.PatchUInt32_OverwritesInPlace`, `PatchUInt32_OutOfRange_Throws` |
| STM-8 | `BwReader` bounds | underrun → `EndOfStreamException` (truncated frame) | `BwReader.Require` | `BwStreamTests.Reader_Underrun_ThrowsEndOfStream`, `Reader_TracksPositionAndRemaining`, `Reader_RejectsOutOfRangeWindow` |

## 2. TCP framing (FRM)

| # | Source authority | Wire fact | C# implementation | Test |
|---|---|---|---|---|
| FRM-1 | `network/tcp_channel.cpp:414` `doSend` | frame = `[uint16 LE length][payload]` | `TcpFraming.EncodeFramePrefix` / `TryDecodeFramePrefix` | `FramingTests.SmallFrame_PrefixIsUInt16Le`, `SmallFrame_Decodes` |
| FRM-2 | `network/tcp_channel.cpp:427-429` | `length == 0xFFFF` → large form `[uint16 0xFFFF][uint32 LE length]` | `TcpFraming.EncodeFramePrefix` | `FramingTests.LargeFrame_PrefixIsMarkerPlusUInt32`, `LargeFrame_Decodes`, `PrefixBytes_MatchesEncoding` |
| FRM-3 | `network/tcp_channel.cpp:29-30` | client first-send header payload `"TCP\r\n\r\n"` (7 bytes) | `TcpFraming.ClientHeader`, `ClientHeaderFrameLength` | `FramingTests.ClientHeader_IsTcpCrLfCrLf` |
| FRM-4 | `network/tcp_channel.cpp:418-426` | client prepends header frame `[uint16 7]["TCP\r\n\r\n"]` before the first bundle | `PacketSender.SendBundle` (first call only) | `LoginHandshakeTests.ReadHeaderAndAuthenticate` (wire oracle) |
| FRM-5 | `network/tcp_channel.cpp:149-313` `handleInput`, `:321` `readSmallFrameLength`, `:345` `readLargeFrameLength`, `:384` `cleanUpAfterFrameReceive` | sticky packets (many frames per read) and partial packets (frame split across reads) reassembled incrementally | `PacketReceiver.Append` / `TryTakeFrame` | `FramingTests.Receiver_SplitsStickyFrames`, `Receiver_ReassemblesPartialFrames`, `Receiver_HandlesLargeFrame`, `Receiver_CompactsAfterManyFrames` |
| FRM-6 | sanity cap (client-side guard) | oversized single frame rejected rather than buffered | `PacketReceiver(maxFrameLength)` | `FramingTests.Receiver_RejectsOverCapFrame` |

## 3. Bundle layout (BND)

Bundle payload: `[flags:uint8] [firstRequestOffset:uint32?] <message records...>`.

| # | Source authority | Wire fact | C# implementation | Test |
|---|---|---|---|---|
| BND-1 | `network/tcp_bundle.hpp:29-30` | `FLAG_HAS_REQUESTS = 0x01`, `FLAG_MASK = 0x01` | `MessageSpec.BundleFlagHasRequests`, `BundleFlagMask` | `BundleCodecTests.SingleRequest_SetsFlagsAndFirstRequestOffset`, `Parse_RejectsUnknownFlags` |
| BND-2 | `network/tcp_bundle_processor.cpp:72-86` | `firstRequestOffset` present iff flag set; out-of-bounds → `REASON_CORRUPTED_PACKET` (cast to `int` first, so `0xFFFFFFFF` sentinel passes) | `BundleWriter.Build`, `BundleReader.Parse` | `SingleRequest_SetsFlagsAndFirstRequestOffset`, `Parse_RejectsOutOfBoundsFirstRequestOffset` |
| BND-3 | `network/tcp_bundle_processor.cpp:88-173` | message record `[id:uint8][length field][payload]`; requests add `[replyId:int32][nextRequestOffset:uint32]` before payload | `BundleReader.Parse`, `MessageRecord` | `TwoRequests_ChainOffsets_AndPreservePayloads`, `FixedMessage_RoundTrips`, `VariableMessage_RoundTrips` |
| BND-4 | `network/tcp_bundle_processor.cpp:154-169` | request chain walk; `nextRequestOffset` of last request = `0xFFFFFFFF` (`network/tcp_bundle.cpp:93` `Offset(-1)`) | `BundleWriter` chain patching, `BundleReader` walk | `TwoRequests_ChainOffsets_AndPreservePayloads` (sentinel assertions) |
| BND-5 | `network/tcp_bundle_processor.cpp:98-108` | unhandled message id → `REASON_NONEXISTENT_ENTRY` (bundle dropped) | `BundleReader.Parse` throws `ProtocolException` | `Parse_RejectsUnknownMessageId` |
| BND-6 | `network/tcp_bundle_processor.cpp:143-152` | truncated payload → `REASON_CORRUPTED_PACKET` | `BundleReader.Parse` throws `ProtocolException` | `Parse_RejectsTruncatedPayload` |
| BND-7 | `network/interface_element.cpp:24-26` | `REPLY`: id `0xFF`, `VARIABLE_LENGTH_MESSAGE`, lengthParam `4` | `MessageSpec.Reply` | `ReplySpec_IsRecognised` |

## 4. Message length fields (LEN)

| # | Source authority | Wire fact | C# implementation | Test |
|---|---|---|---|---|
| LEN-1 | `network/interface_element.cpp:530-556` `expandLength` | length field is a little-endian scalar of width `lengthParam` (1/2/3 bytes LE, or 4 bytes LE) | `BundleReader.Parse` width switch | `VariableMessage_EncodesLengthField`, `VariableMessage_RoundTrips`, `FixedMessage_RoundTrips` |
| LEN-2 | `network/interface_element.cpp:565-583`, `network/tcp_bundle_processor.cpp:130-141` | if the inline length overflows (negative after `(int)` cast for width 4) or cannot be represented (`!canHandleLength`, all-ones for widths 1-3), a `uint32 LE` length follows the field | `BundleReader.Parse` special form | `OversizeVariableMessage_UsesAllOnesPlusUint32` |
| LEN-3 | `network/interface_element.cpp:424-431` `unexpandLength` (writer side) | special form used only when `lengthParam==1&&len>=0xFF`, `==2&&len>=0xFFFF`, `==3&&len>=0xFFFFFF`; width 4 is always inline | `BundleWriter.Build` oversize decision | `OversizeVariableMessage_UsesAllOnesPlusUint32`, `Oversize_StillFitsInWidth4WithoutSpecialForm` |
| LEN-4 | `network/interface_element.hpp:127-135` `canHandleLength` | `length < (1 << (8*lengthHeaderSize)) - 1`; width 4 always true | `MessageSpec.CanHandleLength`, `MaxPayloadLength` | `CanHandleLength_MatchesInterfaceElement` |
| LEN-5 | `network/misc.hpp:90` | `MessageID` is `uint8`; `ReplyID` is `int32` | `MessageSpec.Id` (`byte`), `MessageRecord.ReplyId` | `BundleCodecTests` request tests |

## 5. Message id assignment (ID)

| # | Source authority | Wire fact | C# implementation | Test |
|---|---|---|---|---|
| ID-1 | `network/interface_minder.cpp:38-49` `add` | ids assigned sequentially by declaration order (nth declared → id n); `addRange` reserves blocks | `MessageSpecTable.IdByDeclarationOrder` | `MessageSpecTable_ResolvesById`, `MessageSpecTable_RejectsDuplicateIds` |
| ID-2 | `connection/client_interface.hpp:62` | `authenticate` is the first declared client→baseapp message → id `0x00`, fixed 4-byte key | `LoginMessageIds.Authenticate` | `LoginHandshakeTests.AssertClientAuthenticate` (byte oracle) |
| ID-3 | `connection/client_interface.hpp:169-173` | `loggedOff` carries a `uint8` reason | `LoginMessageIds.LoggedOff` | `LoginHandshakeTests.Login_ThenLoggedOff_Terminates` |

## 6. Login / entity creation state machine (LGN)

| # | Source authority | Wire fact | C# implementation | Test |
|---|---|---|---|---|
| LGN-1 | `connection/client_interface.hpp:62-66` | client → server: `authenticate { uint32 key }` | `ClientConnection.ConnectAsync` | `LoginHandshakeTests.Login_Succeeds_AndCreatesEntity`, `AssertClientAuthenticate` |
| LGN-2 | kbengine login flow (see note below) | server → client: `onLoginCheckStatus { int32 status }`, non-zero fails | `ClientConnection` `OnLoginCheckStatus` dispatch | `LoginHandshakeTests.Login_NonZeroStatus_LogsOff` |
| LGN-3 | kbengine `onCreatedProxies` / `onEntityEnterWorld` shape | server → client: `createEntity { int32 entityId, uint16 entityTypeId, ...data }` | `ClientConnection` `CreateEntity` dispatch | `LoginHandshakeTests.Login_Succeeds_AndCreatesEntity` |
| LGN-4 | `connection/client_interface.hpp:169-173` | server → client: `loggedOff { uint8 reason }` | `ClientConnection` `LoggedOff` dispatch | `LoginHandshakeTests.Login_ThenLoggedOff_Terminates` |
| LGN-5 | `ClientConnection` design | state transitions observable at event time (state set before event raised) | `ClientConnection.DispatchRecord` | `LoginHandshakeTests` (state assertions) |

### Login flow note (discrepancy, awaiting user decision)

The **client→server** ids (`authenticate = 0x00`, `loggedOff`) come directly
from BigWorld's `connection/client_interface.hpp` and are stable.

The **server→client** login messages (`onLoginCheckStatus`, `createEntity`)
do **not** exist in BigWorld's `BaseAppInterface` today — BigWorld's real
handshake is `onBaseAppLogin` / `onBaseAppLoginFailed` then a separate
baseapp connection and `onCreatePlayer`. The SDK targets the **kbengine**
login shape (`importClientMessages` → `hello` → `Loginapp_login` →
`onLoginSuccessfully` → baseapp connect → `Baseapp_loginBaseapp` →
`onCreatedProxies` → `onEntityEnterWorld`) because the task book aligns the
plugin capabilities with kbengine's client plugins.

Consequently the server→client ids in `LoginMessageIds` are **defaults
(keyed to the kbengine flow) and are overridable** via the `LoginMessageIds`
constructor argument. They are recorded here as a deliberate, configurable
divergence rather than a claimed BigWorld identity.

## 7. Protocol errors

| # | Source authority | Wire fact | C# implementation | Test |
|---|---|---|---|---|
| ERR-1 | `network/tcp_bundle_processor.cpp:64-70,98-108,143-152` | invalid flags / unknown id / truncated payload → bundle dropped with `REASON_CORRUPTED_PACKET` / `REASON_NONEXISTENT_ENTRY` | `ProtocolException` | `Parse_RejectsUnknownFlags`, `Parse_RejectsUnknownMessageId`, `Parse_RejectsTruncatedPayload` |
| ERR-2 | `network/interface_element.cpp:24-26` | replies are not part of the login flow | `ClientConnection` routes `0xFF` to `UnhandledMessage` | `LoginHandshakeTests.Login_ReplySurfacesAsUnhandled` |

## Implementation map

| Namespace | Files |
|---|---|
| `BigWorld.ClientSdk.Streams` | `BwWriter`, `BwReader` (STM) |
| `BigWorld.ClientSdk.Protocol` | `MessageSpec`, `MessageSpecTable`, `BundleWriter`, `BundleReader`, `TcpFraming`, `PacketReceiver`, `PacketSender`, `ProtocolException` (BND/LEN/ID/FRM) |
| `BigWorld.ClientSdk.Transport` | `ITransport`, `TcpTransport` |
| `BigWorld.ClientSdk.Connection` | `ConnectionState`, `LoginMessageIds`, `ClientConnection` (LGN) |
| Tests | `Streams/BwStreamTests`, `Protocol/BundleCodecTests`, `Protocol/FramingTests`, `Connection/LoginHandshakeTests` |
