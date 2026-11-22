import { describe, expect, it } from "vitest";
import {
  BundleWriter,
  LoginMessageIds,
  MessageSpec,
  MessageSpecTable,
  parseBundle,
  payloadReader,
  REPLY_MESSAGE_IDENTIFIER,
} from "../src/index.js";

/** Bundle encode/decode round-trips against the Mercury TCP bundle layout
 * (tcp_bundle.cpp / tcp_bundle_processor.cpp). See PROTOCOL.md rows BND-*. */

const authenticate = new MessageSpec(0x00, "authenticate", "fixed", 4);
const var2 = new MessageSpec(0x21, "onLoginCheckStatus", "variable", 2);
const var4 = new MessageSpec(0x42, "wide", "variable", 4);

const testTable = MessageSpecTable.fromSpecs([authenticate, var2, var4, MessageSpec.Reply]);

function parse(bundle: Uint8Array) {
  return parseBundle(bundle, 0, bundle.byteLength, testTable);
}

function readUInt16(b: Uint8Array, at: number): number {
  return b[at] | (b[at + 1] << 8);
}

function readUInt32(b: Uint8Array, at: number): number {
  return (b[at] | (b[at + 1] << 8) | (b[at + 2] << 16) | (b[at + 3] << 24)) >>> 0;
}

function readInt32(b: Uint8Array, at: number): number {
  return readUInt32(b, at) | 0;
}

