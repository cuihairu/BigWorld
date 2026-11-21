/**
 * Client entity bases. TypeScript port of the KBEngine.Entity /
 * KBEngine.EntityCall classes from kbengine's JS client plugin
 * (kbe/res/sdk_templates/client/js/kbengine.js).
 *
 * The kbengine plugin resolves the remote-call message ids from the client
 * interface it imports at boot (Baseapp_onRemoteCallCellMethodFromClient /
 * Entity_onRemoteMethodCall). BigWorld's SDK is transport- and message-table
 * driven, so the message spec for a cell/base call is supplied by the host via
 * {@link EntityCallOptions}; the wire behaviour (entity id prefix, one call per
 * bundle) is unchanged.
 */

import { BundleWriter } from "./bundle.js";
import { BwWriter } from "./bwStream.js";
import { MessageSpec } from "./messageSpec.js";

/** kbengine: KBEngine.ENTITYCALL_TYPE_CELL = 0, _BASE = 1. */
export enum EntityCallType {
  Cell = 0,
  Base = 1,
}

export class Vector3 {
  x: number;
  y: number;
  z: number;

  constructor(x = 0, y = 0, z = 0) {
    this.x = x;
    this.y = y;
    this.z = z;
  }

  distance(pos: Vector3): number {
    const dx = pos.x - this.x;
    const dy = pos.y - this.y;
    const dz = pos.z - this.z;
    return Math.sqrt(dx * dx + dy * dy + dz * dz);
  }

  add(v: Vector3): this {
    this.x += v.x;
    this.y += v.y;
    this.z += v.z;
    return this;
  }

  sub(v: Vector3): this {
    this.x -= v.x;
    this.y -= v.y;
    this.z -= v.z;
    return this;
  }

  mul(num: number): this {
    this.x *= num;
    this.y *= num;
    this.z *= num;
    return this;
  }
}

/** Outgoing message specs used by an {@link EntityCall}. */
export interface EntityCallOptions {
  /** Message emitted for a cell call (kbengine: Baseapp_onRemoteCallCellMethodFromClient). */
  cellMessage: MessageSpec;
  /** Message emitted for a base call (kbengine: Entity_onRemoteMethodCall). */
  baseMessage: MessageSpec;
  /** Write the encoded bundle to the wire. */
  send: (bundlePayload: Uint8Array) => void;
}

/** An in-progress entity call (kbengine: Bundle returned by newCall). */
export interface EntityCallHandle {
  /** Payload writer; append the method id / arguments here. */
  readonly writer: BwWriter;
  /** Encode the bundle (must be called before sendCall, or sendCall calls it). */
  finish(): Uint8Array;
}

export class EntityCall {
  id = 0;
  className = "";
  type = EntityCallType.Cell;

  private readonly options: EntityCallOptions;
  private pending: EntityCallHandle | undefined;

  constructor(options: EntityCallOptions) {
    this.options = options;
  }

  get isBase(): boolean {
    return this.type === EntityCallType.Base;
  }

  get isCell(): boolean {
    return this.type === EntityCallType.Cell;
  }

  /** Begin a call: write the entity id prefix, return the payload writer. */
  newCall(): EntityCallHandle {
    if (this.pending !== undefined) {
      throw new Error("EntityCall: previous call not sent (call sendCall first)");
    }

    const bundle = new BundleWriter();
    const spec = this.isBase ? this.options.baseMessage : this.options.cellMessage;
    const writer = bundle.startMessage(spec);
    // The message body is [entityId: int32][methodId: uint8][args...], so the
    // body length fed to endMessage must include the entity id prefix (the
    // server's Entity.onRemoteMethodCall reads it from the body).
    const bodyStart = writer.size;
    writer.writeInt32(this.id);

    const handle: EntityCallHandle = {
      writer,
      finish: () => {
        bundle.endMessage(writer.size - bodyStart);
        return bundle.build();
      },
    };
    this.pending = handle;
    return handle;
  }

  /** Finish and send the pending (or supplied) call. */
  sendCall(handle?: EntityCallHandle): void {
    const call = handle ?? this.pending;
    if (call === undefined) {
      throw new Error("EntityCall: no call to send (call newCall first)");
    }
    const payload = call.finish();
    this.options.send(payload);
    if (this.pending === call) {
      this.pending = undefined;
    }
  }
}

/**
 * Base client entity (kbengine: KBEngine.Entity). Subclass per generated entity
 * type; the generator (CL-3) emits `onEnterWorld`/`onLeaveWorld`/`__init` and
 * property setters that call into the protocol layer.
 */
export class Entity {
  id = 0;
  className = "";
  position = new Vector3();
  direction = new Vector3();
  velocity = 0;

  cell: EntityCall | null = null;
  base: EntityCall | null = null;

  /** True after enterWorld (kbengine: inWorld). */
  inWorld = false;
  /** True after __init__ ran (kbengine: inited). */
  inited = false;
  isControlled = false;

  entityLastLocalPos = new Vector3();
  entityLastLocalDir = new Vector3();
  isOnGround = false;

  /** Mirrors the server entity script __init__; override in subclasses. */
  __init__(): void {}

  onDestroy(): void {}

  onControlled(controlled: boolean): void {}

  isPlayer(): boolean {
    return false;
  }
}

/**
 * Well-known client event names, mirroring KBEngine.EventTypes (kbengine.js).
 * Subset used by the login / entity-creation flow; the plugin fires these via
 * {@link Event} so a host can register handlers without hard-coding strings.
 */
export const EventTypes = {
  createAccount: "createAccount",
  onCreateAccountResult: "onCreateAccountResult",
  bindAccountEmail: "bindAccountEmail",
  onBindAccountEmail: "onBindAccountEmail",
  newPassword: "newPassword",
  onNewPassword: "onNewPassword",
  resetPassword: "resetPassword",
  onResetPassword: "onResetPassword",
  onKicked: "onKicked",
  onDisconnected: "onDisconnected",
  onConnectionState: "onConnectionState",
  login: "login",
  logout: "logout",
  reloginBaseapp: "reloginBaseapp",
  onVersionNotMatch: "onVersionNotMatch",
  onScriptVersionNotMatch: "onScriptVersionNotMatch",
  onLoginFailed: "onLoginFailed",
  onLoginBaseapp: "onLoginBaseapp",
  onLoginBaseappFailed: "onLoginBaseappFailed",
  onReloginBaseapp: "onReloginBaseapp",
  onReloginBaseappSuccessfully: "onReloginBaseappSuccessfully",
  onReloginBaseappFailed: "onReloginBaseappFailed",
  onEnterWorld: "onEnterWorld",
  onLeaveWorld: "onLeaveWorld",
  onEnterSpace: "onEnterSpace",
  onLeaveSpace: "onLeaveSpace",
  updatePosition: "updatePosition",
  addSpaceGeometryMapping: "addSpaceGeometryMapping",
  onSetSpaceData: "onSetSpaceData",
  onDelSpaceData: "onDelSpaceData",
  onControlled: "onControlled",
  onLoseControlledEntity: "onLoseControlledEntity",
  onStreamDataStarted: "onStreamDataStarted",
  onStreamDataRecv: "onStreamDataRecv",
  onStreamDataCompleted: "onStreamDataCompleted",
} as const;
