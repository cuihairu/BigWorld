/**
 * BigWorld network byte-order reader (little-endian). TypeScript mirror of
 * BigWorld.ClientSdk.Streams.BwReader (client_sdk/unity/PROTOCOL.md §1).
 * Throws on underrun so
 * the packet receiver can detect truncated frames.
 */

const UTF8_DECODER = new TextDecoder("utf-8");

export class BwReader {
  private readonly buffer: Uint8Array;
  private readonly view: DataView;
  private offset: number;
  private readonly end: number;

  constructor(buffer: Uint8Array, offset = 0, length = buffer.byteLength - offset) {
    if (offset < 0 || length < 0 || offset + length > buffer.byteLength) {
      throw new RangeError("BwReader window out of range");
    }
    this.buffer = buffer;
    this.view = new DataView(buffer.buffer, buffer.byteOffset, buffer.byteLength);
    this.offset = offset;
    this.end = offset + length;
  }

  get remaining(): number {
    return this.end - this.offset;
  }

  get position(): number {
    return this.offset;
  }

  private require(count: number): void {
    if (this.offset + count > this.end) {
      throw new Error(`Underrun: need ${count} bytes, ${this.remaining} remaining`);
    }
  }

  readUInt8(): number {
    this.require(1);
    return this.view.getUint8(this.offset++);
  }

  readInt8(): number {
    this.require(1);
    return this.view.getInt8(this.offset++);
  }

  readBool(): boolean {
    return this.readUInt8() !== 0;
  }

  readUInt16(): number {
    this.require(2);
    const v = this.view.getUint16(this.offset, true);
    this.offset += 2;
    return v;
  }

  readInt16(): number {
    this.require(2);
    const v = this.view.getInt16(this.offset, true);
    this.offset += 2;
    return v;
  }

  readUInt32(): number {
    this.require(4);
    const v = this.view.getUint32(this.offset, true);
    this.offset += 4;
    return v;
  }

  readInt32(): number {
    this.require(4);
    const v = this.view.getInt32(this.offset, true);
    this.offset += 4;
    return v;
  }

  readUInt64(): bigint {
    this.require(8);
    const v = this.view.getBigUint64(this.offset, true);
    this.offset += 8;
    return v;
  }

  readInt64(): bigint {
    this.require(8);
    const v = this.view.getBigInt64(this.offset, true);
    this.offset += 8;
    return v;
  }

  readFloat(): number {
    this.require(4);
    const v = this.view.getFloat32(this.offset, true);
    this.offset += 4;
    return v;
  }

  readDouble(): number {
    this.require(8);
    const v = this.view.getFloat64(this.offset, true);
    this.offset += 8;
    return v;
  }

  /** Packed int, exactly BinaryIStream::readPackedInt. */
  readPackedInt(): number {
    let value = this.readUInt8();
    if (value === 0xff) {
      value = this.readUInt8() | (this.readUInt8() << 8) | (this.readUInt8() << 16);
    }
    return value;
  }

  /** Packed-int length prefix + UTF-8 bytes. */
  readString(): string {
    const length = this.readPackedInt();
    const bytes = this.readBytesRaw(length);
    return UTF8_DECODER.decode(bytes);
  }

  /** Read count raw bytes and return them (consumes from the stream). */
  readBytesRaw(count: number): Uint8Array {
    this.require(count);
    const result = this.buffer.slice(this.offset, this.offset + count);
    this.offset += count;
    return result;
  }

  skip(count: number): void {
    this.require(count);
    this.offset += count;
  }

  /** A view of the underlying buffer starting at the current position. */
  subArray(length: number): Uint8Array {
    this.require(length);
    return this.buffer.subarray(this.offset, this.offset + length);
  }
}
