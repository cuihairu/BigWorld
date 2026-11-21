/**
 * JSON-RPC over stdio with LSP Content-Length framing.
 *
 * The bw-lsp engine speaks this protocol, and it is the only channel between
 * the extension and the engine's C++ code. There is no third-party client
 * library here on purpose: the framing is eight lines of ASCII, and owning it
 * keeps the extension dependency-free at runtime and the protocol exactly the
 * one the engine's own unit tests pin. Everything in this file is pure
 * TypeScript so the vitest suite can exercise it without a child process.
 */

/** The shape every reply from the engine takes. */
export interface JsonRpcMessage {
	jsonrpc: string;
	id?: number | string | null;
	method?: string;
	params?: unknown;
	result?: unknown;
	error?: { code: number; message: string };
}

/**
 * Wraps one JSON-RPC message body in the Content-Length frame the engine
 * reads. The header is CRLF, the length is the byte length of the UTF-8 body.
 */
export function encodeFrame(body: string): Buffer {
	const payload = Buffer.from(body, "utf8");
	const header = Buffer.from(`Content-Length: ${payload.length}\r\n\r\n`, "ascii");
	return Buffer.concat([header, payload]);
}

/**
 * Incrementally decodes a byte stream into whole frame bodies.
 *
 * A single chunk may carry a partial frame, one frame, or several; headers and
 * bodies may arrive split across any byte boundary. It emits a frame body only
 * once the declared Content-Length bytes have all arrived, so the caller always
 * gets complete JSON.
 */
export class FrameParser {
	private buffer: Buffer = Buffer.alloc(0);
	private contentLength: number | null = null;

	/** Feeds a chunk and returns every complete body it closes out. */
	push(chunk: Buffer): string[] {
		this.buffer =
			this.buffer.length === 0 ? chunk : Buffer.concat([this.buffer, chunk]);

		const bodies: string[] = [];

		for (;;) {
			if (this.contentLength === null) {
				const headerEnd = this.buffer.indexOf("\r\n\r\n");
				if (headerEnd === -1) {
					// No complete header yet; wait for more bytes.
					break;
				}

				const header = this.buffer.subarray(0, headerEnd).toString("ascii");
				const match = /content-length:\s*(\d+)/i.exec(header);
				this.buffer = this.buffer.subarray(headerEnd + 4);

				if (match) {
					this.contentLength = Number(match[1]);
				} else {
					// A frame with no Content-Length is not a frame the engine
					// would have written; drop the header and keep going.
					continue;
				}
			}

			if (this.buffer.length < this.contentLength) {
				// Body not complete yet.
				break;
			}

			bodies.push(
				this.buffer.subarray(0, this.contentLength).toString("utf8")
			);
			this.buffer = this.buffer.subarray(this.contentLength);
			this.contentLength = null;
		}

		return bodies;
	}
}

/** The minimal writable side of the engine's stdio. */
export interface WritableLike {
	write(chunk: Buffer): unknown;
}

/** The minimal readable side of the engine's stdio. */
export interface ReadableLike {
	on(event: "data", listener: (chunk: Buffer) => void): unknown;
}

/**
 * A JSON-RPC client over one engine pipe pair. Requests are matched to
 * replies by id; the engine never pushes, so there is nothing else to route.
 */
export class JsonRpcClient {
	private readonly parser = new FrameParser();
	private nextId = 1;
	private closed = false;
	private readonly pending = new Map<
		number,
		{ resolve: (value: unknown) => void; reject: (error: Error) => void }
	>();

	constructor(
		private readonly writable: WritableLike,
		readable: ReadableLike
	) {
		readable.on("data", (chunk: Buffer) => this.feed(chunk));
	}

	private feed(chunk: Buffer): void {
		for (const body of this.parser.push(chunk)) {
			this.handleBody(body);
		}
	}

	private handleBody(body: string): void {
		let message: JsonRpcMessage;
		try {
			message = JSON.parse(body) as JsonRpcMessage;
		} catch {
			// Not JSON the engine wrote; ignore rather than crash the client.
			return;
		}

		const id = message.id;
		if (typeof id !== "number") {
			return;
		}

		const entry = this.pending.get(id);
		if (!entry) {
			return;
		}
		this.pending.delete(id);

		if (message.error) {
			entry.reject(new Error(message.error.message || "jsonrpc error"));
		} else {
			entry.resolve(message.result);
		}
	}

	/** Sends a request and resolves with its result. Rejects on engine error. */
	request(method: string, params?: unknown): Promise<unknown> {
		if (this.closed) {
			return Promise.reject(new Error("rpc client closed"));
		}

		const id = this.nextId++;
		const message: JsonRpcMessage = { jsonrpc: "2.0", id, method };
		if (params !== undefined) {
			message.params = params;
		}

		return new Promise((resolve, reject) => {
			this.pending.set(id, { resolve, reject });
			this.writable.write(encodeFrame(JSON.stringify(message)));
		});
	}

	/** Sends a notification: no id, no reply expected. */
	notify(method: string, params?: unknown): void {
		if (this.closed) {
			return;
		}
		const message: JsonRpcMessage = { jsonrpc: "2.0", method };
		if (params !== undefined) {
			message.params = params;
		}
		this.writable.write(encodeFrame(JSON.stringify(message)));
	}

	/** Rejects anything still in flight. Safe to call more than once. */
	close(): void {
		if (this.closed) {
			return;
		}
		this.closed = true;
		for (const [, entry] of this.pending) {
			entry.reject(new Error("rpc client closed"));
		}
		this.pending.clear();
	}
}
