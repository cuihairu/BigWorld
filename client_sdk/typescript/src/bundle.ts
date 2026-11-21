/**
 * Mercury TCP bundle encode/decode. TypeScript port of
 * BigWorld.ClientSdk.Protocol.BundleWriter / BundleReader
 * (client_sdk/unity/PROTOCOL.md §3, §4).
 *
 * Layout:
 *   [flags: uint8][firstRequestOffset: uint32?][<message records>]
 *   record: [id: uint8][length field][replyId: int32?][nextRequestOffset: uint32?][payload]
 *
 * Length fields are little-endian; oversized variable messages use the
 * all-ones + uint32 form (interface_element.cpp:424-431, :565-583).
 */

import { BwWriter } from "./bwStream.js";
import { BwReader } from "./bwReader.js";
import {
  BUNDLE_FLAG_HAS_REQUESTS,
  BUNDLE_FLAG_MASK,
  LAST_REQUEST_OFFSET,
  MessageSpec,
  MessageSpecTable,
} from "./messageSpec.js";

export interface MessageRecord {
  messageId: number;
  spec: MessageSpec;
  payloadOffset: number;
  payloadLength: number;
  isRequest: boolean;
  replyId: number;
  bundle: Uint8Array;
}

interface PendingMessage {
  spec: MessageSpec;
  isRequest: boolean;
  id: number;
  segmentEnd: number; // end offset in _messages (exclusive)
  bodyStart: number; // offset in _messages of first body byte
  replyIdOffset: number; // offset in _messages of [replyId] (request only)
  bodyLength: number;
}

export class BundleWriter {
  private readonly messages = new BwWriter();
  private readonly pending: PendingMessage[] = [];
  private current: PendingMessage | undefined;

  hasRequests = false;

  get messageCount(): number {
    return this.pending.length;
  }

  reset(): void {
    this.messages.reset();
    this.pending.length = 0;
    this.current = undefined;
    this.hasRequests = false;
  }

  /** Begin a non-request message; returns the payload writer. */
  startMessage(spec: MessageSpec): BwWriter {
    return this.beginMessage(spec, false, 0);
  }

  /** Begin a request message; replyId is written verbatim. */
  startRequest(spec: MessageSpec, replyId: number): BwWriter {
    return this.beginMessage(spec, true, replyId);
  }

  private beginMessage(spec: MessageSpec, isRequest: boolean, replyId: number): BwWriter {
    if (this.current !== undefined) {
      throw new Error("previous message not terminated (call endMessage)");
    }

    const pending: PendingMessage = {
      spec,
      isRequest,
      id: spec.id,
      segmentEnd: 0,
      bodyStart: 0,
      replyIdOffset: 0,
      bodyLength: 0,
    };
    this.current = pending;

    this.messages.writeUInt8(spec.id);

    if (spec.style === "variable") {
      this.messages.reserve(spec.lengthParam);
    }

    if (isRequest) {
      pending.replyIdOffset = this.messages.size;
      this.messages.writeInt32(replyId);
      this.messages.writeUInt32(LAST_REQUEST_OFFSET);
      this.hasRequests = true;
    }

    pending.bodyStart = this.messages.size;
    return this.messages;
  }

  /** Terminate the current message; bodyLength is bytes written since start. */
  endMessage(bodyLength: number): void {
    const current = this.current;
    if (current === undefined) {
      throw new Error("no message in progress");
    }

    current.bodyLength = bodyLength;
    current.segmentEnd = this.messages.size;

    if (this.messages.size - current.bodyStart !== bodyLength) {
      throw new Error("body length does not match bytes written");
    }

    this.pending.push(current);
    this.current = undefined;
  }

