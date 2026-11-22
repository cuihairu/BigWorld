using System;

namespace BigWorld.ClientSdk.Protocol
{
    /// <summary>
    /// Mirror of Mercury::InterfaceElement length styles
    /// (programming/bigworld/lib/network/interface_element.hpp:34-56).
    /// </summary>
    public enum MessageLengthStyle
    {
        /// <summary>FIXED_LENGTH_MESSAGE (0): LengthParam is the payload size.</summary>
        Fixed = 0,

        /// <summary>VARIABLE_LENGTH_MESSAGE (1): LengthParam is the width of the
        /// length field stored after the message id (1..4 bytes).</summary>
        Variable = 1
    }

    /// <summary>
    /// One entry of a Mercury interface: message id, length style and length
    /// parameter. Direct analogue of Mercury::InterfaceElement
    /// (programming/bigworld/lib/network/interface_element.hpp:57-235).
    /// </summary>
    public sealed class MessageSpec
    {
        /// <summary>REPLY_MESSAGE_IDENTIFIER (interface_element.hpp:380).</summary>
        public const byte ReplyMessageIdentifier = 0xFF;

        /// <summary>Mercury::InterfaceElement::REPLY (interface_element.cpp:24-25):
        /// id 0xFF, VARIABLE_LENGTH_MESSAGE, lengthParam 4.</summary>
        public static readonly MessageSpec Reply = new MessageSpec(
            ReplyMessageIdentifier, "Reply", MessageLengthStyle.Variable, 4);

        /// <summary>Flag bit for bundles that carry at least one request
        /// (TCPBundle::FLAG_HAS_REQUESTS, tcp_bundle.hpp:31).</summary>
        public const byte BundleFlagHasRequests = 0x01;

        /// <summary>Receiver rejects flags outside FLAG_MASK
        /// (tcp_bundle_processor.cpp:73-80).</summary>
        public const byte BundleFlagMask = 0x01;

        /// <summary>Sentinel "no more requests" offset, TCPBundle::Offset(-1)
        /// (tcp_bundle.cpp:116).</summary>
        public const uint LastRequestOffset = 0xFFFFFFFF;

        public MessageSpec(byte id, string name, MessageLengthStyle style, int lengthParam)
        {
            if (style == MessageLengthStyle.Variable && (lengthParam < 0 || lengthParam > 4))
            {
                throw new ArgumentOutOfRangeException(
                    nameof(lengthParam), "variable length width must be 0..4");
            }

            if (style == MessageLengthStyle.Fixed && lengthParam < 0)
            {
                throw new ArgumentOutOfRangeException(nameof(lengthParam));
            }

            Id = id;
            Name = name;
            Style = style;
            LengthParam = lengthParam;
        }

        public byte Id { get; }

        public string Name { get; }

        public MessageLengthStyle Style { get; }

        public int LengthParam { get; }

        public bool IsReply => Id == ReplyMessageIdentifier;

        /// <summary>
        /// Maximum payload length representable in the length field for a
        /// variable-width header: length &lt; (1 &lt;&lt; (8*width)) - 1
        /// (InterfaceElement::canHandleLength, interface_element.hpp:127-135).
        /// Fixed messages have no length field and return int.MaxValue.
        /// </summary>
        public int MaxPayloadLength => Style == MessageLengthStyle.Variable
            ? LengthParam >= 4 ? int.MaxValue : (1 << (8 * LengthParam)) - 1
            : int.MaxValue;

        /// <summary>
        /// Whether payloadSize fits in this message's length field
        /// (InterfaceElement::canHandleLength, interface_element.hpp:127-135).
        /// </summary>
        public bool CanHandleLength(int payloadSize)
        {
            if (Style != MessageLengthStyle.Variable)
            {
                return true;
            }

            return LengthParam >= 4 || payloadSize < MaxPayloadLength;
        }

        public override string ToString() => $"{Name}({Id},{Style}:{LengthParam})";
    }
}