/**
 * Message spec and interface table. TypeScript mirror of
 * BigWorld.ClientSdk.Protocol.MessageSpec / MessageSpecTable
 * (client_sdk/unity/PROTOCOL.md §1, §5).
 */

export type MessageLengthStyle = "fixed" | "variable";

export const REPLY_MESSAGE_IDENTIFIER = 0xff;
export const BUNDLE_FLAG_HAS_REQUESTS = 0x01;
export const BUNDLE_FLAG_MASK = 0x01;
export const LAST_REQUEST_OFFSET = 0xffffffff;

export class MessageSpec {
  readonly id: number;
  readonly name: string;
  readonly style: MessageLengthStyle;
  /** For fixed messages: the payload size. For variable: the width of the
   * length field (0..4 bytes, little-endian). */
  readonly lengthParam: number;

  constructor(id: number, name: string, style: MessageLengthStyle, lengthParam: number) {
    if (style === "variable" && (lengthParam < 0 || lengthParam > 4)) {
      throw new RangeError("variable length width must be 0..4");
    }
    if (style === "fixed" && lengthParam < 0) {
      throw new RangeError("fixed payload size must be non-negative");
    }
    this.id = id;
    this.name = name;
    this.style = style;
    this.lengthParam = lengthParam;
  }

  get isReply(): boolean {
    return this.id === REPLY_MESSAGE_IDENTIFIER;
  }

  /** Mercury::InterfaceElement::REPLY (interface_element.cpp:24-26). */
  static readonly Reply = new MessageSpec(
    REPLY_MESSAGE_IDENTIFIER,
    "Reply",
    "variable",
    4
  );

  /**
   * Maximum payload length representable in the length field
   * (InterfaceElement::canHandleLength, interface_element.hpp:127-135).
   */
  get maxPayloadLength(): number {
    if (this.style === "fixed") {
      return Number.MAX_SAFE_INTEGER;
    }
    return this.lengthParam >= 4
      ? Number.MAX_SAFE_INTEGER
      : (1 << (8 * this.lengthParam)) - 1;
  }

  /** True iff payloadSize fits in this message's length field. */
  canHandleLength(payloadSize: number): boolean {
    if (this.style !== "variable") {
      return true;
    }
    return this.lengthParam >= 4 || payloadSize < this.maxPayloadLength;
  }
}

/** Immutable lookup table from message id to spec. */
export class MessageSpecTable {
  private readonly byId = new Array<MessageSpec | undefined>(256);

  private constructor() {}

  static fromSpecs(specs: Iterable<MessageSpec>): MessageSpecTable {
    const table = new MessageSpecTable();
    for (const spec of specs) {
      if (table.byId[spec.id] !== undefined) {
        throw new Error(`duplicate message id ${spec.id}`);
      }
      table.byId[spec.id] = spec;
    }
    return table;
  }

  get(id: number): MessageSpec | undefined {
    return this.byId[id];
  }

  has(id: number): boolean {
    return this.byId[id] !== undefined;
  }

  /** Message ids assigned by declaration order (interface_minder.cpp:38-49). */
  static idByDeclarationOrder(declarationIndex: number): number {
    if (declarationIndex < 0 || declarationIndex > 254) {
      throw new RangeError("interface has at most 255 message ids");
    }
    return declarationIndex;
  }

  *all(): Iterable<MessageSpec> {
    for (const spec of this.byId) {
      if (spec !== undefined) {
        yield spec;
      }
    }
  }
}
