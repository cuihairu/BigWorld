using System;
using System.Collections.Generic;
using System.Linq;
using BigWorld.ClientSdk.Connection;
using BigWorld.ClientSdk.Protocol;
using BigWorld.ClientSdk.Streams;
using Xunit;

namespace BigWorld.ClientSdk.Tests.Protocol
{
    /// <summary>
    /// Bundle encode/decode round-trips against the Mercury TCP bundle layout
    /// (tcp_bundle.cpp / tcp_bundle_processor.cpp). See PROTOCOL.md rows BND-*.
    /// </summary>
    public sealed class BundleCodecTests
    {
        private static readonly MessageSpec Authenticate =
            new MessageSpec(0x00, "authenticate", MessageLengthStyle.Fixed, 4);

        private static readonly MessageSpec Var2 =
            new MessageSpec(0x21, "onLoginCheckStatus", MessageLengthStyle.Variable, 2);

        private static readonly MessageSpec Var4 =
            new MessageSpec(0x42, "wide", MessageLengthStyle.Variable, 4);

        // Lookup table covering every spec exercised by these tests.
        private static readonly MessageSpecTable TestTable =
            MessageSpecTable.FromSpecs(new[] { Authenticate, Var2, Var4, MessageSpec.Reply });

        // BND-1: a fixed-length message has no length field.
        [Fact]
        public void FixedMessage_EncodesAsFlagsIdPayload()
        {
            byte[] bundle = BundleWriter.BuildSingle(
                Authenticate, w => w.WriteUInt32(0xDEADBEEFu));

            Assert.Equal(
                new byte[] { 0x00, 0x00, 0xEF, 0xBE, 0xAD, 0xDE },
                bundle);
        }

        [Fact]
        public void FixedMessage_RoundTrips()
        {
            byte[] bundle = BundleWriter.BuildSingle(Authenticate, w => w.WriteUInt32(0xCAFEBABEu));
            IReadOnlyList<MessageRecord> records = Parse(bundle);
            MessageRecord record = Assert.Single(records);

            Assert.Equal(0x00, record.MessageId);
            Assert.False(record.IsRequest);
            Assert.Equal(4, record.PayloadLength);
            Assert.Equal(0xCAFEBABEu, BundleReader.PayloadReader(record).ReadUInt32());
        }

        // BND-2: a variable-length message carries a 2-byte LE length field.
        [Fact]
        public void VariableMessage_EncodesLengthField()
        {
            // 4-byte payload.
            byte[] bundle = BundleWriter.BuildSingle(
                Var2, w => w.WriteInt32(0));

            // [flags 0][id 0x21][len 0x04 0x00][payload 00 00 00 00]
            Assert.Equal(
                new byte[] { 0x00, 0x21, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00 },
                bundle);
        }

        [Fact]
        public void VariableMessage_RoundTrips()
        {
            byte[] bundle = BundleWriter.BuildSingle(Var2, w => w.WriteString("hello"));
            MessageRecord record = Assert.Single(Parse(bundle));

            Assert.Equal(0x21, record.MessageId);
            Assert.Equal(6, record.PayloadLength); // packed len 1 + "hello" 5
            Assert.Equal("hello", BundleReader.PayloadReader(record).ReadString());
        }

        // BND-3: a request carries replyId + nextRequestOffset after the length field.
        [Fact]
        public void SingleRequest_SetsFlagsAndFirstRequestOffset()
        {
            byte[] bundle = BuildBundle(w =>
            {
                BwWriter payload = w.StartRequest(Var2, replyId: 7);
                payload.WriteInt32(99);
                w.EndMessage(4);
            });

            Assert.Equal(MessageSpec.BundleFlagHasRequests, bundle[0]); // flags
            // firstRequestOffset = 1 + 4 = 5.
            Assert.Equal(5u, ReadUInt32(bundle, 1));
            // Request record: [id 0x21][len field = 4][replyId 7][next 0xFFFFFFFF][payload].
            Assert.Equal(0x21, bundle[5]);
            Assert.Equal(4, ReadUInt16(bundle, 6));
            Assert.Equal(7, ReadInt32(bundle, 8));
            Assert.Equal(0xFFFFFFFFu, ReadUInt32(bundle, 12));
            Assert.Equal(99, ReadInt32(bundle, 16));
        }

