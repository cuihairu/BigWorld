import * as net from "node:net";
import { describe, expect, it } from "vitest";
import {
  ClientConnection,
  ConnectionState,
  EntityCreatedEventArgs,
  LoginStatusEventArgs,
  MessageEventArgs,
  encodeFramePrefix,
  payloadReader,
  TcpTransport,
} from "../src/index.js";

/** End-to-end login handshake against an in-process mock baseapp speaking the
 * Mercury wire format. Covers the CL-1 acceptance path: login → create →
 * onLoginCheckStatus → entity creation.
 *
 * The mock asserts the client's first two frames byte-for-byte (header frame
 * "TCP\r\n\r\n", then the authenticate bundle) so the test is also a
 * wire-format oracle, not just a round trip. */

const SESSION_KEY = 0x11223344;
const ENTITY_ID = 1000;
const ENTITY_TYPE_ID = 2;
const REPLY_ID_VALUE = 0xcafebabe | 0;

const ID_ON_LOGIN_CHECK_STATUS = 0x21;
const ID_CREATE_ENTITY = 0x22;
const ID_LOGGED_OFF = 0x23;

/** Buffered exact-read helper over a socket. */
class WireConn {
  private buf: Buffer = Buffer.alloc(0);
  private waiting: Array<{ count: number; resolve: (d: Buffer) => void; reject: (e: Error) => void }> = [];

  constructor(private readonly socket: net.Socket) {
    socket.on("data", (d: Buffer) => {
      this.buf = Buffer.concat([this.buf, d]);
      this.pump();
    });
    socket.on("error", (e: Error) => {
      for (const w of this.waiting) {
        w.reject(e);
      }
      this.waiting = [];
    });
    socket.on("close", () => {
      for (const w of this.waiting) {
        w.reject(new Error("socket closed while waiting for bytes"));
      }
      this.waiting = [];
    });
  }

  readExact(count: number): Promise<Buffer> {
    if (this.buf.length >= count) {
      const out = this.buf.subarray(0, count);
      this.buf = this.buf.subarray(count);
      return Promise.resolve(Buffer.from(out));
    }
    return new Promise((resolve, reject) => {
      this.waiting.push({ count, resolve, reject });
    });
  }

  drainUntilClosed(): Promise<void> {
    return new Promise((resolve) => {
      this.socket.on("close", () => resolve());
      // If the peer already closed, close fires promptly.
    });
  }

  private pump(): void {
    const still: typeof this.waiting = [];
    for (const w of this.waiting) {
      if (this.buf.length >= w.count) {
        const out = this.buf.subarray(0, w.count);
        this.buf = this.buf.subarray(w.count);
        w.resolve(Buffer.from(out));
      } else {
        still.push(w);
      }
    }
    this.waiting = still;
  }
}

class MockBaseApp {
  port = 0;
  readonly completion: Promise<void>;
  private readonly server: net.Server;
  private readonly ready: Promise<void>;

  private constructor(respond: (conn: WireConn, socket: net.Socket) => Promise<void>) {
    this.server = net.createServer();
    this.completion = new Promise<void>((resolve, reject) => {
      this.server.once("error", reject);
      this.server.on("connection", (socket) => {
        const conn = new WireConn(socket);
        respond(conn, socket)
          .then(() => resolve())
          .catch((e: unknown) => reject(e));
      });
    });
    this.ready = new Promise<void>((resolve, reject) => {
      this.server.once("error", reject);
      this.server.listen(0, "127.0.0.1", () => resolve());
    });
    this.port = 0;
  }

  /** Start the listener and resolve once bound (so `port` is valid). */
  static async create(
    respond: (conn: WireConn, socket: net.Socket) => Promise<void>
  ): Promise<MockBaseApp> {
    const app = new MockBaseApp(respond);
    await app.ready;
    app.port = (app.server.address() as net.AddressInfo).port;
    return app;
  }

  async dispose(): Promise<void> {
    this.server.close();
    // Let the pending connection resolve before tearing down.
    try {
      await Promise.race([this.completion, new Promise((r) => setTimeout(r, 2000))]);
    } catch {
      // Ignore — the test surfaces server faults via Race().
    }
  }
}

