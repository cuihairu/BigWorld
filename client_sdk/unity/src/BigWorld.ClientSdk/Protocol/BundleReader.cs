using System;
using System.Collections.Generic;
using BigWorld.ClientSdk.Streams;

namespace BigWorld.ClientSdk.Protocol
{
    /// <summary>One decoded message from a bundle.</summary>
    public sealed class MessageRecord
    {
        public MessageRecord(
            byte messageId,
            MessageSpec spec,
            int payloadOffset,
            int payloadLength,
            bool isRequest,
            int replyId,
            byte[] bundleBuffer)
        {
            MessageId = messageId;
            Spec = spec;
            PayloadOffset = payloadOffset;
            PayloadLength = payloadLength;
            IsRequest = isRequest;
            ReplyId = replyId;
            BundleBuffer = bundleBuffer;
        }

        /// <summary>Message id (first byte of the record).</summary>
        public byte MessageId { get; }

        public MessageSpec Spec { get; }

        /// <summary>Offset of the payload inside <see cref="BundleBuffer"/>.</summary>
        public int PayloadOffset { get; }

        public int PayloadLength { get; }

        /// <summary>True iff this record was part of the bundle's request chain.</summary>
        public bool IsRequest { get; }

        /// <summary>Reply id; valid when <see cref="IsRequest"/>.</summary>
        public int ReplyId { get; }

        public byte[] BundleBuffer { get; }
    }

    /// <summary>
    /// Parses a Mercury TCP bundle payload, mirroring
    /// Mercury::TCPBundleProcessor::dispatchMessages
    /// (programming/bigworld/lib/network/tcp_bundle_processor.cpp:34-201).
    ///
    /// Layout consumed:
    ///   [flags: uint8]
    ///   [firstRequestOffset: uint32]  — iff flags &amp; FLAG_HAS_REQUESTS
    ///   <message records>
    ///
    /// Message record:
    ///   [messageId: uint8]
    ///   [length: L bytes]             — variable msgs; all-0xFF &rarr; uint32 follows
    ///   [replyId: int32]              — requests
    ///   [nextRequestOffset: uint32]   — requests
    ///   [payload: length bytes]
    /// </summary>
    public sealed class BundleReader
    {
        /// <summary>
        /// Parse a bundle payload. Throws on unknown message ids, out-of-bounds
        /// request offsets and short payloads, matching the
        /// REASON_CORRUPTED_PACKET / REASON_NONEXISTENT_ENTRY paths in the C++
        /// processor.
        /// </summary>
        public static IReadOnlyList<MessageRecord> Parse(
            byte[] bundle, int offset, int length, MessageSpecTable table)
        {
            var reader = new BwReader(bundle, offset, length);
            int bundleStart = reader.Position;
            var records = new List<MessageRecord>();

            byte flags = reader.ReadUInt8();
            if ((flags & ~MessageSpec.BundleFlagMask) != 0)
            {
                throw new ProtocolException(
                    $"Invalid bundle flags 0x{flags:x2}");
            }

            uint nextRequestOffset = MessageSpec.LastRequestOffset;
            if ((flags & MessageSpec.BundleFlagHasRequests) != 0)
            {
                nextRequestOffset = reader.ReadUInt32();
                // The C++ processor casts to int before comparing
                // (tcp_bundle_processor.cpp:76), so the 0xFFFFFFFF sentinel
                // compares as -1 and is accepted.
                if ((int)nextRequestOffset > length)
                {
                    throw new ProtocolException(
                        $"Out-of-bounds first request offset {nextRequestOffset} > {length}");
                }
            }

            while (reader.Remaining > 0)
            {
                int msgStart = reader.Position - bundleStart;

                byte messageId = reader.ReadUInt8();
                if (!table.TryGet(messageId, out MessageSpec? spec) || spec == null)
                {
                    throw new ProtocolException($"Unhandled message id {messageId}");
                }

                int payloadLength;
                if (spec.Style == MessageLengthStyle.Variable)
                {
                    // The length field is a little-endian scalar of width
                    // LengthParam, exactly InterfaceElement::expandLength
                    // (interface_element.cpp:530-556, BW_NTOHS / BW_UNPACK3 /
                    // BW_NTOHL).
                    payloadLength = spec.LengthParam switch
                    {
                        0 => 0,
                        1 => reader.ReadUInt8(),
                        2 => reader.ReadUInt16(),
                        3 => reader.ReadUInt8()
                            | (reader.ReadUInt8() << 8)
                            | (reader.ReadUInt8() << 16),
                        4 => unchecked((int)reader.ReadUInt32()),
                        _ => throw new ProtocolException(
                            $"Invalid length parameter {spec.LengthParam} for {spec.Name}")
                    };

                    // expandLength returns -1 when the value overflows its field
                    // (interface_element.cpp:565) or cannot be represented
                    // (:576-583); the processor then reads the special uint32
                    // length (tcp_bundle_processor.cpp:134-140). For widths 1-3
                    // this is exactly the all-ones field; for width 4 it is any
                    // field >= 0x80000000.
                    if (payloadLength < 0 || !spec.CanHandleLength(payloadLength))
                    {
                        payloadLength = (int)reader.ReadUInt32();
                    }
                }
                else
                {
                    payloadLength = spec.LengthParam;
                }

                bool isRequest = (uint)msgStart == nextRequestOffset;
                int replyId = 0;
                if (isRequest)
                {
                    replyId = reader.ReadInt32();
                    nextRequestOffset = reader.ReadUInt32();
                    // Same int-cast as tcp_bundle_processor.cpp:159: the
                    // 0xFFFFFFFF "last request" sentinel must not be rejected.
                    if ((int)nextRequestOffset > length)
                    {
                        throw new ProtocolException(
                            $"Out-of-bounds next request offset {nextRequestOffset} > {length}");
                    }
                }

                if (reader.Remaining < payloadLength)
                {
                    throw new ProtocolException(
                        $"Truncated payload for message id {messageId}: " +
                        $"need {payloadLength}, have {reader.Remaining}");
                }

                records.Add(new MessageRecord(
                    messageId,
                    spec,
                    reader.Position,
                    payloadLength,
                    isRequest,
                    replyId,
                    bundle));

                reader.Skip(payloadLength);
            }

            return records;
        }

        /// <summary>
        /// Read the payload of a record as a fresh <see cref="BwReader"/>.
        /// </summary>
        public static BwReader PayloadReader(MessageRecord record) =>
            new BwReader(record.BundleBuffer, record.PayloadOffset, record.PayloadLength);
    }
}