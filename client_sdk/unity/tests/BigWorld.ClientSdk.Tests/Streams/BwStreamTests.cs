using System;
using System.IO;
using System.Text;
using BigWorld.ClientSdk.Streams;
using Xunit;

namespace BigWorld.ClientSdk.Tests.Streams
{
    /// <summary>
    /// Byte-order / packed-int / string codec tests. Wire facts verified against
    /// programming/bigworld/lib/cstdmf/binary_stream.ipp and binary_stream.hpp
    /// (see client_sdk/unity/PROTOCOL.md rows STM-*).
    /// </summary>
    public sealed class BwStreamTests
    {
        // STM-1: all multi-byte scalars are little-endian.
        [Fact]
        public void UInt16_IsLittleEndian()
        {
            var writer = new BwWriter();
            writer.WriteUInt16(0x1234);
            Assert.Equal(new byte[] { 0x34, 0x12 }, writer.ToArray());
        }

        [Fact]
        public void UInt32_IsLittleEndian()
        {
            var writer = new BwWriter();
            writer.WriteUInt32(0x12345678u);
            Assert.Equal(new byte[] { 0x78, 0x56, 0x34, 0x12 }, writer.ToArray());
        }

        [Fact]
        public void UInt64_IsLittleEndian()
        {
            var writer = new BwWriter();
            writer.WriteUInt64(0x0102030405060708UL);
            Assert.Equal(
                new byte[] { 0x08, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01 },
                writer.ToArray());
        }

        [Fact]
        public void Int32_MinusOne_IsAllOnes()
        {
            var writer = new BwWriter();
            writer.WriteInt32(-1);
            Assert.Equal(new byte[] { 0xFF, 0xFF, 0xFF, 0xFF }, writer.ToArray());
        }

        // STM-2: floats/doubles stream as raw IEEE-754 in little-endian order.
        [Fact]
        public void Float_IsLittleEndianIeee754()
        {
            var writer = new BwWriter();
            writer.WriteFloat(1.5f);
            // 1.5f = 0x3FC00000
            Assert.Equal(new byte[] { 0x00, 0x00, 0xC0, 0x3F }, writer.ToArray());
        }

        [Fact]
        public void Float_RoundTrips()
        {
            RoundTrip(w => w.WriteFloat(-273.25f), r => Assert.Equal(-273.25f, r.ReadFloat()));
        }

        [Fact]
        public void Double_RoundTrips()
        {
            RoundTrip(w => w.WriteDouble(3.14159265358979), r => Assert.Equal(3.14159265358979, r.ReadDouble()));
        }

        // STM-3: packed int, binary_stream.hpp:254-300.
        [Theory]
        [InlineData(0, new byte[] { 0x00 })]
        [InlineData(1, new byte[] { 0x01 })]
        [InlineData(254, new byte[] { 0xFE })]
        [InlineData(255, new byte[] { 0xFF, 0xFF, 0x00, 0x00 })]
        [InlineData(256, new byte[] { 0xFF, 0x00, 0x01, 0x00 })]
        [InlineData(0x010203, new byte[] { 0xFF, 0x03, 0x02, 0x01 })]
        [InlineData(0xFFFFFF, new byte[] { 0xFF, 0xFF, 0xFF, 0xFF })]
        public void PackedInt_Encoding(int value, byte[] expected)
        {
            var writer = new BwWriter();
            writer.WritePackedInt(value);
            Assert.Equal(expected, writer.ToArray());
        }

        [Theory]
        [InlineData(0)]
        [InlineData(1)]
        [InlineData(254)]
        [InlineData(255)]
        [InlineData(4096)]
        [InlineData(0xFFFFFF)]
        public void PackedInt_RoundTrips(int value)
        {
            var writer = new BwWriter();
            writer.WritePackedInt(value);
            var reader = new BwReader(writer.ToArray());
            Assert.Equal(value, reader.ReadPackedInt());
            Assert.Equal(0, reader.Remaining);
        }

        [Fact]
        public void PackedInt_RejectsNegative()
        {
            var writer = new BwWriter();
            Assert.Throws<ArgumentOutOfRangeException>(() => writer.WritePackedInt(-1));
        }

        [Fact]
        public void PackedInt_RejectsTwoToThe24()
        {
            var writer = new BwWriter();
            Assert.Throws<ArgumentOutOfRangeException>(() => writer.WritePackedInt(1 << 24));
        }

        // STM-4: string = packed-int length + UTF-8 bytes (appendString).
        [Fact]
        public void String_Encoding_IsPackedLengthPlusUtf8()
        {
            var writer = new BwWriter();
            writer.WriteString("hello");
            Assert.Equal(
                new byte[] { 0x05, (byte)'h', (byte)'e', (byte)'l', (byte)'l', (byte)'o' },
                writer.ToArray());
        }

        [Fact]
        public void String_MultibyteUtf8_UsesByteLength()
        {
            // 你好 is 6 UTF-8 bytes.
            var writer = new BwWriter();
            writer.WriteString("你好");
            Assert.Equal(7, writer.Length);
            Assert.Equal(6, writer.ToArray()[0]);

            var reader = new BwReader(writer.ToArray());
            Assert.Equal("你好", reader.ReadString());
        }

        [Fact]
        public void String_Empty_And_Null()
        {
            var writer = new BwWriter();
            writer.WriteString(string.Empty);
            Assert.Equal(new byte[] { 0x00 }, writer.ToArray());

            writer.Reset();
            writer.WriteString(null);
            Assert.Equal(new byte[] { 0x00 }, writer.ToArray());
        }