function writeFrame(socket: net.Socket, payload: Uint8Array): void {
  socket.write(encodeFramePrefix(payload.byteLength));
  socket.write(payload);
}

/** Read the client's first two frames and assert they are exactly the header
 * frame then the authenticate bundle. Returns the authenticate bundle. */
async function readHeaderAndAuthenticate(conn: WireConn): Promise<Uint8Array> {
  const headerLen = await conn.readExact(2);
  expect([...headerLen]).toEqual([0x07, 0x00]);
  const header = await conn.readExact(7);
  expect(header.toString("ascii")).toBe("TCP\r\n\r\n");

  const authLen = await conn.readExact(2);
  expect([...authLen]).toEqual([0x06, 0x00]);
  const auth = await conn.readExact(6);
  return new Uint8Array(auth);
}

function assertClientAuthenticate(auth: Uint8Array): void {
  expect(auth[0]).toBe(0x00); // flags: no requests
  expect(auth[1]).toBe(0x00); // authenticate id (first declared message)
  const key = (auth[2] | (auth[3] << 8) | (auth[4] << 16) | (auth[5] << 24)) >>> 0;
  expect(key).toBe(SESSION_KEY);
}

function onLoginCheckStatus(status: number): Uint8Array {
  // [flags 0][id 0x21][len 4][status int32 LE]
  return new Uint8Array([
    0x00, ID_ON_LOGIN_CHECK_STATUS, 0x04, 0x00,
    status & 0xff, (status >> 8) & 0xff, (status >> 16) & 0xff, (status >> 24) & 0xff,
  ]);
}

function createEntity(entityId: number, entityTypeId: number, extraInt32: number): Uint8Array {
  // [flags 0][id 0x22][len 10][entityId int32][entityTypeId uint16][extra int32]
  return new Uint8Array([
    0x00, ID_CREATE_ENTITY, 0x0a, 0x00,
    entityId & 0xff, (entityId >> 8) & 0xff, (entityId >> 16) & 0xff, (entityId >> 24) & 0xff,
    entityTypeId & 0xff, (entityTypeId >> 8) & 0xff,
    extraInt32 & 0xff, (extraInt32 >> 8) & 0xff, (extraInt32 >> 16) & 0xff, (extraInt32 >> 24) & 0xff,
  ]);
}

function loggedOff(reason: number): Uint8Array {
  // [flags 0][id 0x23][reason uint8]
  return new Uint8Array([0x00, ID_LOGGED_OFF, reason]);
}

function reply(replyId: number): Uint8Array {
  // [flags 0][id 0xFF][len 4 bytes][replyId int32 LE]
  return new Uint8Array([
    0x00, 0xff, 0x04, 0x00, 0x00, 0x00,
    replyId & 0xff, (replyId >> 8) & 0xff, (replyId >> 16) & 0xff, (replyId >> 24) & 0xff,
  ]);
}

/** Await the expected event, but surface the mock server's own failure if the
 * responder faults first (e.g. a wire-oracle assertion inside it). */
async function race<T>(expected: Promise<T>, server: MockBaseApp): Promise<T> {
  const event = expected.then((value) => ({ kind: "event" as const, value }));
  const serverDone = server.completion.then(() => ({ kind: "server" as const }));
  const timeout = new Promise<never>((_, reject) =>
    setTimeout(() => reject(new Error("timed out waiting for client event")), 5000)
  );

  const winner = await Promise.race([event, serverDone, timeout]);
  if (winner.kind !== "event") {
    await server.completion; // rethrows a server fault if any
    throw new Error("server finished without the expected client event");
  }
  return winner.value;
}

function withConnection(): { conn: ClientConnection; transport: TcpTransport } {
  const transport = new TcpTransport();
  const conn = new ClientConnection(transport);
  return { conn, transport };
}

