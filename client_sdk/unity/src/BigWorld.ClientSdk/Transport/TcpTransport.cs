using System;
using System.IO;
using System.Net.Sockets;
using System.Threading;
using System.Threading.Tasks;

namespace BigWorld.ClientSdk.Transport
{
    /// <summary>
    /// TCP stream transport. Receives on a background loop and re-raises raw
    /// byte chunks as <see cref="DataReceived"/>; reassembly into frames is done
    /// by PacketReceiver.
    /// </summary>
    public sealed class TcpTransport : ITransport
    {
        private const int ReceiveBufferSize = 64 * 1024;

        private TcpClient? _client;
        private readonly byte[] _buffer = new byte[ReceiveBufferSize];
        private int _closed;

        public event EventHandler? Connected;

        public event EventHandler<DataReceivedEventArgs>? DataReceived;

        public event EventHandler<TransportClosedEventArgs>? Closed;

        public bool IsConnected =>
            _client?.Client is { Connected: true };

        public async Task ConnectAsync(string host, int port, CancellationToken cancellationToken = default)
        {
            cancellationToken.ThrowIfCancellationRequested();

            var client = new TcpClient();
            _client = client;

            CancellationTokenRegistration registration = cancellationToken.Register(
                static state => ((TcpClient?)state)?.Close(), client);

            try
            {
                await client.ConnectAsync(host, port).ConfigureAwait(false);

                client.NoDelay = true;
                client.Client.SetSocketOption(SocketOptionLevel.Socket, SocketOptionName.KeepAlive, true);

                Connected?.Invoke(this, EventArgs.Empty);
                _ = ReceiveLoopAsync(client);
            }
            finally
            {
                registration.Dispose();
            }
        }

        private async Task ReceiveLoopAsync(TcpClient client)
        {
            NetworkStream stream = client.GetStream();
            try
            {
                while (!_closed.Equals(1))
                {
                    int read = await stream.ReadAsync(_buffer, 0, _buffer.Length).ConfigureAwait(false);
                    if (read == 0)
                    {
                        Close("peer closed");
                        return;
                    }

                    DataReceived?.Invoke(
                        this, new DataReceivedEventArgs(_buffer, 0, read));
                }
            }
            catch (OperationCanceledException)
            {
                Close("cancelled");
            }
            catch (IOException)
            {
                Close("stream error");
            }
            catch (ObjectDisposedException)
            {
                Close("disposed");
            }
        }

        public void Send(byte[] data, int offset, int count)
        {
            if (data == null)
            {
                throw new ArgumentNullException(nameof(data));
            }

            TcpClient? client = _client;
            if (client == null || Volatile.Read(ref _closed) != 0)
            {
                throw new IOException("transport is not connected");
            }

            NetworkStream stream = client.GetStream();
            stream.Write(data, offset, count);
        }

        /// <summary>Close the connection. Safe to call multiple times.</summary>
        public void Close(string? reason)
        {
            if (Interlocked.Exchange(ref _closed, 1) != 0)
            {
                return;
            }

            try
            {
                _client?.Close();
            }
            catch (ObjectDisposedException)
            {
                // Already closed.
            }

            Closed?.Invoke(this, new TransportClosedEventArgs(reason));
        }

        public void Dispose()
        {
            Close("disposed");
            _client?.Dispose();
        }
    }
}