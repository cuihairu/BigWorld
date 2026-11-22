using System;
using BigWorld.ClientSdk.Streams;

namespace BigWorld.ClientSdk.Protocol
{
    /// <summary>
    /// TCP frame length-prefix codec.
    ///
    /// Source authority:
    ///   - Write: TCPChannel::doSend (programming/bigworld/lib/network/tcp_channel.cpp:404-432)
    ///   - Read: TCPChannel::readSmallFrameLength / readLargeFrameLength
    ///     (tcp_channel.cpp:361-394)
    ///   - Client header: CLIENT_HEADER "TCP\r\n\r\n" (tcp_channel.cpp:42-43),
    ///     readHeader (tcp_channel.cpp:279-315)
    ///
    /// Layout:
    ///   [uint16 LE n]            — n == 0xFFFF means "large form" follows
    ///   [uint32 LE length]        — large form
    ///   [payload...]
    ///
    /// The client's first send carries the 8-byte header frame before the
    /// bundle frame (tcp_channel.cpp:406-411).
    /// </summary>
    public static class TcpFraming
    {
        /// <summary>Client header "TCP\r\n\r\n" (tcp_channel.cpp:29).</summary>
        public static readonly byte[] ClientHeader =
            { (byte)'T', (byte)'C', (byte)'P', 0x0D, 0x0A, 0x0D, 0x0A };

        /// <summary>Size of the header frame (uint16 length + 7-byte payload),
        /// tcp_channel.cpp:370-375 (CLIENT_HEADER_SIZE = 7).</summary>
        public static int ClientHeaderFrameLength => 2 + ClientHeader.Length;

        /// <summary>0xFFFF small-length marker for the large (uint32) form.</summary>
        public const ushort LargeFormMarker = 0xFFFF;

        /// <summary>Maximum payload size expressible in the small form.</summary>
        public const int SmallFormMax = ushort.MaxValue - 1;

        /// <summary>Encode the length prefix for a payload of payloadLength bytes.</summary>
        public static byte[] EncodeFramePrefix(int payloadLength)
        {
            if (payloadLength < 0)
            {
                throw new ArgumentOutOfRangeException(nameof(payloadLength));
            }

            var writer = new BwWriter(6);
            if (payloadLength >= ushort.MaxValue)
            {
                writer.WriteUInt16(LargeFormMarker);
                writer.WriteUInt32((uint)payloadLength);
            }
            else
            {
                writer.WriteUInt16((ushort)payloadLength);
            }

            return writer.ToArray();
        }

        /// <summary>
        /// Try to decode a length prefix from the buffer head.
        /// </summary>
        /// <param name="data">Bytes available.</param>
        /// <param name="consumed">Bytes the prefix occupies (0 when incomplete).</param>
        /// <param name="payloadLength">Decoded payload length.</param>
        /// <returns>True if a complete prefix was decoded.</returns>
        public static bool TryDecodeFramePrefix(
            byte[] data, int offset, int length,
            out int payloadLength, out int consumed)
        {
            payloadLength = 0;
            consumed = length;

            if (length < 2)
            {
                return false;
            }

            var reader = new BwReader(data, offset, 2);
            ushort small = reader.ReadUInt16();
            if (small != LargeFormMarker)
            {
                payloadLength = small;
                consumed = 2;
                return true;
            }

            if (length < 6)
            {
                return false;
            }

            payloadLength = (int)new BwReader(data, offset + 2, 4).ReadUInt32();
            consumed = 6;
            return true;
        }

        /// <summary>Decode a complete frame prefix without consuming (header only).</summary>
        public static int FramePrefixBytes(int payloadLength) =>
            payloadLength >= ushort.MaxValue ? 6 : 2;
    }
}