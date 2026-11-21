/**
 * @bigworld/client-sdk — BigWorld Mercury TCP client plugin SDK (TypeScript).
 *
 * Wire authority: programming/bigworld/lib/network (client_sdk/unity/PROTOCOL.md
 * maps every row to a source line).
 */

// streams
export { BwWriter } from "./bwStream.js";
export { BwReader } from "./bwReader.js";

// message table
export {
  MessageSpec,
  MessageSpecTable,
  REPLY_MESSAGE_IDENTIFIER,
  BUNDLE_FLAG_HAS_REQUESTS,
  BUNDLE_FLAG_MASK,
  LAST_REQUEST_OFFSET,
} from "./messageSpec.js";
export type { MessageLengthStyle } from "./messageSpec.js";

// bundle codec
export { BundleWriter, parseBundle, payloadReader } from "./bundle.js";
export type { MessageRecord } from "./bundle.js";

// framing
export {
  PacketReceiver,
  PacketSender,
  encodeFramePrefix,
  tryDecodeFramePrefix,
  framePrefixBytes,
  CLIENT_HEADER,
  CLIENT_HEADER_FRAME_LENGTH,
  LARGE_FORM_MARKER,
  SMALL_FORM_MAX,
  DEFAULT_MAX_FRAME_LENGTH,
} from "./framing.js";
export type { FramePrefix } from "./framing.js";

// events (kbengine-aligned)
export { EventBus, Event } from "./event.js";

// entities
export { Entity, EntityCall, Vector3, EventTypes } from "./entity.js";
export { EntityCallType } from "./entity.js";
export type { EntityCallOptions, EntityCallHandle } from "./entity.js";

// transport
export { TcpTransport } from "./transport.js";
export type { ITransport } from "./transport.js";

// connection
export {
  ClientConnection,
  ConnectionState,
  LoginMessageIds,
} from "./connection.js";
export type {
  ClientConnectionCallbacks,
  EntityCreatedEventArgs,
  LoginStatusEventArgs,
  MessageEventArgs,
} from "./connection.js";
