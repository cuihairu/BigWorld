/**
 * Login / entity-creation state machine over a Mercury TCP channel. TypeScript
 * port of BigWorld.ClientSdk.Connection.ClientConnection
 * (client_sdk/unity/PROTOCOL.md §6).
 *
 * Sequence:
 *   1. TCP connect, then PacketSender emits the client header frame
 *      ("TCP\r\n\r\n") — TCPChannel::doSend.
 *   2. Client sends `authenticate` (uint32 session key) — client_interface.hpp.
 *   3. Server replies `onLoginCheckStatus`; a non-zero status means failure.
 *   4. Server pushes `createEntity` (entityId + entityTypeId + data);
 *      the connection becomes Connected.
 *   5. `loggedOff` (uint8 reason) terminates the session.
 */

import { BundleWriter, MessageRecord, parseBundle, payloadReader } from "./bundle.js";
import { BwReader } from "./bwReader.js";
import { BwWriter } from "./bwStream.js";
import { PacketReceiver, PacketSender } from "./framing.js";
import { MessageSpec, MessageSpecTable, REPLY_MESSAGE_IDENTIFIER } from "./messageSpec.js";
import type { ITransport } from "./transport.js";

/** Login / entity-creation state machine states. */
export enum ConnectionState {
  /** No active connection. */
  Disconnected = 0,
  /** TCP connect in flight. */
  Connecting = 1,
  /** Client header frame ("TCP\r\n\r\n") sent, awaiting server. */
  Handshaking = 2,
  /** authenticate/login sent, awaiting onLoginCheckStatus. */
  Authenticating = 3,
  /** Login accepted; awaiting player entity creation. */
  AwaitingEntities = 4,
  /** Player entity created; fully connected. */
  Connected = 5,
  /** Server sent loggedOff (or local logout). */
  LoggedOff = 6,
}

export interface LoginStatusEventArgs {
  ok: boolean;
  statusCode: number;
  payload: Uint8Array;
}

export interface EntityCreatedEventArgs {
  entityId: number;
  entityTypeId: number;
  payload: BwReader;
}

export interface MessageEventArgs {
  record: MessageRecord;
}

/**
 * Message ids used by the login / entity-creation state machine.
 *
 * The client→server ids mirror BigWorld's ClientInterface declaration order
 * (programming/bigworld/lib/connection/client_interface.hpp) — Mercury assigns
 * ids sequentially by declaration (interface_minder.cpp:38-49), so
 * `authenticate` (the first declared message) is id 0.
 *
 * The server→client ids (onLoginCheckStatus / createEntity) follow the kbengine
 * login flow the SDK targets; because the exact server-side interface depends
 * on the deployed server these live in one place so a host can substitute its
 * own table (client_sdk/README.md, PROTOCOL.md login rows).
 */
export class LoginMessageIds {
  static readonly Default = new LoginMessageIds();

  /** client → baseapp (ClientInterface, client_interface.hpp:33-46). */
  authenticate = new MessageSpec(0x00, "authenticate", "fixed", 4);

  /** server → client (kbengine login flow). */
  onLoginCheckStatus = new MessageSpec(0x21, "onLoginCheckStatus", "variable", 2);

  /** server → client (kbengine onCreatedProxies / onEntityEnterWorld shape). */
  createEntity = new MessageSpec(0x22, "createEntity", "variable", 2);

  /** client_interface.hpp:166-168 loggedOff carries a uint8 reason. */
  loggedOff = new MessageSpec(0x23, "loggedOff", "fixed", 1);

  /** Build the server→client table used for parsing inbound frames. */
  serverToClientTable(): MessageSpecTable {
    return MessageSpecTable.fromSpecs([
      MessageSpec.Reply,
      this.onLoginCheckStatus,
      this.createEntity,
      this.loggedOff,
    ]);
  }
}

export interface ClientConnectionCallbacks {
  onStateChanged?: (state: ConnectionState) => void;
  onLoginCheckStatus?: (args: LoginStatusEventArgs) => void;
  onEntityCreated?: (args: EntityCreatedEventArgs) => void;
  onUnhandledMessage?: (args: MessageEventArgs) => void;
  onClosed?: (reason: string) => void;
}

export class ClientConnection {
  private readonly transport: ITransport;
  private readonly sender: PacketSender;
  private readonly receiver: PacketReceiver;
  private readonly serverToClient: MessageSpecTable;
  private readonly ids: LoginMessageIds;
  private _state = ConnectionState.Disconnected;

