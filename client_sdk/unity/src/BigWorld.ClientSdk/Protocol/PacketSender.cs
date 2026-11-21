using System;
using System.IO;
using BigWorld.ClientSdk.Streams;
using BigWorld.ClientSdk.Transport;

namespace BigWorld.ClientSdk.Protocol
{
    /// <summary>
    /// Writes Mercury TCP frames over an <see cref="ITransport"/>. On the first
    /// send it emits the client header frame ("TCP\r\n\r\n") before the bundle
    /// frame, exactly as TCPChannel::doSend does
    /// (programming/bigworld/lib/network/tcp_channel.cpp:404-432).
    /// </summary>
    public sealed class PacketSender : IDisposable
    {
        private readonly ITransport _transport;
        private bool _headerSent;

        public PacketSender(ITransport transport)
        {
            _transport = transport ?? throw new ArgumentNullException(nameof(transport));
        }

        /// <summary>
        /// Send a bundle payload as one frame. On the first call the client
        /// header frame is emitted first.
        /// </summary>
        public void SendBundle(byte[] bundlePayload)
        {
            if (bundlePayload == null)
            {
                throw new ArgumentNullException(nameof(bundlePayload));
            }

            var writer = new BwWriter(bundlePayload.Length + 16);
            if (!_headerSent)
            {
                _headerSent = true;
                writer.WriteUInt16((ushort)TcpFraming.ClientHeader.Length);
                writer.WriteRaw(TcpFraming.ClientHeader, 0, TcpFraming.ClientHeader.Length);
            }

            byte[] prefix = TcpFraming.EncodeFramePrefix(bundlePayload.Length);
            writer.WriteRaw(prefix, 0, prefix.Length);
            writer.WriteRaw(bundlePayload, 0, bundlePayload.Length);

            byte[] frame = writer.ToArray();
            _transport.Send(frame, 0, frame.Length);
        }

        public void Dispose()
        {
            // The transport is owned by the connection, not the sender.
        }
    }
}