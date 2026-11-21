using System;
using System.Threading;
using System.Threading.Tasks;

namespace BigWorld.ClientSdk.Transport
{
    /// <summary>Raised when the transport delivers raw bytes from the wire.</summary>
    public sealed class DataReceivedEventArgs : EventArgs
    {
        public DataReceivedEventArgs(byte[] data, int offset, int count)
        {
            Data = data;
            Offset = offset;
            Count = count;
        }

        public byte[] Data { get; }

        public int Offset { get; }

        public int Count { get; }
    }

    /// <summary>Raised when the connection closes or faults.</summary>
    public sealed class TransportClosedEventArgs : EventArgs
    {
        public TransportClosedEventArgs(string? reason)
        {
            Reason = reason;
        }

        public string? Reason { get; }
    }

    /// <summary>
    /// Byte-stream transport abstraction. The protocol core is written against
    /// this interface so it can be embedded in Unity, UE, a headless host or a
    /// mock server, matching the CL-1 requirement that the transport layer be
    /// interface-driven with TcpTransport as the first implementation.
    /// </summary>
    public interface ITransport : IDisposable
    {
        /// <summary>Raised when the connection is established.</summary>
        event EventHandler? Connected;

        /// <summary>Raised with raw bytes received from the peer.</summary>
        event EventHandler<DataReceivedEventArgs>? DataReceived;

        /// <summary>Raised exactly once when the connection ends.</summary>
        event EventHandler<TransportClosedEventArgs>? Closed;

        /// <summary>True while the transport is usable.</summary>
        bool IsConnected { get; }

        /// <summary>Open the connection. Fires <see cref="Connected"/> on success.</summary>
        Task ConnectAsync(string host, int port, CancellationToken cancellationToken = default);

        /// <summary>Write raw bytes to the wire.</summary>
        void Send(byte[] data, int offset, int count);
    }
}