describe("BundleWriter/BundleReader", () => {
  // BND-1: a fixed-length message has no length field.
  it("fixed message encodes as flags id payload", () => {
    const bundle = BundleWriter.buildSingle(authenticate, (w) => w.writeUInt32(0xdeadbeef));
    expect(bundle).toEqual(new Uint8Array([0x00, 0x00, 0xef, 0xbe, 0xad, 0xde]));
  });

  it("fixed message round-trips", () => {
    const bundle = BundleWriter.buildSingle(authenticate, (w) => w.writeUInt32(0xcafebabe));
    const records = parse(bundle);
    expect(records).toHaveLength(1);
    const record = records[0];
    expect(record.messageId).toBe(0x00);
    expect(record.isRequest).toBe(false);
    expect(record.payloadLength).toBe(4);
    expect(payloadReader(record).readUInt32()).toBe(0xcafebabe);
  });

  // BND-2: a variable-length message carries a 2-byte LE length field.
  it("variable message encodes length field", () => {
    const bundle = BundleWriter.buildSingle(var2, (w) => w.writeInt32(0));
    // [flags 0][id 0x21][len 0x04 0x00][payload 00 00 00 00]
    expect(bundle).toEqual(new Uint8Array([0x00, 0x21, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00]));
  });

  it("variable message round-trips", () => {
    const bundle = BundleWriter.buildSingle(var2, (w) => w.writeString("hello"));
    const record = parse(bundle)[0];
    expect(record.messageId).toBe(0x21);
    expect(record.payloadLength).toBe(6); // packed len 1 + "hello" 5
    expect(payloadReader(record).readString()).toBe("hello");
  });

  // BND-3: a request carries replyId + nextRequestOffset after the length field.
  it("single request sets flags and first request offset", () => {
    const writer = new BundleWriter();
    const payload = writer.startRequest(var2, 7);
    payload.writeInt32(99);
    writer.endMessage(4);
    const bundle = writer.build();

    expect(bundle[0]).toBe(0x01); // flags: has requests
    expect(readUInt32(bundle, 1)).toBe(5); // firstRequestOffset = 1 + 4
    expect(bundle[5]).toBe(0x21); // id
    expect(readUInt16(bundle, 6)).toBe(4); // length field
    expect(readInt32(bundle, 8)).toBe(7); // replyId
    expect(readUInt32(bundle, 12)).toBe(0xffffffff); // next request sentinel
    expect(readInt32(bundle, 16)).toBe(99); // payload
  });

  // BND-4: two requests chain via nextRequestOffset; non-request payloads
  // must not be clobbered (regression: the offset patch used to land in the
  // body of a non-empty request).
  it("two requests chain offsets and preserve payloads", () => {
    const writer = new BundleWriter();

    const p1 = writer.startRequest(var2, 1);
    p1.writeInt32(0x11111111);
    writer.endMessage(4);

    const p2 = writer.startMessage(var2);
    p2.writeString("between");
    writer.endMessage(8); // packed len 1 + "between" 7

    const p3 = writer.startRequest(var2, 2);
    p3.writeInt32(0x22222222);
    writer.endMessage(4);

    const bundle = writer.build();

    expect(bundle[0]).toBe(0x01); // flags
    expect(readUInt32(bundle, 1)).toBe(5); // first request at offset 5
    expect(readUInt32(bundle, 12)).toBe(31); // first request's next → second request
    expect(readUInt32(bundle, 38)).toBe(0xffffffff); // second request's next sentinel

    const records = parse(bundle);
    expect(records).toHaveLength(3);

    expect(records[0].isRequest).toBe(true);
    expect(records[0].replyId).toBe(1);
    expect(payloadReader(records[0]).readInt32()).toBe(0x11111111);

    expect(records[1].isRequest).toBe(false);
    expect(payloadReader(records[1]).readString()).toBe("between");

    expect(records[2].isRequest).toBe(true);
    expect(records[2].replyId).toBe(2);
    expect(payloadReader(records[2]).readInt32()).toBe(0x22222222);
  });

  // BND-5: oversize variable messages use all-0xFF length + uint32.
  it("oversize variable message uses all-ones plus uint32", () => {
    // var2 max inline length is 0xFFFF - 1 = 65534; 70000 must go oversize.
    const payload = new Uint8Array(70000);
    payload[0] = 0x5a;
    payload[69999] = 0xe5;

    const writer = new BundleWriter();
    const body = writer.startMessage(var2);
    body.writeBlob(payload);
    writer.endMessage(payload.byteLength);
    const bundle = writer.build();

    expect(bundle[1]).toBe(0x21);
    expect(bundle[2]).toBe(0xff);
    expect(bundle[3]).toBe(0xff);
    expect(readUInt32(bundle, 4)).toBe(70000);

    const record = parse(bundle)[0];
    expect(record.payloadLength).toBe(70000);
    const readBack = payloadReader(record).readBytesRaw(70000);
    expect(readBack[0]).toBe(0x5a);
    expect(readBack[69999]).toBe(0xe5);
  });

  it("oversize still fits in width-4 without special form", () => {
    // Width-4 length fields are always inline (no uint32 shift):
    // [flags][id][len uint32][payload].
    const bundle = BundleWriter.buildSingle(var4, (w) => w.writeUInt32(0x11223344));
    expect(bundle[1]).toBe(0x42);
    expect([...bundle.slice(2, 6)]).toEqual([0x04, 0x00, 0x00, 0x00]);
    expect([...bundle.slice(6, 10)]).toEqual([0x44, 0x33, 0x22, 0x11]);
  });

  // BND-6: error paths mirror REASON_CORRUPTED_PACKET / NONEXISTENT_ENTRY.
  it("parse rejects unknown flags", () => {
    const bundle = new Uint8Array([0x02]);
    expect(() => parse(bundle)).toThrow();
  });

  it("parse rejects unknown message id", () => {
    const bundle = new Uint8Array([0x00, 0x40, 0x00, 0x00]);
    expect(() => parse(bundle)).toThrow(/Unhandled message id 64/);
  });

  it("parse rejects truncated payload", () => {
    // id 0x21 (var2) with length 4 but only 2 payload bytes.
    const bundle = new Uint8Array([0x00, 0x21, 0x04, 0x00, 0x01, 0x02]);
    expect(() => parse(bundle)).toThrow(/Truncated payload/);
  });

  it("parse rejects out-of-bounds first request offset", () => {
    const bundle = new Uint8Array([
      0x01, 0xff, 0xff, 0xff, 0x00, 0x21, 0x04, 0x00, 0x01, 0x02, 0x03, 0x04,
    ]);
    expect(() => parse(bundle)).toThrow(/Out-of-bounds/);
  });

  // BND-7: REPLY (id 0xFF, variable, 4) is a valid inbound spec.
  it("reply spec is recognised", () => {
    expect(MessageSpec.Reply.isReply).toBe(true);
    expect(MessageSpec.Reply.style).toBe("variable");
    expect(MessageSpec.Reply.lengthParam).toBe(4);
    expect(REPLY_MESSAGE_IDENTIFIER).toBe(0xff);

    const bundle = BundleWriter.buildSingle(MessageSpec.Reply, (w) => w.writeInt32(42));
    expect(bundle[1]).toBe(0xff);

    const records = parseBundle(bundle, 0, bundle.byteLength, LoginMessageIds.Default.serverToClientTable());
    expect(records[0].messageId).toBe(0xff);
  });

  // BND-8: MessageSpec.canHandleLength / maxPayloadLength semantics
  // (interface_element.hpp:127-135).
  it("canHandleLength matches interface element", () => {
    const width2 = new MessageSpec(0x10, "v2", "variable", 2);
    expect(width2.maxPayloadLength).toBe(0xffff);
    expect(width2.canHandleLength(0xfffe)).toBe(true);
    expect(width2.canHandleLength(0xffff)).toBe(false);

    const width4 = new MessageSpec(0x11, "v4", "variable", 4);
    expect(width4.maxPayloadLength).toBe(Number.MAX_SAFE_INTEGER);
    expect(width4.canHandleLength(1_000_000)).toBe(true);

    expect(authenticate.maxPayloadLength).toBe(Number.MAX_SAFE_INTEGER);
    expect(authenticate.canHandleLength(4)).toBe(true);
  });

  it("message spec validates length param", () => {
    expect(() => new MessageSpec(1, "x", "variable", 5)).toThrow(RangeError);
    expect(() => new MessageSpec(1, "x", "variable", -1)).toThrow(RangeError);
    expect(() => new MessageSpec(1, "x", "fixed", -1)).toThrow(RangeError);
  });

  it("message spec table rejects duplicate ids", () => {
    expect(() =>
      MessageSpecTable.fromSpecs([
        new MessageSpec(1, "a", "fixed", 0),
        new MessageSpec(1, "b", "fixed", 0),
      ])
    ).toThrow(/duplicate message id 1/);
  });

  it("message spec table resolves by id", () => {
    const table = MessageSpecTable.fromSpecs([authenticate, var2]);
    expect(table.get(0x00)).toBe(authenticate);
    expect(table.get(0x21)).toBe(var2);
    expect(table.get(0x40)).toBeUndefined();
  });
});