        // BND-4: two requests chain via nextRequestOffset; non-request payloads
        // must not be clobbered (regression: the offset patch used to land in the
        // body of a non-empty request).
        [Fact]
        public void TwoRequests_ChainOffsets_AndPreservePayloads()
        {
            byte[] bundle = BuildBundle(w =>
            {
                BwWriter p1 = w.StartRequest(Var2, replyId: 1);
                p1.WriteInt32(0x11111111);
                w.EndMessage(4);

                BwWriter p2 = w.StartMessage(Var2);
                p2.WriteString("between");
                w.EndMessage(8); // packed len 1 + "between" 7

                BwWriter p3 = w.StartRequest(Var2, replyId: 2);
                p3.WriteInt32(0x22222222);
                w.EndMessage(4);
            });

            // flags
            Assert.Equal(MessageSpec.BundleFlagHasRequests, bundle[0]);
            // first request at offset 5.
            uint first = ReadUInt32(bundle, 1);
            Assert.Equal(5u, first);
            // first request's nextRequestOffset points at the second request.
            Assert.Equal(31u, ReadUInt32(bundle, 12));
            // second request's nextRequestOffset is the sentinel.
            Assert.Equal(0xFFFFFFFFu, ReadUInt32(bundle, 38));

            // Round trip: both records are requests, payloads intact.
            IReadOnlyList<MessageRecord> records = Parse(bundle);
            Assert.Equal(3, records.Count);

            Assert.True(records[0].IsRequest);
            Assert.Equal(1, records[0].ReplyId);
            Assert.Equal(0x11111111, BundleReader.PayloadReader(records[0]).ReadInt32());

            Assert.False(records[1].IsRequest);
            Assert.Equal("between", BundleReader.PayloadReader(records[1]).ReadString());

            Assert.True(records[2].IsRequest);
            Assert.Equal(2, records[2].ReplyId);
            Assert.Equal(0x22222222, BundleReader.PayloadReader(records[2]).ReadInt32());
        }

        // BND-5: oversize variable messages use all-0xFF length + uint32.
        [Fact]
        public void OversizeVariableMessage_UsesAllOnesPlusUint32()
        {
            // Var2 max inline length is 0xFFFF - 1 = 65534; 70000 must go oversize.
            byte[] payload = new byte[70000];
            payload[0] = 0x5A;
            payload[69999] = 0xE5;

            byte[] bundle = BuildBundle(w =>
            {
                BwWriter body = w.StartMessage(Var2);
                body.WriteBlob(payload);
                w.EndMessage(payload.Length);
            });

            // [flags 0][id 0x21][FF FF][len uint32 70000][payload...]
            Assert.Equal(0x21, bundle[1]);
            Assert.Equal(0xFF, bundle[2]);
            Assert.Equal(0xFF, bundle[3]);
            Assert.Equal(70000u, ReadUInt32(bundle, 4));

            MessageRecord record = Assert.Single(Parse(bundle));
            Assert.Equal(70000, record.PayloadLength);
            byte[] readBack = BundleReader.PayloadReader(record).ReadBytesRaw(70000);
            Assert.Equal(0x5A, readBack[0]);
            Assert.Equal(0xE5, readBack[69999]);
        }

        [Fact]
        public void Oversize_StillFitsInWidth4WithoutSpecialForm()
        {
            // Width-4 length fields are always inline (no uint32 shift):
            // [flags][id][len uint32][payload].
            byte[] bundle = BundleWriter.BuildSingle(Var4, w => w.WriteUInt32(0x11223344));
            Assert.Equal(0x42, bundle[1]);
            Assert.Equal(new byte[] { 0x04, 0x00, 0x00, 0x00 }, bundle[2..6]); // length field
            Assert.Equal(new byte[] { 0x44, 0x33, 0x22, 0x11 }, bundle[6..10]); // payload
        }

        // BND-6: error paths mirror REASON_CORRUPTED_PACKET / NONEXISTENT_ENTRY.
        [Fact]
        public void Parse_RejectsUnknownFlags()
        {
            byte[] bundle = { 0x02 };
            Assert.Throws<ProtocolException>(() => Parse(bundle));
        }

        [Fact]
        public void Parse_RejectsUnknownMessageId()
        {
            byte[] bundle = { 0x00, 0x40, 0x00, 0x00 }; // id 0x40 not in table
            Assert.Throws<ProtocolException>(() => Parse(bundle));
        }

        [Fact]
        public void Parse_RejectsTruncatedPayload()
        {
            // id 0x21 (Var2) with length 4 but only 2 payload bytes.
            byte[] bundle = { 0x00, 0x21, 0x04, 0x00, 0x01, 0x02 };
            Assert.Throws<ProtocolException>(() => Parse(bundle));
        }

