/**
 * Pins the wire format: Content-Length framing and the request/reply pairing.
 * These are the only bytes the extension and the engine share, so they are
 * tested byte-for-byte and chunk-for-chunk, without a child process.
 */

import { describe, expect, it } from "vitest";

import {
	encodeFrame,
	FrameParser,
	JsonRpcClient,
	type ReadableLike,
	type WritableLike,
} from "./rpc";

describe("encodeFrame", () => {
	it("prefixes the body with its UTF-8 byte length", () => {
		const body = JSON.stringify({ jsonrpc: "2.0", id: 1, method: "initialize" });
		const frame = encodeFrame(body);
		const text = frame.toString("utf8");

		expect(text.startsWith(`Content-Length: ${Buffer.byteLength(body)}\r\n\r\n`)).toBe(true);
		expect(text.endsWith(body)).toBe(true);
	});

	it("measures the body in bytes, not characters", () => {
		const body = '{"digest":"18D00DC6é"}'; // a non-ASCII byte
		const frame = encodeFrame(body);
		const text = frame.toString("utf8");

		expect(text).toContain(`Content-Length: ${Buffer.byteLength(body)}`);
	});
});

describe("FrameParser", () => {
	it("decodes one complete frame", () => {
		const parser = new FrameParser();
		expect(parser.push(encodeFrame('{"a":1}'))).toEqual(['{"a":1}']);
	});

	it("holds a partial body until the declared length arrives", () => {
		const parser = new FrameParser();
		const frame = encodeFrame('{"hello":"world"}');
		const mid = Math.floor(frame.length / 2);

		expect(parser.push(frame.subarray(0, mid))).toEqual([]);
		expect(parser.push(frame.subarray(mid))).toEqual(['{"hello":"world"}']);
	});

	it("emits several frames carried by one chunk", () => {
		const parser = new FrameParser();
		const chunk = Buffer.concat([encodeFrame('{"n":1}'), encodeFrame('{"n":2}')]);
		expect(parser.push(chunk)).toEqual(['{"n":1}', '{"n":2}']);
	});

	it("accepts the header split across chunk boundaries", () => {
		const parser = new FrameParser();
		const frame = encodeFrame('{"x":1}');
		expect(parser.push(frame.subarray(0, 5))).toEqual([]);
		expect(parser.push(frame.subarray(5))).toEqual(['{"x":1}']);
	});

	it("drops a frame with no Content-Length instead of stalling", () => {
		const parser = new FrameParser();
		const stray = Buffer.from("not a frame at all\r\n\r\nhello", "utf8");
		expect(parser.push(stray)).toEqual([]);
	});
});

/** A fake duplex pair the client drives, so tests can reply at will. */
function fakeDuplex(): {
	written: Buffer[];
	writable: WritableLike;
	readable: ReadableLike;
	deliver: (body: string) => void;
} {
	const written: Buffer[] = [];
	let listener: ((chunk: Buffer) => void) | null = null;

	const writable: WritableLike = {
		write(chunk: Buffer) {
			written.push(Buffer.from(chunk));
			return true;
		},
	};
	const readable: ReadableLike = {
		on(_event: string, l: (chunk: Buffer) => void) {
			listener = l;
			return null;
		},
	};

	return {
		written,
		writable,
		readable,
		deliver(body: string) {
			listener?.(encodeFrame(body));
		},
	};
}

/** The JSON body inside the last frame the client wrote. */
function lastFrameBody(written: Buffer[]): Record<string, unknown> {
	const text = written[written.length - 1].toString("utf8");
	return JSON.parse(text.slice(text.indexOf("\r\n\r\n") + 4)) as Record<string, unknown>;
}

describe("JsonRpcClient", () => {
	it("routes a reply to the request that asked for it", async () => {
		const duplex = fakeDuplex();
		const client = new JsonRpcClient(duplex.writable, duplex.readable);

		const promise = client.request("bw/schemaDigest", {});
		const sent = lastFrameBody(duplex.written);
		duplex.deliver(
			JSON.stringify({ jsonrpc: "2.0", id: sent.id, result: { digest: "ABC" } })
		);

		await expect(promise).resolves.toEqual({ digest: "ABC" });
	});

	it("rejects when the engine answers with an error", async () => {
		const duplex = fakeDuplex();
		const client = new JsonRpcClient(duplex.writable, duplex.readable);

		const promise = client.request("bw/validate", {});
		const sent = lastFrameBody(duplex.written);
		duplex.deliver(
			JSON.stringify({
				jsonrpc: "2.0",
				id: sent.id,
				error: { code: -32601, message: "method not found" },
			})
		);

		await expect(promise).rejects.toThrow("method not found");
	});

	it("sends a notification with no id and expects no reply", () => {
		const duplex = fakeDuplex();
		const client = new JsonRpcClient(duplex.writable, duplex.readable);

		client.notify("exit");
		const sent = lastFrameBody(duplex.written);
		expect(sent.id).toBeUndefined();
		expect(sent.method).toBe("exit");
	});

	it("rejects in-flight requests when closed", async () => {
		const duplex = fakeDuplex();
		const client = new JsonRpcClient(duplex.writable, duplex.readable);

		const promise = client.request("bw/schemaDigest", {});
		client.close();
		await expect(promise).rejects.toThrow("closed");
	});

	it("rejects a new request after close", async () => {
		const duplex = fakeDuplex();
		const client = new JsonRpcClient(duplex.writable, duplex.readable);
		client.close();
		await expect(client.request("bw/schemaDigest", {})).rejects.toThrow("closed");
	});
});
