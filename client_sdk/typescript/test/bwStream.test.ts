import { describe, expect, it } from "vitest";
import { BwReader, BwWriter } from "../src/index.js";

/** Byte-order / packed-int / string codec tests. Mirrors BwStreamTests.cs
 * (client_sdk/unity/PROTOCOL.md rows STM-*). */

function roundTrip(write: (w: BwWriter) => void, verify: (r: BwReader) => void): void {
  const writer = new BwWriter();
  write(writer);
  const reader = new BwReader(writer.toArray());
  verify(reader);
  expect(reader.remaining).toBe(0);
}

describe("BwWriter/BwReader", () => {
  // STM-1: all multi-byte scalars are little-endian.
  it("uint16 is little-endian", () => {
    const w = new BwWriter();
    w.writeUInt16(0x1234);
    expect(w.toArray()).toEqual(new Uint8Array([0x34, 0x12]));
  });

  it("uint32 is little-endian", () => {
    const w = new BwWriter();
    w.writeUInt32(0x12345678);
    expect(w.toArray()).toEqual(new Uint8Array([0x78, 0x56, 0x34, 0x12]));
  });

  it("uint64 is little-endian", () => {
    const w = new BwWriter();
    w.writeUInt64(0x0102030405060708n);
    expect(w.toArray()).toEqual(new Uint8Array([0x08, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01]));
  });

  it("int32 minus one is all ones", () => {
    const w = new BwWriter();
    w.writeInt32(-1);
    expect(w.toArray()).toEqual(new Uint8Array([0xff, 0xff, 0xff, 0xff]));
  });

  it("int8 sign-extends on round trip", () => {
    const w = new BwWriter();
    w.writeInt8(-1);
    expect(w.toArray()).toEqual(new Uint8Array([0xff]));
    const r = new BwReader(w.toArray());
    expect(r.readInt8()).toBe(-1);
  });

  // STM-2: floats/doubles stream as raw IEEE-754 in little-endian order.
  it("float is little-endian IEEE-754", () => {
    const w = new BwWriter();
    w.writeFloat(1.5);
    // 1.5 = 0x3FC00000
    expect(w.toArray()).toEqual(new Uint8Array([0x00, 0x00, 0xc0, 0x3f]));
  });

  it("float round-trips", () => {
    roundTrip((w) => w.writeFloat(-273.25), (r) => expect(r.readFloat()).toBeCloseTo(-273.25, 5));
  });

  it("double round-trips", () => {
    roundTrip((w) => w.writeDouble(3.14159265358979), (r) => expect(r.readDouble()).toBeCloseTo(3.14159265358979, 10));
  });

  // STM-3: packed int, binary_stream.hpp:254-300.
  const packedCases: Array<[number, number[]]> = [
    [0, [0x00]],
    [1, [0x01]],
    [254, [0xfe]],
    [255, [0xff, 0xff, 0x00, 0x00]],
    [256, [0xff, 0x00, 0x01, 0x00]],
    [0x010203, [0xff, 0x03, 0x02, 0x01]],
    [0xffffff, [0xff, 0xff, 0xff, 0xff]],
  ];

  it.each(packedCases)("packed int encodes %d", (value, expected) => {
    const w = new BwWriter();
    w.writePackedInt(value);
    expect(w.toArray()).toEqual(new Uint8Array(expected));
  });

  it.each([0, 1, 254, 255, 4096, 0xffffff])("packed int round-trips %d", (value) => {
    const w = new BwWriter();
    w.writePackedInt(value);
    const r = new BwReader(w.toArray());
    expect(r.readPackedInt()).toBe(value);
    expect(r.remaining).toBe(0);
  });

  it("packed int rejects negative", () => {
    const w = new BwWriter();
    expect(() => w.writePackedInt(-1)).toThrow(RangeError);
  });

  it("packed int rejects 2^24", () => {
    const w = new BwWriter();
    expect(() => w.writePackedInt(1 << 24)).toThrow(RangeError);
  });

  // STM-4: string = packed-int length + UTF-8 bytes (appendString).
  it("string encodes as packed length plus UTF-8", () => {
    const w = new BwWriter();
    w.writeString("hello");
    expect(w.toArray()).toEqual(new Uint8Array([0x05, 0x68, 0x65, 0x6c, 0x6c, 0x6f]));
  });

  it("string multibyte UTF-8 uses byte length", () => {
    const w = new BwWriter();
    w.writeString("你好");
    expect(w.size).toBe(7); // 1 packed + 6 UTF-8 bytes
    expect(w.toArray()[0]).toBe(6);
    const r = new BwReader(w.toArray());
    expect(r.readString()).toBe("你好");
  });

  it("string empty writes a single zero byte", () => {
    const w = new BwWriter();
    w.writeString("");
    expect(w.toArray()).toEqual(new Uint8Array([0x00]));
  });

  it("string long uses three-byte packed length", () => {
    const value = "a".repeat(300);
    const w = new BwWriter();
    w.writeString(value);
    expect([...w.toArray().slice(0, 4)]).toEqual([0xff, 300 & 0xff, 0x01, 0x00]);
    const r = new BwReader(w.toArray());
    expect(r.readString()).toBe(value);
  });

  // STM-5: Vector3 = 3 little-endian floats x,y,z.
  it("vector3 is twelve bytes and round-trips", () => {
    const w = new BwWriter();
    w.writeVector3(1.25, -2.5, 1000);
    expect(w.size).toBe(12);
    const r = new BwReader(w.toArray());
    expect(r.readFloat()).toBeCloseTo(1.25, 5);
    expect(r.readFloat()).toBeCloseTo(-2.5, 5);
    expect(r.readFloat()).toBeCloseTo(1000, 3);
    expect(r.remaining).toBe(0);
  });

  it("bool writes a single byte", () => {
    const w = new BwWriter();
    w.writeBool(true);
    w.writeBool(false);
    expect(w.toArray()).toEqual(new Uint8Array([0x01, 0x00]));
  });

  it("blob writes raw bytes without prefix", () => {
    const w = new BwWriter();
    w.writeBlob(new Uint8Array([0x0a, 0x0b, 0x0c]));
    expect(w.toArray()).toEqual(new Uint8Array([0x0a, 0x0b, 0x0c]));
  });

  // STM-6: patching in place (used for the bundle request-offset chain).
  it("patchUInt32 overwrites in place", () => {
    const w = new BwWriter();
    w.writeUInt8(0xaa);
    w.writeUInt32(0);
    w.writeUInt8(0xbb);
    w.patchUInt32(1, 0xcafebabe);
    expect(w.toArray()).toEqual(new Uint8Array([0xaa, 0xbe, 0xba, 0xfe, 0xca, 0xbb]));
  });

  it("patchUInt32 out of range throws", () => {
    const w = new BwWriter();
    w.writeUInt32(1);
    expect(() => w.patchUInt32(2, 1)).toThrow(RangeError);
  });

  // STM-7: reader bounds behaviour.
  it("reader underrun throws", () => {
    const r = new BwReader(new Uint8Array([0x01, 0x02]));
    expect(() => r.readUInt32()).toThrow();
  });

  it("reader tracks position and remaining", () => {
    const r = new BwReader(new Uint8Array([0x01, 0x02, 0x03, 0x04]));
    r.readUInt16();
    expect(r.position).toBe(2);
    expect(r.remaining).toBe(2);
    r.skip(2);
    expect(r.position).toBe(4);
    expect(r.remaining).toBe(0);
  });

  it("reader rejects out-of-range window", () => {
    const buffer = new Uint8Array(4);
    expect(() => new BwReader(buffer, 2, 3)).toThrow(RangeError);
    expect(() => new BwReader(buffer, -1, 2)).toThrow(RangeError);
  });

  it("readBytesRaw consumes", () => {
    const r = new BwReader(new Uint8Array([0x01, 0x02, 0x03, 0x04]));
    const head = r.readBytesRaw(2);
    expect(head).toEqual(new Uint8Array([0x01, 0x02]));
    expect(r.remaining).toBe(2);
  });

  it("writer grows beyond initial capacity", () => {
    const w = new BwWriter(4);
    for (let i = 0; i < 1000; i++) {
      w.writeUInt32(i);
    }
    expect(w.size).toBe(4000);
    const r = new BwReader(w.toArray());
    for (let i = 0; i < 1000; i++) {
      expect(r.readUInt32()).toBe(i);
    }
  });

  it("reset allows reuse", () => {
    const w = new BwWriter();
    w.writeUInt32(0xdeadbeef);
    w.reset();
    w.writeUInt16(0x00ff);
    expect(w.toArray()).toEqual(new Uint8Array([0xff, 0x00]));
  });
});
