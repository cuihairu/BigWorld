import { describe, expect, it } from "vitest";
import {
  CLIENT_HEADER,
  PacketReceiver,
  PacketSender,
  encodeFramePrefix,
  tryDecodeFramePrefix,
} from "../src/index.js";

/** TCP frame prefix + incremental reassembler tests. Wire facts against
 * tcp_channel.cpp doSend / readSmallFrameLength / readLargeFrameLength
 * (PROTOCOL.md rows FRM-*). */

function buildFrame(payload: Uint8Array): Uint8Array {
  const prefix = encodeFramePrefix(payload.byteLength);
  const frame = new Uint8Array(prefix.byteLength + payload.byteLength);
  frame.set(prefix, 0);
  frame.set(payload, prefix.byteLength);
  return frame;
}

describe("TcpFraming", () => {
  // FRM-1: small form is a 2-byte LE length prefix.
  it("small frame prefix is uint16 LE", () => {
    expect([...encodeFramePrefix(5)]).toEqual([0x05, 0x00]);
  });

  // FRM-2: 0xFFFF triggers the large (uint32) form.
  it("large frame prefix is marker plus uint32", () => {
    expect([...encodeFramePrefix(0xffff)]).toEqual([0xff, 0xff, 0xff, 0xff, 0x00, 0x00]);
    // 70000 = 0x11170, uint32 LE = 70 11 01 00.
    expect([...encodeFramePrefix(70000)]).toEqual([0xff, 0xff, 0x70, 0x11, 0x01, 0x00]);
  });

  it("decodes a small prefix", () => {
    const prefix = tryDecodeFramePrefix(new Uint8Array([0x05, 0x00]), 0, 2);
    expect(prefix).toEqual({ length: 5, prefixBytes: 2 });
  });

  it("decodes a large prefix", () => {
    const data = new Uint8Array([0xff, 0xff, 0x10, 0x00, 0x00, 0x00, 0xaa]);
    const prefix = tryDecodeFramePrefix(data, 0, data.byteLength);
    expect(prefix).toEqual({ length: 0x10, prefixBytes: 6 });
  });

  it("requires prefix bytes", () => {
    expect(tryDecodeFramePrefix(new Uint8Array([0x05]), 0, 1)).toBeUndefined();
    // Marker present but uint32 incomplete.
    expect(tryDecodeFramePrefix(new Uint8Array([0xff, 0xff, 0x10]), 0, 3)).toBeUndefined();
  });

  // FRM-3: client header is a 7-byte "TCP\r\n\r\n" payload.
  it("client header is TCP CRLF CRLF", () => {
    expect([...CLIENT_HEADER]).toEqual([0x54, 0x43, 0x50, 0x0d, 0x0a, 0x0d, 0x0a]);
    expect(String.fromCharCode(...CLIENT_HEADER)).toBe("TCP\r\n\r\n");
  });

  // FRM-4: reassembly - several frames delivered in one TCP read.
  it("receiver splits sticky frames", () => {
    const receiver = new PacketReceiver();

    const frame1 = buildFrame(new Uint8Array([0xaa, 0xbb]));
    const frame2 = buildFrame(new Uint8Array([0xcc]));
    const chunk = new Uint8Array(frame1.byteLength + frame2.byteLength);
    chunk.set(frame1, 0);
    chunk.set(frame2, frame1.byteLength);

    receiver.append(chunk);

    const got1 = receiver.tryTakeFrame();
    expect(got1).not.toBeUndefined();
    expect([...got1!]).toEqual([0xaa, 0xbb]);

    const got2 = receiver.tryTakeFrame();
    expect([...got2!]).toEqual([0xcc]);

    expect(receiver.tryTakeFrame()).toBeUndefined();
    expect(receiver.pendingBytes).toBe(0);
  });

  // FRM-5: reassembly - one frame split across several TCP reads.
  it("receiver reassembles partial frames", () => {
    const receiver = new PacketReceiver();
    const frame = buildFrame(new Uint8Array([0x01, 0x02, 0x03, 0x04, 0x05]));

    // Feed byte-at-a-time; the frame must only appear once complete.
    for (let i = 0; i < frame.byteLength - 1; i++) {
      receiver.append(frame.slice(i, i + 1));
      expect(receiver.tryTakeFrame()).toBeUndefined();
    }

    // The final byte completes the frame.
    receiver.append(frame.slice(frame.byteLength - 1));
    const got = receiver.tryTakeFrame();
    expect([...got!]).toEqual([0x01, 0x02, 0x03, 0x04, 0x05]);
  });

  // FRM-6: large frames flow through the reassembler unchanged.
  it("receiver handles a large frame", () => {
    const payload = new Uint8Array(70000);
    payload[0] = 0x77;
    const frame = buildFrame(payload);

    const receiver = new PacketReceiver();
    receiver.append(frame);

    const got = receiver.tryTakeFrame();
    expect(got).not.toBeUndefined();
    expect(got!.byteLength).toBe(70000);
    expect(got![0]).toBe(0x77);
  });

  // FRM-7: an oversized single frame is rejected rather than buffered forever.
  it("receiver rejects over-cap frame", () => {
    const receiver = new PacketReceiver(16);
    receiver.append(buildFrame(new Uint8Array(32)));
    // The cap is enforced when the prefix is decoded.
    expect(() => receiver.tryTakeFrame()).toThrow(/exceeds cap/);
  });

  // FRM-8: consumed space is reclaimed so long streams don't grow the buffer.
  it("receiver reclaims space across many frames", () => {
    const receiver = new PacketReceiver();
    // Enough frames to cross the 4096-byte compaction threshold.
    for (let i = 0; i < 2000; i++) {
      receiver.append(buildFrame(new Uint8Array([i & 0xff])));
      const got = receiver.tryTakeFrame();
      expect(got![0]).toBe(i & 0xff);
    }
    expect(receiver.pendingBytes).toBe(0);
  });

  it("packet sender prepends the client header frame on first send", () => {
    const sent: Uint8Array[] = [];
    const sender = new PacketSender((data) => sent.push(data));

    const payload = new Uint8Array([0xaa]);
    sender.sendBundle(payload);

    expect(sent).toHaveLength(1);
    const frame = sent[0];
    // [uint16 7]["TCP\r\n\r\n"][uint16 1][payload]
    expect(frame.byteLength).toBe(2 + 7 + 2 + 1);
    expect(frame[0]).toBe(0x07);
    expect(frame[1]).toBe(0x00);
    expect(String.fromCharCode(...frame.slice(2, 9))).toBe("TCP\r\n\r\n");
    expect(frame[9]).toBe(0x01);
    expect(frame[10]).toBe(0x00);
    expect(frame[11]).toBe(0xaa);
    expect(sender.isHeaderSent).toBe(true);
  });
});
