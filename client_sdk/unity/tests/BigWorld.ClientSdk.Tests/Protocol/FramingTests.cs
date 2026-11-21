using System;
using System.Text;
using BigWorld.ClientSdk.Protocol;
using Xunit;

namespace BigWorld.ClientSdk.Tests.Protocol
{
    /// <summary>
    /// TCP frame prefix + incremental reassembler tests. Wire facts against
    /// tcp_channel.cpp doSend / readSmallFrameLength / readLargeFrameLength
    /// (PROTOCOL.md rows FRM-*).
    /// </summary>
    public sealed class FramingTests
    {
        // FRM-1: small form is a 2-byte LE length prefix.
        [Fact]
        public void SmallFrame_PrefixIsUInt16Le()
        {
            Assert.Equal(new byte[] { 0x05, 0x00 }, TcpFraming.EncodeFramePrefix(5));
        }

        // FRM-2: 0xFFFF triggers the large (uint32) form.
        [Fact]
        public void LargeFrame_PrefixIsMarkerPlusUInt32()
        {
            Assert.Equal(
                new byte[] { 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00 },
                TcpFraming.EncodeFramePrefix(0xFFFF));
            // 70000 = 0x11170, uint32 LE = 70 11 01 00.
            Assert.Equal(
                new byte[] { 0xFF, 0xFF, 0x70, 0x11, 0x01, 0x00 },
                TcpFraming.EncodeFramePrefix(70000));
        }

        [Fact]
        public void SmallFrame_Decodes()
        {
            Assert.True(TcpFraming.TryDecodeFramePrefix(
                new byte[] { 0x05, 0x00 }, 0, 2, out int len, out int consumed));
            Assert.Equal(5, len);
            Assert.Equal(2, consumed);
        }

        [Fact]
        public void LargeFrame_Decodes()
        {
            byte[] data = { 0xFF, 0xFF, 0x10, 0x00, 0x00, 0x00, 0xAA };
            Assert.True(TcpFraming.TryDecodeFramePrefix(data, 0, data.Length, out int len, out int consumed));
            Assert.Equal(0x10, len);
            Assert.Equal(6, consumed);
        }

        [Fact]
        public void Decode_RequiresPrefixBytes()
        {
            Assert.False(TcpFraming.TryDecodeFramePrefix(new byte[] { 0x05 }, 0, 1, out _, out _));
            // Marker present but uint32 incomplete.
            Assert.False(TcpFraming.TryDecodeFramePrefix(
                new byte[] { 0xFF, 0xFF, 0x10 }, 0, 3, out _, out _));
        }

        [Fact]
        public void PrefixBytes_MatchesEncoding()
        {
            Assert.Equal(2, TcpFraming.FramePrefixBytes(5));
            Assert.Equal(6, TcpFraming.FramePrefixBytes(0xFFFF));
        }

        // FRM-3: client header is a 7-byte "TCP\r\n\r\n" payload.
        [Fact]
        public void ClientHeader_IsTcpCrLfCrLf()
        {
            Assert.Equal("TCP\r\n\r\n", Encoding.ASCII.GetString(TcpFraming.ClientHeader));
            Assert.Equal(9, TcpFraming.ClientHeaderFrameLength);
        }

        // FRM-4: reassembly - several frames delivered in one TCP read.
        [Fact]
        public void Receiver_SplitsStickyFrames()
        {
            var receiver = new PacketReceiver();
            byte[] frame1 = BuildFrame(new byte[] { 0xAA, 0xBB });
            byte[] frame2 = BuildFrame(new byte[] { 0xCC });

            receiver.Append(Concat(frame1, frame2), 0, frame1.Length + frame2.Length);

            Assert.True(receiver.TryTakeFrame(out byte[]? got1));
            Assert.Equal(new byte[] { 0xAA, 0xBB }, got1);
            Assert.True(receiver.TryTakeFrame(out byte[]? got2));
            Assert.Equal(new byte[] { 0xCC }, got2);
            Assert.False(receiver.TryTakeFrame(out _));
            Assert.Equal(0, receiver.PendingBytes);
        }

        // FRM-5: reassembly - one frame split across several TCP reads.
        [Fact]
        public void Receiver_ReassemblesPartialFrames()
        {
            var receiver = new PacketReceiver();
            byte[] frame = BuildFrame(new byte[] { 0x01, 0x02, 0x03, 0x04, 0x05 });

            // Feed byte-at-a-time; the frame must only appear once complete.
            for (int i = 0; i < frame.Length - 1; i++)
            {
                receiver.Append(frame, i, 1);
                Assert.False(receiver.TryTakeFrame(out _));
            }

            // The final byte completes the frame.
            receiver.Append(frame, frame.Length - 1, 1);
            Assert.True(receiver.TryTakeFrame(out byte[]? got));
            Assert.Equal(new byte[] { 0x01, 0x02, 0x03, 0x04, 0x05 }, got);
        }

        // FRM-6: large frames flow through the reassembler unchanged.
        [Fact]
        public void Receiver_HandlesLargeFrame()
        {
            var receiver = new PacketReceiver();
            byte[] payload = new byte[70000];
            payload[0] = 0x77;
            byte[] frame = BuildFrame(payload);

            receiver.Append(frame, 0, frame.Length);
            Assert.True(receiver.TryTakeFrame(out byte[]? got));
            Assert.Equal(payload.Length, got!.Length);
            Assert.Equal(0x77, got[0]);
        }

        // FRM-7: an oversized single frame is rejected rather than buffered forever.
        [Fact]
        public void Receiver_RejectsOverCapFrame()
        {
            var receiver = new PacketReceiver(maxFrameLength: 16);
            byte[] frame = BuildFrame(new byte[32]);
            receiver.Append(frame, 0, frame.Length);
            // The cap is enforced when the prefix is decoded.
            Assert.Throws<InvalidOperationException>(() => receiver.TryTakeFrame(out _));
        }

        // FRM-8: consumed space is reclaimed so long streams don't grow the buffer.
        [Fact]
        public void Receiver_CompactsAfterManyFrames()
        {
            var receiver = new PacketReceiver();
            // Enough frames to cross the 4096-byte compaction threshold.
            for (int i = 0; i < 2000; i++)
            {
                byte[] frame = BuildFrame(new byte[] { (byte)i });
                receiver.Append(frame, 0, frame.Length);
                Assert.True(receiver.TryTakeFrame(out byte[]? got));
                Assert.Equal((byte)i, got![0]);
            }

            Assert.Equal(0, receiver.PendingBytes);
        }

        private static byte[] BuildFrame(byte[] payload)
        {
            byte[] prefix = TcpFraming.EncodeFramePrefix(payload.Length);
            byte[] frame = new byte[prefix.Length + payload.Length];
            Array.Copy(prefix, frame, prefix.Length);
            Array.Copy(payload, 0, frame, prefix.Length, payload.Length);
            return frame;
        }

        private static byte[] Concat(byte[] a, byte[] b)
        {
            var result = new byte[a.Length + b.Length];
            Array.Copy(a, result, a.Length);
            Array.Copy(b, 0, result, a.Length, b.Length);
            return result;
        }
    }
}