        [Fact]
        public void String_Long_UsesThreeBytePackedLength()
        {
            string value = new string('a', 300);
            var writer = new BwWriter();
            writer.WriteString(value);
            Assert.Equal(new byte[] { 0xFF, 300 & 0xFF, 0x01, 0x00 }, writer.ToArray()[..4]);

            var reader = new BwReader(writer.ToArray());
            Assert.Equal(value, reader.ReadString());
        }

        // STM-5: Vector3 = 3 little-endian floats x,y,z.
        [Fact]
        public void Vector3_IsTwelveBytes()
        {
            var writer = new BwWriter();
            writer.WriteVector3(1f, 2f, 3f);
            Assert.Equal(12, writer.Length);

            var reader = new BwReader(writer.ToArray());
            reader.ReadFloat(); // spot check one component, round trip below
        }

        [Fact]
        public void Vector3_RoundTrips()
        {
            var writer = new BwWriter();
            writer.WriteVector3(1.25f, -2.5f, 1000f);
            var reader = new BwReader(writer.ToArray());
            Assert.Equal(1.25f, reader.ReadFloat());
            Assert.Equal(-2.5f, reader.ReadFloat());
            Assert.Equal(1000f, reader.ReadFloat());
            Assert.Equal(0, reader.Remaining);
        }

        [Fact]
        public void Bool_WritesSingleByte()
        {
            var writer = new BwWriter();
            writer.WriteBool(true);
            writer.WriteBool(false);
            Assert.Equal(new byte[] { 0x01, 0x00 }, writer.ToArray());
        }

        [Fact]
        public void Blob_WritesRawBytesWithoutPrefix()
        {
            var writer = new BwWriter();
            writer.WriteBlob(new byte[] { 0x0A, 0x0B, 0x0C });
            Assert.Equal(new byte[] { 0x0A, 0x0B, 0x0C }, writer.ToArray());
        }

        // STM-6: patching in place (used for the bundle request-offset chain).
        [Fact]
        public void PatchUInt32_OverwritesInPlace()
        {
            var writer = new BwWriter();
            writer.WriteUInt8(0xAA);
            writer.WriteUInt32(0u);
            writer.WriteUInt8(0xBB);
            writer.PatchUInt32(1, 0xCAFEBABEu);
            Assert.Equal(
                new byte[] { 0xAA, 0xBE, 0xBA, 0xFE, 0xCA, 0xBB },
                writer.ToArray());
        }

        [Fact]
        public void PatchUInt32_OutOfRange_Throws()
        {
            var writer = new BwWriter();
            writer.WriteUInt32(1u);
            Assert.Throws<ArgumentOutOfRangeException>(() => writer.PatchUInt32(2, 1u));
        }

        // STM-7: reader bounds behaviour.
        [Fact]
        public void Reader_Underrun_ThrowsEndOfStream()
        {
            var reader = new BwReader(new byte[] { 0x01, 0x02 });
            Assert.Throws<EndOfStreamException>(() => reader.ReadUInt32());
        }

        [Fact]
        public void Reader_TracksPositionAndRemaining()
        {
            var reader = new BwReader(new byte[] { 0x01, 0x02, 0x03, 0x04 });
            reader.ReadUInt16();
            Assert.Equal(2, reader.Position);
            Assert.Equal(2, reader.Remaining);
            reader.Skip(2);
            Assert.Equal(4, reader.Position);
            Assert.Equal(0, reader.Remaining);
        }

        [Fact]
        public void Reader_RejectsOutOfRangeWindow()
        {
            byte[] buffer = new byte[4];
            Assert.Throws<ArgumentOutOfRangeException>(() => new BwReader(buffer, 2, 3));
            Assert.Throws<ArgumentOutOfRangeException>(() => new BwReader(buffer, -1, 2));
        }

        [Fact]
        public void Reader_ReadBytesRaw_Consumes()
        {
            var reader = new BwReader(new byte[] { 0x01, 0x02, 0x03, 0x04 });
            byte[] head = reader.ReadBytesRaw(2);
            Assert.Equal(new byte[] { 0x01, 0x02 }, head);
            Assert.Equal(2, reader.Remaining);
        }

        [Fact]
        public void Writer_GrowsBeyondInitialCapacity()
        {
            var writer = new BwWriter(4);
            for (int i = 0; i < 1000; i++)
            {
                writer.WriteUInt32((uint)i);
            }

            Assert.Equal(4000, writer.Length);
            var reader = new BwReader(writer.ToArray());
            for (int i = 0; i < 1000; i++)
            {
                Assert.Equal((uint)i, reader.ReadUInt32());
            }
        }

        [Fact]
        public void Int8_SignExtendsOnRoundTrip()
        {
            var writer = new BwWriter();
            writer.WriteInt8(-1);
            Assert.Equal(new byte[] { 0xFF }, writer.ToArray());
            var reader = new BwReader(writer.ToArray());
            Assert.Equal(-1, reader.ReadInt8());
        }

        [Fact]
        public void Reset_AllowsReuse()
        {
            var writer = new BwWriter();
            writer.WriteUInt32(0xDEADBEEFu);
            writer.Reset();
            writer.WriteUInt16(0x00FF);
            Assert.Equal(new byte[] { 0xFF, 0x00 }, writer.ToArray());
        }

        private static void RoundTrip(Action<BwWriter> write, Action<BwReader> verify)
        {
            var writer = new BwWriter();
            write(writer);
            var reader = new BwReader(writer.ToArray());
            verify(reader);
            Assert.Equal(0, reader.Remaining);
        }
    }
}