describe("ClientConnection login handshake", () => {
  // LGN-1: happy path — status 0 then createEntity reaches Connected.
  it("login succeeds and creates an entity", async () => {
    const server = await MockBaseApp.create(async (conn, socket) => {
      assertClientAuthenticate(await readHeaderAndAuthenticate(conn));
      writeFrame(socket, onLoginCheckStatus(0));
      writeFrame(socket, createEntity(ENTITY_ID, ENTITY_TYPE_ID, 42));
      await conn.drainUntilClosed();
    });

    try {
      const { conn, transport } = withConnection();
      let created: EntityCreatedEventArgs | undefined;
      const createdPromise = new Promise<EntityCreatedEventArgs>((resolve) => {
        conn.onEntityCreated = (args) => {
          created = args;
          resolve(args);
        };
      });

      await conn.connect("127.0.0.1", server.port, SESSION_KEY);
      const args = await race(createdPromise, server);

      expect(args.entityId).toBe(ENTITY_ID);
      expect(args.entityTypeId).toBe(ENTITY_TYPE_ID);
      expect(args.payload.readInt32()).toBe(42);
      expect(conn.state).toBe(ConnectionState.Connected);
      transport.dispose();
    } finally {
      await server.dispose();
    }
  });

  // LGN-2: a non-zero login status fails the handshake.
  it("non-zero login status logs off", async () => {
    const server = await MockBaseApp.create(async (conn, socket) => {
      assertClientAuthenticate(await readHeaderAndAuthenticate(conn));
      writeFrame(socket, onLoginCheckStatus(1));
      await conn.drainUntilClosed();
    });

    try {
      const { conn, transport } = withConnection();
      let status: LoginStatusEventArgs | undefined;
      const statusPromise = new Promise<LoginStatusEventArgs>((resolve) => {
        conn.onLoginCheckStatus = (args) => {
          status = args;
          resolve(args);
        };
      });

      await conn.connect("127.0.0.1", server.port, SESSION_KEY);
      const got = await race(statusPromise, server);

      expect(got.ok).toBe(false);
      expect(got.statusCode).toBe(1);
      expect(status!.payload.byteLength).toBe(0);
      expect(conn.state).toBe(ConnectionState.LoggedOff);
      transport.dispose();
    } finally {
      await server.dispose();
    }
  });

  // LGN-3: loggedOff after a successful login terminates the session.
  it("loggedOff after login terminates", async () => {
    const server = await MockBaseApp.create(async (conn, socket) => {
      assertClientAuthenticate(await readHeaderAndAuthenticate(conn));
      writeFrame(socket, onLoginCheckStatus(0));
      writeFrame(socket, loggedOff(0));
      await conn.drainUntilClosed();
    });

    try {
      const { conn, transport } = withConnection();
      const loggedOffPromise = new Promise<ConnectionState>((resolve) => {
        conn.onStateChanged = (state) => {
          if (state === ConnectionState.LoggedOff) {
            resolve(state);
          }
        };
      });

      await conn.connect("127.0.0.1", server.port, SESSION_KEY);
      await race(loggedOffPromise, server);

      expect(conn.state).toBe(ConnectionState.LoggedOff);
      transport.dispose();
    } finally {
      await server.dispose();
    }
  });

  // LGN-4: a reply (id 0xFF) is not part of the login flow and surfaces as an
  // unhandled inbound message.
  it("reply surfaces as unhandled", async () => {
    const server = await MockBaseApp.create(async (conn, socket) => {
      assertClientAuthenticate(await readHeaderAndAuthenticate(conn));
      writeFrame(socket, onLoginCheckStatus(0));
      writeFrame(socket, reply(REPLY_ID_VALUE));
      await conn.drainUntilClosed();
    });

    try {
      const { conn, transport } = withConnection();
      const unhandledPromise = new Promise<MessageEventArgs>((resolve) => {
        conn.onUnhandledMessage = (args) => resolve(args);
      });

      await conn.connect("127.0.0.1", server.port, SESSION_KEY);
      const args = await race(unhandledPromise, server);

      expect(args.record.messageId).toBe(0xff);
      expect(payloadReader(args.record).readInt32()).toBe(REPLY_ID_VALUE);
      transport.dispose();
    } finally {
      await server.dispose();
    }
  });
});
