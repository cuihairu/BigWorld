/**
 * BigWorld network byte-order writer (little-endian). TypeScript mirror of
 * BigWorld.ClientSdk.Streams.BwWriter; same wire facts, see
 * client_sdk/unity/PROTOCOL.md §1 (source authority: lib/cstdmf/binary_stream.ipp:8
 * "BigWorld network byte order is little-endian", binary_stream.hpp:257-270
 * writePackedInt, :382-392 appendString).
 */

const UTF8_ENCODER = new TextEncoder();

export class BwWriter {
  private buffer: Uint8Array;
  private length = 0;
  private view: DataView;

  constructor(initialCapacity = 64) {
    this.buffer = new Uint8Array(Math.max(16, initialCapacity));
    this.view = new DataView(this.buffer.buffer);
  }

  /** Number of bytes written so far. */
  get size(): number {
    return this.length;
  }

  /** Current contents (a copy). */
  toArray(): Uint8Array {
    return this.buffer.slice(0, this.length);
  }

  reset(): void {
    this.length = 0;
  }

  private ensure(extra: number): void {
    if (this.length + extra <= this.buffer.byteLength) {
      return;
    }
    const newSize = Math.max(this.buffer.byteLength * 2, this.length + extra);
    const next = new Uint8Array(newSize);
    next.set(this.buffer.subarray(0, this.length));
    this.buffer = next;
    this.view = new DataView(next.buffer);
  }

  writeUInt8(value: number): void {
    this.ensure(1);
    this.view.setUint8(this.length, value & 0xff);
    this.length += 1;
  }

  writeInt8(value: number): void {
    this.writeUInt8(value & 0xff);
  }

  writeBool(value: boolean): void {
    this.writeUInt8(value ? 1 : 0);
  }

  writeUInt16(value: number): void {
    this.ensure(2);
    this.view.setUint16(this.length, value & 0xffff, true);
    this.length += 2;
  }

  writeInt16(value: number): void {
    this.writeUInt16(value & 0xffff);
  }

  writeUInt32(value: number): void {
    this.ensure(4);
    this.view.setUint32(this.length, value >>> 0, true);
    this.length += 4;
  }

  writeInt32(value: number): void {
    this.writeUInt32(value | 0);
  }

  writeUInt64(value: number | bigint): void {
    this.ensure(8);
    this.view.setBigUint64(this.length, BigInt.asUintN(64, BigInt(value)), true);
    this.length += 8;
  }

  writeInt64(value: number | bigint): void {
    this.ensure(8);
    this.view.setBigInt64(this.length, BigInt.asIntN(64, BigInt(value)), true);
    this.length += 8;
  }

  writeFloat(value: number): void {
    this.ensure(4);
    this.view.setFloat32(this.length, value, true);
    this.length += 4;
  }

  writeDouble(value: number): void {
    this.ensure(8);
    this.view.setFloat64(this.length, value, true);
    this.length += 8;
  }

  /**
   * Packed (variable-length) int, exactly BinaryOStream::writePackedInt:
   * values 0..254 in 1 byte; 255..(2^24-1) as 0xFF + 3 little-endian bytes.
   */
  writePackedInt(value: number): void {
    if (value < 0 || value >= 1 << 24) {
      throw new RangeError(`packed int must be in [0, 2^24), got ${value}`);
    }
    if (value >= 255) {
      this.writeUInt8(0xff);
      this.writeUInt8(value & 0xff);
      this.writeUInt8((value >> 8) & 0xff);
      this.writeUInt8((value >> 16) & 0xff);
    } else {
      this.writeUInt8(value);
    }
  }

  /** BigWorld string: packed-int length + UTF-8 bytes (appendString). */
  writeString(value: string): void {
    const bytes = UTF8_ENCODER.encode(value);
    this.writePackedInt(bytes.byteLength);
    this.writeBlob(bytes);
  }

  /** Raw bytes with no length prefix (packet payloads). */
  writeBlob(bytes: Uint8Array): void {
    this.ensure(bytes.byteLength);
    this.buffer.set(bytes, this.length);
    this.length += bytes.byteLength;
  }

  /** 3 little-endian floats: x, y, z (Vector3 streaming). */
  writeVector3(x: number, y: number, z: number): void {
    this.writeFloat(x);
    this.writeFloat(y);
    this.writeFloat(z);
  }

  /** Patch a previously written 32-bit little-endian value in place. */
  patchUInt32(at: number, value: number): void {
    if (at < 0 || at + 4 > this.length) {
      throw new RangeError(`patch out of range: ${at}`);
    }
    this.view.setUint32(at, value >>> 0, true);
  }

  /** Append raw bytes from a source slice (used by the bundle writer). */
  writeRaw(source: Uint8Array, offset: number, count: number): void {
    this.ensure(count);
    this.buffer.set(source.subarray(offset, offset + count), this.length);
    this.length += count;
  }

  /** Reserve n zeroed bytes without writing; returns the offset. */
  reserve(count: number): number {
    const at = this.length;
    this.ensure(count);
    this.buffer.fill(0, this.length, this.length + count);
    this.length += count;
    return at;
  }
}
