import { describe, expect, it } from "vitest";
import { BwReader, BwWriter, MessageSpec } from "../src/index.js";
import { EntityCallType, type EntityCallOptions } from "../src/entity.js";
import { AccountBase } from "../src/generated/AccountBase.js";
import {
  ClientAvatarBase,
  EntityCallClientAvatarBase,
} from "../src/generated/ClientAvatarBase.js";

/**
 * CL-3 runtime smoke for the generated entity bases (batch one): the generated
 * Account / ClientAvatar classes compile against the CL-2 runtime and drive
 * the accepted paths:
 *
 *   * initial-snapshot decode (createEntity payload -> readClientData),
 *   * server-to-client method dispatch (invokeClientMethod -> onXxx hook),
 *   * client-to-server exposed method call (sendXxx -> outgoing bundle bytes).
 *
 * The last asserts the exact wire tail: [entityId: int32 LE][exposed method
 * id: uint8], as the server's Entity.onRemoteMethodCall expects.
 */

class TestAccount extends AccountBase {
  lastMessage?: string;
  override onChatMessage(value0: string): void {
    this.lastMessage = value0;
  }
}

describe("generated AccountBase", () => {
  it("readClientData decodes playerName", () => {
    const writer = new BwWriter();
    writer.writeString("Alice");
    const account = new TestAccount();
    account.readClientData(new BwReader(writer.toArray()));
    expect(account.playerName).toBe("Alice");
  });

  it("invokeClientMethod dispatches known ids", () => {
    const writer = new BwWriter();
    writer.writeString("hi");
    const account = new TestAccount();
    account.invokeClientMethod(AccountBase.methodIdChatMessage,
      new BwReader(writer.toArray()));
    expect(account.lastMessage).toBe("hi");
  });

  it("invokeClientMethod ignores unknown ids", () => {
    const account = new TestAccount();
    expect(() =>
      account.invokeClientMethod(99, new BwReader(new Uint8Array())),
    ).not.toThrow();
  });
});

describe("generated ClientAvatarBase", () => {
  it("sendLogOff writes entityId then exposed id 0", () => {
    const spec = new MessageSpec(0x41, "Entity_onRemoteMethodCall", "variable", 1);
    const body = captureCallBundle(spec, 1000, (call) =>
      call.sendLogOff());
    expect(body).toEqual([0xe8, 0x03, 0x00, 0x00, 0x00]);
  });

  it("exposes the server-ordered id constants", () => {
    expect(ClientAvatarBase.propertyIdProp3).toBe(0);
    expect(ClientAvatarBase.propertyIdProp2).toBe(1);
    expect(ClientAvatarBase.propertyIdProp1).toBe(2);
    expect(ClientAvatarBase.propertyIdPlayerName).toBe(3);
    expect(ClientAvatarBase.propertyIdProp5).toBe(4);
    expect(ClientAvatarBase.methodIdCellTick).toBe(1);
    expect(ClientAvatarBase.methodIdFromCell).toBe(2);
  });
});

function captureCallBundle(
  spec: MessageSpec, entityId: number,
  act: (call: EntityCallClientAvatarBase) => void,
): number[] {
  const captured: number[] = [];
  const options: EntityCallOptions = {
    cellMessage: spec,
    baseMessage: spec,
    send: (payload: Uint8Array) => captured.push(...payload),
  };
  const call = new EntityCallClientAvatarBase(options);
  call.id = entityId;
  call.type = EntityCallType.Base;
  act(call);
  // Bundle framing: [flags][id][len] then the body
  // [entityId: int32 LE][exposed id: uint8][args...].
  return captured.slice(3);
}