        [Fact]
        public void Parse_RejectsOutOfBoundsFirstRequestOffset()
        {
            byte[] bundle =
            {
                0x01,       // flags: has requests
                0xFF, 0xFF, 0xFF, 0x00, // firstRequestOffset far past end
                0x21, 0x04, 0x00, 0x01, 0x02, 0x03, 0x04
            };
            Assert.Throws<ProtocolException>(() => Parse(bundle));
        }

        // BND-7: REPLY (id 0xFF, variable, 4) is a valid inbound spec.
        [Fact]
        public void ReplySpec_IsRecognised()
        {
            Assert.True(MessageSpec.Reply.IsReply);
            Assert.Equal(MessageLengthStyle.Variable, MessageSpec.Reply.Style);
            Assert.Equal(4, MessageSpec.Reply.LengthParam);

            byte[] bundle = BundleWriter.BuildSingle(MessageSpec.Reply, w => w.WriteInt32(42));
            Assert.Equal(0xFF, bundle[1]);

            // The full server table knows REPLY.
            IReadOnlyList<MessageRecord> records =
                BundleReader.Parse(bundle, 0, bundle.Length, LoginMessageIds.Default.ServerToClientTable());
            Assert.Equal(0xFF, Assert.Single(records).MessageId);
        }

        // BND-8: MessageSpec.CanHandleLength / MaxPayloadLength semantics
        // (interface_element.hpp:127-135).
        [Fact]
        public void CanHandleLength_MatchesInterfaceElement()
        {
            var width2 = new MessageSpec(0x10, "v2", MessageLengthStyle.Variable, 2);
            Assert.Equal(0xFFFF, width2.MaxPayloadLength);
            Assert.True(width2.CanHandleLength(0xFFFE));
            Assert.False(width2.CanHandleLength(0xFFFF));

            var width4 = new MessageSpec(0x11, "v4", MessageLengthStyle.Variable, 4);
            Assert.Equal(int.MaxValue, width4.MaxPayloadLength);
            Assert.True(width4.CanHandleLength(1_000_000));

            Assert.Equal(int.MaxValue, Authenticate.MaxPayloadLength);
            Assert.True(Authenticate.CanHandleLength(4));
        }

        [Fact]
        public void MessageSpec_ValidatesLengthParam()
        {
            Assert.Throws<ArgumentOutOfRangeException>(
                () => new MessageSpec(1, "x", MessageLengthStyle.Variable, 5));
            Assert.Throws<ArgumentOutOfRangeException>(
                () => new MessageSpec(1, "x", MessageLengthStyle.Variable, -1));
            Assert.Throws<ArgumentOutOfRangeException>(
                () => new MessageSpec(1, "x", MessageLengthStyle.Fixed, -1));
        }

        [Fact]
        public void MessageSpecTable_RejectsDuplicateIds()
        {
            Assert.Throws<ArgumentException>(() =>
                MessageSpecTable.FromSpecs(new[]
                {
                    new MessageSpec(1, "a", MessageLengthStyle.Fixed, 0),
                    new MessageSpec(1, "b", MessageLengthStyle.Fixed, 0)
                }));
        }

        [Fact]
        public void MessageSpecTable_ResolvesById()
        {
            MessageSpecTable table = MessageSpecTable.FromSpecs(new[] { Authenticate, Var2 });
            Assert.True(table.TryGet(0x00, out MessageSpec? auth));
            Assert.Same(Authenticate, auth);
            Assert.True(table.TryGet(0x21, out MessageSpec? var2));
            Assert.Same(Var2, var2);
            Assert.False(table.TryGet(0x40, out _));
        }

        private static byte[] BuildBundle(Action<BundleWriter> build)
        {
            var writer = new BundleWriter();
            build(writer);
            return writer.Build();
        }

        private static IReadOnlyList<MessageRecord> Parse(byte[] bundle) =>
            BundleReader.Parse(bundle, 0, bundle.Length, TestTable);

        private static ushort ReadUInt16(byte[] b, int at) =>
            (ushort)(b[at] | (b[at + 1] << 8));

        private static uint ReadUInt32(byte[] b, int at) =>
            (uint)(b[at] | (b[at + 1] << 8) | (b[at + 2] << 16) | (b[at + 3] << 24));

        private static int ReadInt32(byte[] b, int at) => (int)ReadUInt32(b, at);
    }
}
