using System;
using System.Text;

namespace BigWorld.ClientSdk.Streams
{
    /// <summary>
    /// BigWorld network byte-order writer (little-endian).
    ///
    /// Source authority:
    ///   - Byte order: programming/bigworld/lib/cstdmf/binary_stream.ipp:8
    ///     ("BigWorld network byte order is little-endian, not big-endian").
    ///   - Packed int: BinaryOStream::writePackedInt
    ///     (programming/bigworld/lib/cstdmf/binary_stream.hpp:254-270).
    ///   - String: BinaryOStream::appendString
    ///     (programming/bigworld/lib/cstdmf/binary_stream.hpp:382-392) = packedInt + bytes.
    ///
    /// Every Write* method appends the value in little-endian order, matching the
    /// BW_HTON*/BW_PACK3 macros, which are identity on little-endian hosts.
    /// </summary>
    public sealed class BwWriter
    {
        private byte[] _buffer;
        private int _length;

        public BwWriter(int initialCapacity = 64)
        {
            _buffer = new byte[Math.Max(16, initialCapacity)];
            _length = 0;
        }

        /// <summary>Number of bytes written so far.</summary>
        public int Length => _length;

        /// <summary>Current contents (a copy).</summary>
        public byte[] ToArray()
        {
            var result = new byte[_length];
            Array.Copy(_buffer, 0, result, 0, _length);
            return result;
        }

        /// <summary>Raw pointer to the written bytes (valid until next write grows the buffer).</summary>
        public ArraySegment<byte> AsSegment() => new ArraySegment<byte>(_buffer, 0, _length);

        public void Reset() => _length = 0;

        private void Ensure(int extra)
        {
            if (_length + extra <= _buffer.Length)
            {
                return;
            }

            int newSize = Math.Max(_buffer.Length * 2, _length + extra);
            Array.Resize(ref _buffer, newSize);
        }

        private void WriteBytes(byte[] bytes)
        {
            Ensure(bytes.Length);
            Array.Copy(bytes, 0, _buffer, _length, bytes.Length);
            _length += bytes.Length;
        }

        public void WriteUInt8(byte value)
        {
            Ensure(1);
            _buffer[_length++] = value;
        }

        public void WriteInt8(sbyte value) => WriteUInt8((byte)value);

        public void WriteBool(bool value) => WriteUInt8(value ? (byte)1 : (byte)0);

        public void WriteUInt16(ushort value)
        {
            Ensure(2);
            _buffer[_length++] = (byte)(value & 0xFF);
            _buffer[_length++] = (byte)((value >> 8) & 0xFF);
        }

        public void WriteInt16(short value) => WriteUInt16((ushort)value);

        public void WriteUInt32(uint value)
        {
            Ensure(4);
            _buffer[_length++] = (byte)(value & 0xFF);
            _buffer[_length++] = (byte)((value >> 8) & 0xFF);
            _buffer[_length++] = (byte)((value >> 16) & 0xFF);
            _buffer[_length++] = (byte)((value >> 24) & 0xFF);
        }

        public void WriteInt32(int value) => WriteUInt32((uint)value);

        public void WriteUInt64(ulong value)
        {
            Ensure(8);
            for (int i = 0; i < 8; i++)
            {
                _buffer[_length++] = (byte)((value >> (8 * i)) & 0xFF);
            }
        }

        public void WriteInt64(long value) => WriteUInt64((ulong)value);

        public void WriteFloat(float value)
        {
            byte[] bytes = BitConverter.GetBytes(value);
            if (BigEndian)
            {
                Array.Reverse(bytes);
            }
            WriteBytes(bytes);
        }

        public void WriteDouble(double value)
        {
            byte[] bytes = BitConverter.GetBytes(value);
            if (BigEndian)
            {
                Array.Reverse(bytes);
            }
            WriteBytes(bytes);
        }

        // Detected at runtime; BigWorld's wire format is little-endian, so on a
        // big-endian host the float/double byte order is reversed.
        private static readonly bool BigEndian = BitConverter.GetBytes(1)[0] != 1;

        /// <summary>
        /// Packed (variable-length) int, exactly BinaryOStream::writePackedInt.
        /// Values 0..254 fit in 1 byte; values 255..(2^24-1) are stored as
        /// 0xFF followed by BW_PACK3 (3 little-endian bytes). The caller must
        /// ensure value &lt; 2^24, matching the MF_ASSERT in the source.
        /// </summary>
        public void WritePackedInt(int value)
        {
            if (value < 0)
            {
                throw new ArgumentOutOfRangeException(nameof(value), "packed int must be non-negative");
            }

            if (value >= 255)
            {
                if (value >= (1 << 24))
                {
                    throw new ArgumentOutOfRangeException(nameof(value), "packed int must be less than 2^24");
                }

                WriteUInt8(0xFF);
                WriteUInt8((byte)(value & 0xFF));
                WriteUInt8((byte)((value >> 8) & 0xFF));
                WriteUInt8((byte)((value >> 16) & 0xFF));
            }
            else
            {
                WriteUInt8((byte)value);
            }
        }

        /// <summary>
        /// BigWorld string: packed-int length prefix + UTF-8 bytes
        /// (BinaryOStream::appendString).
        /// </summary>
        public void WriteString(string? value)
        {
            byte[] bytes = Encoding.UTF8.GetBytes(value ?? string.Empty);
            WritePackedInt(bytes.Length);
            WriteBytes(bytes);
        }

        /// <summary>Raw bytes with no length prefix (packet payloads).</summary>
        public void WriteBlob(byte[] bytes)
        {
            if (bytes == null)
            {
                throw new ArgumentNullException(nameof(bytes));
            }
            WriteBytes(bytes);
        }

        /// <summary>3 little-endian floats: x, y, z (Vector3 streaming).</summary>
        public void WriteVector3(float x, float y, float z)
        {
            WriteFloat(x);
            WriteFloat(y);
            WriteFloat(z);
        }

        /// <summary>
        /// Patch a previously written 32-bit little-endian value in place.
        /// Used by the bundle writer for the request offset chain.
        /// </summary>
        public void PatchUInt32(int at, uint value)
        {
            if (at < 0 || at + 4 > _length)
            {
                throw new ArgumentOutOfRangeException(nameof(at), "patch out of range");
            }

            _buffer[at] = (byte)(value & 0xFF);
            _buffer[at + 1] = (byte)((value >> 8) & 0xFF);
            _buffer[at + 2] = (byte)((value >> 16) & 0xFF);
            _buffer[at + 3] = (byte)((value >> 24) & 0xFF);
        }

        /// <summary>Append raw bytes from a reader's span (used by the bundle writer).</summary>
        public void WriteRaw(byte[] source, int offset, int count)
        {
            Ensure(count);
            Array.Copy(source, offset, _buffer, _length, count);
            _length += count;
        }

        /// <summary>Reserve n bytes without writing (used for length fields).</summary>
        public int Reserve(int count)
        {
            int at = _length;
            Ensure(count);
            _length += count;
            for (int i = 0; i < count; i++)
            {
                _buffer[at + i] = 0;
            }
            return at;
        }
    }
}