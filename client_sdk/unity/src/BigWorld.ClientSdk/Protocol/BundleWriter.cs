using System;
using System.Collections.Generic;
using BigWorld.ClientSdk.Streams;

namespace BigWorld.ClientSdk.Protocol
{
    /// <summary>
    /// Builds a Mercury TCP bundle payload, mirroring Mercury::TCPBundle
    /// (programming/bigworld/lib/network/tcp_bundle.cpp).
    ///
    /// Output layout (see PROTOCOL.md, rows BND-*):
    ///   [flags: uint8]                — bit0 = FLAG_HAS_REQUESTS
    ///   [firstRequestOffset: uint32]  — present iff flags bit0 set
    ///   <message records...>
    ///
    /// Each message record:
    ///   [messageId: uint8]
    ///   [length: L bytes]             — L = spec.LengthParam (variable msgs only)
    ///   [replyId: int32]              — requests only
    ///   [nextRequestOffset: uint32]   — requests only
    ///   [payload...]
    ///
    /// Oversized variable messages (payload &gt;= (1&lt;&lt;(8L))-1) use the special
    /// encoding: length field = all 0xFF, then a uint32 length inserted before
    /// the payload (tcp_bundle.cpp finaliseCurrentMessage, interface_element.cpp
    /// compressLength).
    /// </summary>
    public sealed class BundleWriter
    {
        private sealed class PendingMessage
        {
            public MessageSpec Spec = null!;
            public bool IsRequest;
            public int Id;
            public int SegmentEnd;    // end offset in _messages (exclusive)
            public int BodyStart;     // offset in _messages of first body byte
            public int ReplyIdOffset; // offset in _messages of [replyId] (request only)
            public int BodyLength;
        }

        private readonly BwWriter _messages;
        private readonly List<PendingMessage> _pending = new List<PendingMessage>();
        private PendingMessage? _current;

        public BundleWriter(int initialCapacity = 256)
        {
            _messages = new BwWriter(initialCapacity);
        }

        public bool HasRequests { get; private set; }

        public int MessageCount => _pending.Count;

        /// <summary>Reset for reuse.</summary>
        public void Clear()
        {
            _messages.Reset();
            _pending.Clear();
            _current = null;
            HasRequests = false;
        }

        /// <summary>Begin a non-request message. Returns the payload writer.</summary>
        public BwWriter StartMessage(MessageSpec spec) => BeginMessage(spec, isRequest: false, replyId: 0);

        /// <summary>Begin a request message. replyId is written verbatim into the
        /// request header (the caller assigns the id).</summary>
        public BwWriter StartRequest(MessageSpec spec, int replyId) =>
            BeginMessage(spec, isRequest: true, replyId);

        private BwWriter BeginMessage(MessageSpec spec, bool isRequest, int replyId)
        {
            if (_current != null)
            {
                throw new InvalidOperationException("previous message not terminated (call EndMessage)");
            }

            var pending = new PendingMessage
            {
                Spec = spec,
                IsRequest = isRequest,
                Id = spec.Id
            };

            _current = pending;

            // [id]
            _messages.WriteUInt8((byte)spec.Id);

            // [length field] for variable-length messages
            if (spec.Style == MessageLengthStyle.Variable)
            {
                _messages.Reserve(spec.LengthParam);
            }

            // [replyId][nextRequestOffset] for requests
            if (isRequest)
            {
                pending.ReplyIdOffset = _messages.Length;
                _messages.WriteInt32(replyId);
                _messages.WriteUInt32(MessageSpec.LastRequestOffset);
                HasRequests = true;
            }

            pending.BodyStart = _messages.Length;
            return _messages;
        }

        /// <summary>
        /// Terminate the current message. bodyLength is the number of payload
        /// bytes written since StartMessage/StartRequest.
        /// </summary>
        public void EndMessage(int bodyLength)
        {
            if (_current == null)
            {
                throw new InvalidOperationException("no message in progress");
            }

            _current.BodyLength = bodyLength;
            _current.SegmentEnd = _messages.Length;

            if (_messages.Length - _current.BodyStart != bodyLength)
            {
                throw new InvalidOperationException("body length does not match bytes written");
            }

            _pending.Add(_current);
            _current = null;
        }

        /// <summary>Encode the complete bundle payload.</summary>
        public byte[] Build()
        {
            if (_current != null)
            {
                throw new InvalidOperationException("message in progress at Build()");
            }

            bool hasRequests = HasRequests;
            int baseOffset = hasRequests ? 1 + 4 : 1;
            byte[] messageBytes = _messages.ToArray();

            var output = new BwWriter(baseOffset + messageBytes.Length + 16);
            output.WriteUInt8(hasRequests ? MessageSpec.BundleFlagHasRequests : (byte)0);
            int firstRequestOffset = -1;
            if (hasRequests)
            {
                output.Reserve(4); // patched below
            }

            // Emit messages, inserting the oversize uint32 length as needed.
            int? prevRequestReplyId = null;
            foreach (PendingMessage pm in _pending)
            {
                int wireStart = output.Length;

                output.WriteUInt8((byte)pm.Id);

                if (pm.Spec.Style == MessageLengthStyle.Variable)
                {
                    int width = pm.Spec.LengthParam;
                    if (width >= 4 || pm.BodyLength < (1 << (8 * width)) - 1)
                    {
                        for (int i = 0; i < width; i++)
                        {
                            output.WriteUInt8((byte)((pm.BodyLength >> (8 * i)) & 0xFF));
                        }
                    }
                    else
                    {
                        for (int i = 0; i < width; i++)
                        {
                            output.WriteUInt8(0xFF);
                        }

                        output.WriteUInt32((uint)pm.BodyLength);
                    }
                }

                // Copy everything after the length field — for a request this is
                // [replyId][nextRequestOffset][body], otherwise just [body]. The
                // length field itself was written above.
                int copyStart = pm.IsRequest ? pm.ReplyIdOffset : pm.BodyStart;
                output.WriteRaw(messageBytes, copyStart, pm.SegmentEnd - copyStart);

                if (pm.IsRequest)
                {
                    if (firstRequestOffset == -1)
                    {
                        firstRequestOffset = wireStart;
                    }
                    else
                    {
                        // Patch the previous request's nextRequestOffset to this one.
                        output.PatchUInt32(prevRequestReplyId!.Value, (uint)wireStart);
                    }

                    // The nextRequestOffset field sits after [id][length][replyId].
                    int width = pm.Spec.Style == MessageLengthStyle.Variable
                        ? pm.Spec.LengthParam
                        : 0;
                    prevRequestReplyId = wireStart + 1 + width + 4;
                }
            }

            if (hasRequests)
            {
                output.PatchUInt32(1, (uint)firstRequestOffset);
            }

            return output.ToArray();
        }

        /// <summary>
        /// Convenience: build a single-message bundle.
        /// </summary>
        public static byte[] BuildSingle(MessageSpec spec, Action<BwWriter> writePayload)
        {
            var writer = new BundleWriter();
            BwWriter payload = writer.StartMessage(spec);
            int start = payload.Length;
            writePayload(payload);
            writer.EndMessage(payload.Length - start);
            return writer.Build();
        }
    }
}