  /** Encode the complete bundle payload. */
  build(): Uint8Array {
    if (this.current !== undefined) {
      throw new Error("message in progress at build()");
    }

    const hasRequests = this.hasRequests;
    const baseOffset = hasRequests ? 1 + 4 : 1;
    const messageBytes = this.messages.toArray();

    const output = new BwWriter(baseOffset + messageBytes.byteLength + 16);
    output.writeUInt8(hasRequests ? BUNDLE_FLAG_HAS_REQUESTS : 0);
    let firstRequestOffset = -1;
    if (hasRequests) {
      output.reserve(4);
    }

    let prevRequestReplyId: number | undefined;
    for (const pm of this.pending) {
      const wireStart = output.size;

      output.writeUInt8(pm.id);

      if (pm.spec.style === "variable") {
        const width = pm.spec.lengthParam;
        if (width >= 4 || pm.bodyLength < (1 << (8 * width)) - 1) {
          for (let i = 0; i < width; i++) {
            output.writeUInt8((pm.bodyLength >> (8 * i)) & 0xff);
          }
        } else {
          for (let i = 0; i < width; i++) {
            output.writeUInt8(0xff);
          }
          output.writeUInt32(pm.bodyLength);
        }
      }

      // Copy everything after the length field.
      const copyStart = pm.isRequest ? pm.replyIdOffset : pm.bodyStart;
      output.writeRaw(messageBytes, copyStart, pm.segmentEnd - copyStart);

      if (pm.isRequest) {
        if (firstRequestOffset === -1) {
          firstRequestOffset = wireStart;
        } else if (prevRequestReplyId !== undefined) {
          output.patchUInt32(prevRequestReplyId, wireStart);
        }

        const width = pm.spec.style === "variable" ? pm.spec.lengthParam : 0;
        prevRequestReplyId = wireStart + 1 + width + 4;
      }
    }

    if (hasRequests) {
      output.patchUInt32(1, firstRequestOffset >>> 0);
    }

    return output.toArray();
  }

  /** Convenience: build a single-message bundle. */
  static buildSingle(
    spec: MessageSpec,
    writePayload: (w: BwWriter) => void
  ): Uint8Array {
    const writer = new BundleWriter();
    const payload = writer.startMessage(spec);
    const start = payload.size;
    writePayload(payload);
    writer.endMessage(payload.size - start);
    return writer.build();
  }
}

export function parseBundle(
  bundle: Uint8Array,
  offset: number,
  length: number,
  table: MessageSpecTable
): MessageRecord[] {
  const reader = new BwReader(bundle, offset, length);
  const bundleStart = reader.position;
  const records: MessageRecord[] = [];

  const flags = reader.readUInt8();
  if ((flags & ~BUNDLE_FLAG_MASK) !== 0) {
    throw new Error(`Invalid bundle flags 0x${flags.toString(16)}`);
  }

  let nextRequestOffset = LAST_REQUEST_OFFSET;
  if ((flags & BUNDLE_FLAG_HAS_REQUESTS) !== 0) {
    nextRequestOffset = reader.readUInt32();
    if ((nextRequestOffset | 0) > length) {
      throw new Error(`Out-of-bounds first request offset ${nextRequestOffset} > ${length}`);
    }
  }

  while (reader.remaining > 0) {
    const msgStart = reader.position - bundleStart;
    const messageId = reader.readUInt8();
    const spec = table.get(messageId);
    if (spec === undefined) {
      throw new Error(`Unhandled message id ${messageId}`);
    }

    let payloadLength: number;
    if (spec.style === "variable") {
      switch (spec.lengthParam) {
        case 0:
          payloadLength = 0;
          break;
        case 1:
          payloadLength = reader.readUInt8();
          break;
        case 2:
          payloadLength = reader.readUInt16();
          break;
        case 3:
          payloadLength =
            reader.readUInt8() | (reader.readUInt8() << 8) | (reader.readUInt8() << 16);
          break;
        case 4:
          payloadLength = reader.readInt32();
          break;
        default:
          throw new Error(`Invalid length parameter ${spec.lengthParam} for ${spec.name}`);
      }

      if (payloadLength < 0 || !spec.canHandleLength(payloadLength)) {
        payloadLength = reader.readUInt32();
      }
    } else {
      payloadLength = spec.lengthParam;
    }

    const isRequest = msgStart === nextRequestOffset;
    let replyId = 0;
    if (isRequest) {
      replyId = reader.readInt32();
      nextRequestOffset = reader.readUInt32();
      if ((nextRequestOffset | 0) > length) {
        throw new Error(`Out-of-bounds next request offset ${nextRequestOffset} > ${length}`);
      }
    }

    if (reader.remaining < payloadLength) {
      throw new Error(
        `Truncated payload for message id ${messageId}: need ${payloadLength}, have ${reader.remaining}`
      );
    }

    records.push({
      messageId,
      spec,
      payloadOffset: reader.position,
      payloadLength,
      isRequest,
      replyId,
      bundle,
    });

    reader.skip(payloadLength);
  }

  return records;
}

/** Read the payload of a record as a fresh BwReader. */
export function payloadReader(record: MessageRecord): BwReader {
  return new BwReader(record.bundle, record.payloadOffset, record.payloadLength);
}
