/**
 * Byte-stream transport abstraction. TypeScript port of
 * BigWorld.ClientSdk.Transport.ITransport / TcpTransport
 * (client_sdk/unity/PROTOCOL.md §2).
 *
 * The protocol core is written against this interface so it can be embedded in a
 * browser, a node host, or a mock server. The TCP-over-WebSocket gateway is
 * deliberately out of scope for this batch (see client_sdk/README.md).
 */

import * as net from "node:net";

/**
 * Transport callbacks + API. Callback fields are assigned by the connection
 * (or a host) before use; a transport must not assume exactly one subscriber.
 */
export interface ITransport {
  readonly isConnected: boolean;
  /** Fired once the connection is established. */
  onConnected?: () => void;
  /** Fired with raw bytes read from the wire (no framing). */
  onData?: (data: Uint8Array) => void;
  /** Fired exactly once when the connection ends. */
  onClosed?: (reason: string) => void;
  /** Open the connection. Resolves when connected. */
  connect(host: string, port: number): Promise<void>;
  /** Write raw bytes to the wire. */
  send(data: Uint8Array): void;
  /** Close the connection. Safe to call multiple times. */
  close(reason?: string): void;
  dispose(): void;
}

/** TCP stream transport; re-raises raw chunks, framing is done by PacketReceiver. */
export class TcpTransport implements ITransport {
  onConnected?: () => void;
  onData?: (data: Uint8Array) => void;
  onClosed?: (reason: string) => void;

  private socket: net.Socket | undefined;
  private closed = false;

  /** True while the transport is usable. */
  get isConnected(): boolean {
    return this.socket !== undefined && !this.closed && !this.socket.destroyed;
  }

  /** Open the connection. Resolves when connected, rejects on failure. */
  connect(host: string, port: number): Promise<void> {
    const socket = net.connect({ host, port });
    this.socket = socket;

    return new Promise<void>((resolve, reject) => {
      const onError = (err: Error): void => {
        socket.removeListener("connect", onConnect);
        reject(err);
      };
      const onConnect = (): void => {
        socket.removeListener("error", onError);
        resolve();
      };
      socket.once("connect", onConnect);
      socket.once("error", onError);
    })
      .then(() => {
        socket.setNoDelay(true);
        socket.on("data", (chunk: Buffer) => {
          this.onData?.(new Uint8Array(chunk));
        });
        socket.on("end", () => {
          this.close("peer closed");
        });
        socket.on("error", () => {
          this.close("stream error");
        });
        this.onConnected?.();
      });
  }

  send(data: Uint8Array): void {
    const socket = this.socket;
    if (socket === undefined || this.closed || socket.destroyed) {
      throw new Error("transport is not connected");
    }
    socket.write(data);
  }

  /** Close the connection. Safe to call multiple times. */
  close(reason = "connection closed"): void {
    if (this.closed) {
      return;
    }
    this.closed = true;
    try {
      this.socket?.destroy();
    } catch {
      // Already destroyed.
    }
    this.socket = undefined;
    this.onClosed?.(reason);
  }

  dispose(): void {
    this.close("disposed");
  }
}
