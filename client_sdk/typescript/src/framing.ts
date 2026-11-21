/**
 * Mercury TCP frame codec. TypeScript port of
 * BigWorld.ClientSdk.Protocol.TcpFraming / PacketReceiver / PacketSender
 * (client_sdk/unity/PROTOCOL.md §2).
 *
 * Source authority:
 *   - Write: TCPChannel::doSend (programming/bigworld/lib/network/tcp_channel.cpp:404-432)
 *   - Read: TCPChannel::readSmallFrameLength / readLargeFrameLength (:361-394)
 *   - Client header: CLIENT_HEADER "TCP\r\n\r\n" (tcp_channel.cpp:29)
 *
 * Layout:
 *   [uint16 LE n]          — n == 0xFFFF means the large form follows
 *   [uint32 LE length]     — large form
 *   [payload...]
 *
 * The client's first send carries the header frame
 * ([uint16 7]["TCP\r\n\r\n"], tcp_channel.cpp:406-411) before the bundle frame.
 */

/** 0xFFFF small-length marker for the large (uint32) form. */
export const LARGE_FORM_MARKER = 0xffff;

/** Maximum payload size expressible in the small form. */
export const SMALL_FORM_MAX = 0xffff - 1;

/** Default sanity cap on a single frame. */
export const DEFAULT_MAX_FRAME_LENGTH = 64 * 1024 * 1024;

import { BwWriter } from "./bwStream.js";

/** Client header "TCP\r\n\r\n" (tcp_channel.cpp:29). */
export const CLIENT_HEADER: Uint8Array = new Uint8Array([
  0x54, 0x43, 0x50, 0x0d, 0x0a, 0x0d, 0x0a,
]);

/** Size of the header frame (uint16 length + 7-byte payload). */
export const CLIENT_HEADER_FRAME_LENGTH = 2 + CLIENT_HEADER.byteLength;

/** Encode the length prefix for a payload of payloadLength bytes. */
export function encodeFramePrefix(payloadLength: number): Uint8Array {
  if (payloadLength < 0) {
    throw new RangeError(`negative frame payload ${payloadLength}`);
  }
  const writer = new Uint8Array(6);
  const view = new DataView(writer.buffer);
  if (payloadLength >= 0xffff) {
    view.setUint16(0, LARGE_FORM_MARKER, true);
    view.setUint32(2, payloadLength, true);
    return writer;
  }
  view.setUint16(0, payloadLength, true);
  return writer.slice(0, 2);
}

export interface FramePrefix {
  length: number;
  /** Bytes the prefix occupies. */
  prefixBytes: number;
}

/**
 * Try to decode a length prefix from the buffer head. Returns undefined when
 * more bytes are needed.
 */
export function tryDecodeFramePrefix(
  data: Uint8Array,
  offset: number,
  length: number
): FramePrefix | undefined {
  if (length < 2) {
    return undefined;
  }
  const view = new DataView(data.buffer, data.byteOffset, data.byteLength);
  const small = view.getUint16(offset, true);
  if (small !== LARGE_FORM_MARKER) {
    return { length: small, prefixBytes: 2 };
  }
  if (length < 6) {
    return undefined;
  }
  return { length: view.getUint32(offset + 2, true), prefixBytes: 6 };
}

/** Bytes a prefix occupies for a payload of payloadLength. */
export function framePrefixBytes(payloadLength: number): number {
  return payloadLength >= 0xffff ? 6 : 2;
}

/**
 * Incremental TCP frame reassembler. Handles sticky packets (many frames in
 * one read) and partial packets (a frame split across reads), mirroring
 * TCPChannel::handleInput / cleanUpAfterFrameReceive (tcp_channel.cpp:130-256).
 */
export class PacketReceiver {
  private buffer: Uint8Array;
  private start = 0;
  private end = 0;

  readonly maxFrameLength: number;

  constructor(maxFrameLength = DEFAULT_MAX_FRAME_LENGTH) {
    this.maxFrameLength = maxFrameLength;
    this.buffer = new Uint8Array(4096);
  }

  /** Feed bytes read from the wire. */
  append(data: Uint8Array): void {
    this.ensureCapacity(data.byteLength);
    this.buffer.set(data, this.end);
    this.end += data.byteLength;
  }

  /** Extract the next complete frame, or undefined when more data is needed. */
  tryTakeFrame(): Uint8Array | undefined {
    const remaining = this.end - this.start;
    if (remaining === 0) {
      return undefined;
    }

    const prefix = tryDecodeFramePrefix(this.buffer, this.start, remaining);
    if (prefix === undefined) {
      return undefined;
    }

    if (prefix.length > this.maxFrameLength) {
      throw new Error(`frame length ${prefix.length} exceeds cap ${this.maxFrameLength}`);
    }

    if (remaining < prefix.prefixBytes + prefix.length) {
      return undefined;
    }

    const frame = this.buffer.slice(this.start + prefix.prefixBytes, this.start + prefix.prefixBytes + prefix.length);
    this.start += prefix.prefixBytes + prefix.length;
    this.compact();
    return frame;
  }

  /** Number of buffered but not-yet-framed bytes. */
  get pendingBytes(): number {
    return this.end - this.start;
  }

  reset(): void {
    this.start = 0;
    this.end = 0;
  }

  private ensureCapacity(extra: number): void {
    if (this.end + extra <= this.buffer.byteLength) {
      return;
    }
    // Compact consumed bytes first.
    if (this.start > 0 && this.end + extra <= this.buffer.byteLength + this.start) {
      this.buffer.copyWithin(0, this.start, this.end);
      this.end -= this.start;
      this.start = 0;
      return;
    }
    const newSize = Math.max(this.buffer.byteLength * 2, this.end + extra);
    const next = new Uint8Array(newSize);
    next.set(this.buffer.subarray(this.start, this.end));
    this.buffer = next;
    this.end -= this.start;
    this.start = 0;
  }

  private compact(): void {
    // Reclaim consumed space once it dominates the buffer.
    if (this.start > 4096 && this.start * 2 > this.end) {
      this.buffer.copyWithin(0, this.start, this.end);
      this.end -= this.start;
      this.start = 0;
    }
  }
}

/**
 * Writes Mercury TCP frames over a sink. On the first send it emits the client
 * header frame before the bundle frame, exactly as TCPChannel::doSend does
 * (tcp_channel.cpp:404-432).
 */
export class PacketSender {
  private readonly write: (data: Uint8Array) => void;
  private headerSent = false;

  constructor(write: (data: Uint8Array) => void) {
    this.write = write;
  }

  get isHeaderSent(): boolean {
    return this.headerSent;
  }

  /** Send a bundle payload as one frame (header frame first on the first call). */
  sendBundle(bundlePayload: Uint8Array): void {
    const writer = new BwWriter(bundlePayload.byteLength + 16);
    if (!this.headerSent) {
      this.headerSent = true;
      writer.writeUInt16(CLIENT_HEADER.byteLength);
      writer.writeBlob(CLIENT_HEADER);
    }

    const prefix = encodeFramePrefix(bundlePayload.byteLength);
    writer.writeBlob(prefix);
    writer.writeBlob(bundlePayload);

    this.write(writer.toArray());
  }
}
