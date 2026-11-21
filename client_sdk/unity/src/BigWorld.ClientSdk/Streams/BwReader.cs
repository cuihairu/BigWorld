using System;
using System.IO;
using System.Text;

namespace BigWorld.ClientSdk.Streams
{
    /// <summary>
    /// BigWorld network byte-order reader (little-endian), the mirror of
    /// <see cref="BwWriter"/>. Throws <see cref="EndOfStreamException"/> on
    /// underrun so the packet receiver can detect truncated frames.
    ///
    /// Source authority mirrors BwWriter (binary_stream.ipp byte order,
    /// readPackedInt at binary_stream.hpp:287-300, string at :309-327).
    /// </summary>
    public sealed class BwReader
    {
        private readonly byte[] _buffer;
        private int _offset;
        private readonly int _end;

        public BwReader(byte[] buffer, int offset, int length)
        {
            if (buffer == null)
            {
                throw new ArgumentNullException(nameof(buffer));
            }

            if (offset < 0 || length < 0 || offset + length > buffer.Length)
            {
                throw new ArgumentOutOfRangeException(nameof(length));
            }

            _buffer = buffer;
            _offset = offset;
            _end = offset + length;
        }

        public BwReader(byte[] buffer)
            : this(buffer, 0, buffer.Length)
        {
        }

        /// <summary>Bytes not yet consumed.</summary>
        public int Remaining => _end - _offset;

        /// <summary>Current absolute read offset.</summary>
        public int Position => _offset;

        private void Require(int count)
        {
            if (_offset + count > _end)
            {
                throw new EndOfStreamException(
                    $"Underrun: need {count} bytes, {Remaining} remaining");
            }
        }

        public byte ReadUInt8()
        {
            Require(1);
            return _buffer[_offset++];
        }

        public sbyte ReadInt8() => (sbyte)ReadUInt8();

        public bool ReadBool() => ReadUInt8() != 0;

        public ushort ReadUInt16()
        {
            Require(2);
            ushort v = (ushort)(_buffer[_offset] | (_buffer[_offset + 1] << 8));
            _offset += 2;
            return v;
        }

        public short ReadInt16() => (short)ReadUInt16();

        public uint ReadUInt32()
        {
            Require(4);
            uint v = (uint)(_buffer[_offset]
                | (_buffer[_offset + 1] << 8)
                | (_buffer[_offset + 2] << 16)
                | (_buffer[_offset + 3] << 24));
            _offset += 4;
            return v;
        }

        public int ReadInt32() => (int)ReadUInt32();

        public ulong ReadUInt64()
        {
            Require(8);
            ulong v = 0;
            for (int i = 0; i < 8; i++)
            {
                v |= (ulong)_buffer[_offset + i] << (8 * i);
            }
            _offset += 8;
            return v;
        }

        public long ReadInt64() => (long)ReadUInt64();

        public float ReadFloat()
        {
            byte[] bytes = ReadBytesRaw(4);
            if (BigEndian)
            {
                Array.Reverse(bytes);
            }
            return BitConverter.ToSingle(bytes, 0);
        }

        public double ReadDouble()
        {
            byte[] bytes = ReadBytesRaw(8);
            if (BigEndian)
            {
                Array.Reverse(bytes);
            }
            return BitConverter.ToDouble(bytes, 0);
        }

        // Mirrors BwWriter.BigEndian.
        private static readonly bool BigEndian = BitConverter.GetBytes(1)[0] != 1;

        /// <summary>Packed int, exactly BinaryIStream::readPackedInt.</summary>
        public int ReadPackedInt()
        {
            int value = ReadUInt8();
            if (value == 0xFF)
            {
                value = ReadUInt8() | (ReadUInt8() << 8) | (ReadUInt8() << 16);
            }
            return value;
        }

        /// <summary>Packed-int length prefix + UTF-8 bytes (BinaryIStream string).</summary>
        public string ReadString()
        {
            int length = ReadPackedInt();
            byte[] bytes = ReadBytesRaw(length);
            return Encoding.UTF8.GetString(bytes, 0, bytes.Length);
        }

        /// <summary>Read count raw bytes and return them (consumes from the stream).</summary>
        public byte[] ReadBytesRaw(int count)
        {
            Require(count);
            var result = new byte[count];
            Array.Copy(_buffer, _offset, result, 0, count);
            _offset += count;
            return result;
        }

        /// <summary>Read count raw bytes into destination (consumes from the stream).</summary>
        public void ReadBytesRaw(byte[] destination, int destOffset, int count)
        {
            Require(count);
            Array.Copy(_buffer, _offset, destination, destOffset, count);
            _offset += count;
        }

        /// <summary>Skip count bytes.</summary>
        public void Skip(int count)
        {
            Require(count);
            _offset += count;
        }

        public float ReadFloat32() => ReadFloat();
    }
}