using System;

namespace BigWorld.ClientSdk.Protocol
{
    /// <summary>
    /// Incremental TCP frame reassembler. Handles both sticky packets (many
    /// frames in one read) and partial packets (a frame split across reads),
    /// mirroring TCPChannel::handleInput / cleanUpAfterFrameReceive
    /// (programming/bigworld/lib/network/tcp_channel.cpp:130-256).
    /// </summary>
    public sealed class PacketReceiver
    {
        private byte[] _buffer;
        private int _start;
        private int _end;

        /// <param name="maxFrameLength">Sanity cap on a single frame, default 64 MiB.</param>
        public PacketReceiver(int maxFrameLength = 64 * 1024 * 1024)
        {
            MaxFrameLength = maxFrameLength;
            _buffer = new byte[4096];
        }

        public int MaxFrameLength { get; }

        /// <summary>Feed bytes read from the wire.</summary>
        public void Append(byte[] data, int offset, int count)
        {
            if (data == null)
            {
                throw new ArgumentNullException(nameof(data));
            }

            EnsureCapacity(count);
            Array.Copy(data, offset, _buffer, _end, count);
            _end += count;
        }

        /// <summary>
        /// Extract the next complete frame if one is available. Returns false if
        /// more data is needed.
        /// </summary>
        public bool TryTakeFrame(out byte[]? frame)
        {
            frame = null;
            int remaining = _end - _start;
            if (remaining == 0)
            {
                return false;
            }

            if (!TcpFraming.TryDecodeFramePrefix(
                    _buffer, _start, remaining, out int payloadLength, out int prefixBytes))
            {
                return false;
            }

            if (payloadLength > MaxFrameLength)
            {
                throw new InvalidOperationException(
                    $"Frame length {payloadLength} exceeds cap {MaxFrameLength}");
            }

            if (remaining < prefixBytes + payloadLength)
            {
                return false;
            }

            frame = new byte[payloadLength];
            Array.Copy(_buffer, _start + prefixBytes, frame, 0, payloadLength);

            _start += prefixBytes + payloadLength;
            Compact();
            return true;
        }

        /// <summary>Number of buffered but not-yet-framed bytes.</summary>
        public int PendingBytes => _end - _start;

        private void EnsureCapacity(int extra)
        {
            if (_end + extra <= _buffer.Length)
            {
                return;
            }

            // Compact consumed bytes first.
            if (_start > 0 && _end + extra <= _buffer.Length + _start)
            {
                Array.Copy(_buffer, _start, _buffer, 0, _end - _start);
                _end -= _start;
                _start = 0;
                return;
            }

            int newSize = Math.Max(_buffer.Length * 2, _end + extra);
            Array.Resize(ref _buffer, newSize);
        }

        private void Compact()
        {
            // Reclaim consumed space once it dominates the buffer.
            if (_start > 4096 && _start * 2 > _end)
            {
                Array.Copy(_buffer, _start, _buffer, 0, _end - _start);
                _end -= _start;
                _start = 0;
            }
        }
    }
}