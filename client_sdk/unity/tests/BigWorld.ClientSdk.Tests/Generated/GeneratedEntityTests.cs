#pragma warning disable CS0067 // events on the capturing transport are never raised

using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using BigWorld.ClientSdk.Connection;
using BigWorld.ClientSdk.Generated;
using BigWorld.ClientSdk.Protocol;
using BigWorld.ClientSdk.Streams;
using BigWorld.ClientSdk.Transport;
using Xunit;

namespace BigWorld.ClientSdk.Tests.Generated
{
    /// <summary>
    /// CL-3 runtime smoke for the generated entity bases (batch one): the
    /// generated Account / ClientAvatar classes compile against the CL-1
    /// runtime and drive the three accepted paths against the wire oracle:
    ///
    /// * initial-snapshot decode (createEntity payload -> ReadClientData),
    /// * server-to-client method dispatch (InvokeClientMethod -> OnXxx hook),
    /// * client-to-server exposed method call (SendXxx -> outgoing frame).
    ///
    /// The last one asserts the exact wire bytes: [entityId: int32 LE][exposed
    /// method id: uint8] as the server's EntityCall expects
    /// (kbengine Entity.onRemoteMethodCall writes the entity id then the
    /// exposed id).
    /// </summary>
    public sealed class GeneratedEntityTests
    {
        private sealed class CapturingTransport : ITransport
        {
            public readonly List<byte> Sent = new List<byte>();

            public event EventHandler? Connected;
            public event EventHandler<DataReceivedEventArgs>? DataReceived;
            public event EventHandler<TransportClosedEventArgs>? Closed;

            public bool IsConnected => true;

            public void Send(byte[] data, int offset, int count)
            {
                Sent.AddRange(data.Skip(offset).Take(count));
            }

            public Task ConnectAsync(string host, int port, CancellationToken cancellationToken = default)
                => Task.CompletedTask;

            public void Dispose()
            {
            }
        }

        private sealed class TestAccount : AccountBase
        {
            public string? LastMessage;
            public int UnknownId = -1;

            public TestAccount(int id) : base(id)
            {
            }

            public override void OnChatMessage(string value0) => LastMessage = value0;

            protected override void OnUnknownClientMethod(int methodId, BwReader reader)
                => UnknownId = methodId;
        }

        [Fact]
        public void ReadClientData_DecodesPlayerName()
        {
            var payload = new BwWriter(64);
            payload.WriteString("Alice");
            var account = new TestAccount(1000);

            account.ReadClientData(new BwReader(payload.ToArray()));

            Assert.Equal("Alice", account.PlayerName);
        }

        [Fact]
        public void InvokeClientMethod_DispatchesKnownAndUnknown()
        {
            var account = new TestAccount(1000);

            var msg = new BwWriter(64);
            msg.WriteString("hi");
            account.InvokeClientMethod(AccountBase.MethodIdChatMessage,
                new BwReader(msg.ToArray()));
            Assert.Equal("hi", account.LastMessage);

            account.InvokeClientMethod(99, new BwReader(Array.Empty<byte>()));
            Assert.Equal(99, account.UnknownId);
        }

        [Fact]
        public void SendStub_WritesEntityIdThenExposedId()
        {
            var transport = new CapturingTransport();
            using var connection = new ClientConnection(transport);
            var spec = new MessageSpec(0x41, "Entity_onRemoteMethodCall",
                MessageLengthStyle.Variable, lengthParam: 1);

            var call = new EntityCallClientAvatarBase(
                connection, entityId: 1000,
                cellMessage: spec, baseMessage: spec,
                EntityCallClientAvatarBase.CallKind.Base);
            call.SendLogOff();

            // The sender emits the client header frame ("TCP\r\n\r\n") before
            // the bundle frame. The bundle payload is
            // [flags][id][len][entityId: int32 LE][exposed id: uint8].
            byte[] sent = transport.Sent.ToArray();
            Assert.Equal((byte)7, sent[0]);   // header frame length prefix
            Assert.Equal((byte)'T', sent[2]);

            // [0:2] header len, [2:9] "TCP\r\n\r\n", [9:11] bundle len.
            int offset = 2 + 7 + 2;
            Assert.Equal((byte)0x00, sent[offset]);           // flags
            Assert.Equal((byte)0x41, sent[offset + 1]);       // message id
            // variable length field (1 byte): body is 5 bytes (int32 + uint8)
            Assert.Equal((byte)5, sent[offset + 2]);
            // entity id: little-endian int32 1000
            Assert.Equal((byte)0xE8, sent[offset + 3]);
            Assert.Equal((byte)0x03, sent[offset + 4]);
            Assert.Equal((byte)0x00, sent[offset + 5]);
            Assert.Equal((byte)0x00, sent[offset + 6]);
            // exposed method id for logOff (0)
            Assert.Equal((byte)0x00, sent[offset + 7]);
        }

        [Fact]
        public void GeneratedIds_MatchServerOrdering()
        {
            // ClientAvatar: fixed asc (prop3=1B, prop2=2B, prop1=4B) then
            // variable asc (playerName, prop5 - both header size 1, stable).
            Assert.Equal(0, ClientAvatarBase.PropertyIdProp3);
            Assert.Equal(1, ClientAvatarBase.PropertyIdProp2);
            Assert.Equal(2, ClientAvatarBase.PropertyIdProp1);
            Assert.Equal(3, ClientAvatarBase.PropertyIdPlayerName);
            Assert.Equal(4, ClientAvatarBase.PropertyIdProp5);

            // client method ids live in the joint exposed id space: the
            // server stable-sorts all exposed methods (base, cell, client)
            // together - logOff (fixed 0B)=0, cellTick (4B)=1, fromCell
            // (variable)=2.
            Assert.Equal(1, ClientAvatarBase.MethodIdCellTick);
            Assert.Equal(2, ClientAvatarBase.MethodIdFromCell);
        }
    }
}