  /** Fired on every state transition. */
  onStateChanged?: (state: ConnectionState) => void;
  /** Fired when the server answers authenticate. */
  onLoginCheckStatus?: (args: LoginStatusEventArgs) => void;
  /** Fired when the player entity is created (state → Connected). */
  onEntityCreated?: (args: EntityCreatedEventArgs) => void;
  /** Fired for inbound messages the login flow does not handle. */
  onUnhandledMessage?: (args: MessageEventArgs) => void;
  /** Fired once when the underlying transport ends. */
  onClosed?: (reason: string) => void;

  constructor(transport: ITransport, messageIds?: LoginMessageIds, callbacks?: ClientConnectionCallbacks) {
    this.transport = transport;
    this.ids = messageIds ?? LoginMessageIds.Default;
    if (callbacks !== undefined) {
      Object.assign(this, callbacks);
    }
    this.serverToClient = this.ids.serverToClientTable();
    this.sender = new PacketSender((data) => this.transport.send(data));
    this.receiver = new PacketReceiver();

    this.transport.onConnected = () => {};
    this.transport.onData = (data) => this.onDataReceived(data);
    this.transport.onClosed = (reason) => this.onTransportClosed(reason);
  }

  get state(): ConnectionState {
    return this._state;
  }

  private setState(next: ConnectionState): void {
    if (this._state === next) {
      return;
    }
    this._state = next;
    this.onStateChanged?.(next);
  }

  /** Connect and run the login handshake. */
  async connect(host: string, port: number, sessionKey: number): Promise<void> {
    this.setState(ConnectionState.Connecting);
    await this.transport.connect(host, port);
    this.setState(ConnectionState.Handshaking);

    // authenticate { uint32 key } — fixed-length message, id from
    // ClientInterface declaration order (client_interface.hpp:38-43).
    const bundle = BundleWriter.buildSingle(this.ids.authenticate, (writer) => {
      writer.writeUInt32(sessionKey >>> 0);
    });
    this.sender.sendBundle(bundle);
    this.setState(ConnectionState.Authenticating);
  }

  /** Send an arbitrary message to the server (post-login RPCs). */
  sendMessage(spec: MessageSpec, writePayload: (writer: BwWriter) => void): void {
    const bundle = BundleWriter.buildSingle(spec, writePayload);
    this.sender.sendBundle(bundle);
  }

  /** Dispose: unsubscribe and drop the transport. */
  dispose(): void {
    this.transport.onConnected = undefined;
    this.transport.onData = undefined;
    this.transport.onClosed = undefined;
    this.transport.dispose();
  }

  private onDataReceived(data: Uint8Array): void {
    this.receiver.append(data);
    for (;;) {
      const frame = this.receiver.tryTakeFrame();
      if (frame === undefined) {
        break;
      }
      this.dispatchFrame(frame);
    }
  }

  private dispatchFrame(frame: Uint8Array): void {
    const records = parseBundle(frame, 0, frame.byteLength, this.serverToClient);
    for (const record of records) {
      this.dispatchRecord(record);
    }
  }

  private dispatchRecord(record: MessageRecord): void {
    switch (record.messageId) {
      case REPLY_MESSAGE_IDENTIFIER:
        // Replies are not part of the login flow; surface as unhandled.
        this.onUnhandledMessage?.({ record });
        return;

      case this.ids.onLoginCheckStatus.id: {
        const reader = payloadReader(record);
        const status = reader.readInt32();
        const ok = status === 0;
        // Transition first so a subscriber observes the new state.
        this.setState(ok ? ConnectionState.AwaitingEntities : ConnectionState.LoggedOff);
        this.onLoginCheckStatus?.({
          ok,
          statusCode: status,
          payload: reader.remaining > 0 ? reader.readBytesRaw(reader.remaining) : new Uint8Array(0),
        });
        return;
      }

      case this.ids.createEntity.id: {
        const reader = payloadReader(record);
        const entityId = reader.readInt32();
        const entityTypeId = reader.readUInt16();
        this.setState(ConnectionState.Connected);
        this.onEntityCreated?.({ entityId, entityTypeId, payload: reader });
        return;
      }

      case this.ids.loggedOff.id: {
        this.setState(ConnectionState.LoggedOff);
        return;
      }

      default:
        this.onUnhandledMessage?.({ record });
        return;
    }
  }

  private onTransportClosed(reason: string): void {
    if (this._state !== ConnectionState.LoggedOff) {
      this.setState(ConnectionState.Disconnected);
    }
    this.onClosed?.(reason);
  }
}
