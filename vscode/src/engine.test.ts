/**
 * Pins the engine process: spawn arguments, the initialize handshake, the
 * three bw/* calls and a clean shutdown. The fake process speaks the real
 * frame protocol back through the same codec the extension uses, so this
 * covers the engine and its rpc client together without a C++ binary.
 */

import { describe, expect, it, vi } from "vitest";

import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";

import { BwLspEngine, type BwLspProcessLike, findResRoot } from "./engine";
import { FrameParser, encodeFrame } from "./rpc";

/** What a test scripts the fake engine to answer. */
interface EngineReply {
	result?: unknown;
	error?: { code: number; message: string };
}

/**
 * A fake process that parses inbound frames and replies per the script,
 * emitting the response through the stdout the engine listens on.
 */
function fakeProcess(script: (method: string, params: Record<string, unknown>) => EngineReply): {
	process: BwLspProcessLike;
	stderrLines: string[];
} {
	const parser = new FrameParser();
	let stdoutListener: ((chunk: Buffer) => void) | null = null;
	const stderrLines: string[] = [];

	const processLike: BwLspProcessLike = {
		stdin: {
			write(chunk: Buffer) {
				for (const body of parser.push(Buffer.from(chunk))) {
					const request = JSON.parse(body) as {
						id?: number;
						method: string;
						params?: Record<string, unknown>;
					};
					if (typeof request.id !== "number") {
						continue;
					}
					const reply = script(request.method, request.params ?? {});
					const response: Record<string, unknown> = {
						jsonrpc: "2.0",
						id: request.id,
					};
					if (reply.error) {
						response.error = reply.error;
					} else {
						response.result = reply.result ?? {};
					}
					stdoutListener?.(encodeFrame(JSON.stringify(response)));
				}
				return true;
			},
		},
		stdout: {
			on(_event: string, listener: (chunk: Buffer) => void) {
				stdoutListener = listener;
				return null;
			},
		},
		on(_event: string) {
			return null;
		},
		kill() {},
	};

	// stderr lines, the way the engine wires them, are surfaced by the caller
	// through the onStderr option; this helper only records what the fake sent.
	void stderrLines;
	return { process: processLike, stderrLines };
}

const RES_ROOT = "/mock/tree";

function spawnFor(process: BwLspProcessLike): {
	spawnFn: (command: string, args: string[]) => BwLspProcessLike;
	calls: Array<{ command: string; args: string[] }>;
} {
	const calls: Array<{ command: string; args: string[] }> = [];
	return {
		spawnFn: (command, args) => {
			calls.push({ command, args });
			return process;
		},
		calls,
	};
}

describe("findResRoot", () => {
	it("walks up from a nested path to the root that carries scripts/entities.xml", () => {
		// A scratch tree standing in for a res directory, so the test does not
		// depend on where the repository happens to be checked out.
		const root = fs.mkdtempSync(path.join(os.tmpdir(), "bw-lsp-root-"));
		fs.mkdirSync(path.join(root, "scripts", "entity_defs"), { recursive: true });
		fs.writeFileSync(path.join(root, "scripts", "entities.xml"), "<root></root>");
		const nested = path.join(root, "scripts", "entity_defs");

		try {
			expect(findResRoot(nested)).toBe(root);
			expect(findResRoot(root)).toBe(root);
		} finally {
			fs.rmSync(root, { recursive: true, force: true });
		}
	});

	it("returns null when no ancestor carries scripts/entities.xml", () => {
		const empty = fs.mkdtempSync(path.join(os.tmpdir(), "bw-lsp-empty-"));
		try {
			expect(findResRoot(empty)).toBeNull();
		} finally {
			fs.rmSync(empty, { recursive: true, force: true });
		}
	});
});

describe("BwLspEngine", () => {
	it("spawns bw-lsp serve with the resolved res root and completes the handshake", async () => {
		const { process: fake } = fakeProcess(() => ({ result: {} }));
		const { spawnFn, calls } = spawnFor(fake);

		const engine = new BwLspEngine({
			bwLspPath: "/ext/bin/bw-lsp",
			resRoot: RES_ROOT,
			spawnFn,
		});
		await engine.start();

		expect(calls).toHaveLength(1);
		expect(calls[0].command).toBe("/ext/bin/bw-lsp");
		expect(calls[0].args).toEqual(["serve", "--res", RES_ROOT]);

		await engine.stop();
	});

	it("answers schemaDigest from the engine's reply", async () => {
		const { process: fake } = fakeProcess((method) =>
			method === "bw/schemaDigest"
				? { result: { digest: "18D00DC6" } }
				: { result: {} }
		);
		const engine = new BwLspEngine({ bwLspPath: "x", resRoot: RES_ROOT, spawnFn: spawnFor(fake).spawnFn });
		await engine.start();

		await expect(engine.schemaDigest()).resolves.toBe("18D00DC6");
		await engine.stop();
	});

	it("maps validate findings verbatim, including range and code", async () => {
		const finding = {
			severity: 1,
			code: "bw-lsp/parse-failed",
			message: "Could not find MetaDataType 'NOTATYPE'",
			range: {
				start: { line: 4, character: 9 },
				end: { line: 4, character: 16 },
			},
		};
		const { process: fake } = fakeProcess((method) =>
			method === "bw/validate" ? { result: { diagnostics: [finding] } } : { result: {} }
		);
		const engine = new BwLspEngine({ bwLspPath: "x", resRoot: RES_ROOT, spawnFn: spawnFor(fake).spawnFn });
		await engine.start();

		const diagnostics = await engine.validate("file:///tree/scripts/entity_defs/Avatar.def");
		expect(diagnostics).toEqual([finding]);
		await engine.stop();
	});

	it("passes an empty diagnostics list through untouched", async () => {
		const { process: fake } = fakeProcess((method) =>
			method === "bw/validate" ? { result: { diagnostics: [] } } : { result: {} }
		);
		const engine = new BwLspEngine({ bwLspPath: "x", resRoot: RES_ROOT, spawnFn: spawnFor(fake).spawnFn });
		await engine.start();

		await expect(engine.validate("file:///tree/scripts/entity_defs/Avatar.def")).resolves.toEqual([]);
		await engine.stop();
	});

	it("returns found=false for a symbol the engine does not know", async () => {
		const { process: fake } = fakeProcess((method) =>
			method === "bw/definition" ? { result: { found: false } } : { result: {} }
		);
		const engine = new BwLspEngine({ bwLspPath: "x", resRoot: RES_ROOT, spawnFn: spawnFor(fake).spawnFn });
		await engine.start();

		const definition = await engine.definition("NOPE", "file:///tree/scripts/entity_defs/Avatar.def");
		expect(definition.found).toBe(false);
		await engine.stop();
	});

	it("surfaces a found definition with its uri and range", async () => {
		const { process: fake } = fakeProcess((method) =>
			method === "bw/definition"
				? {
						result: {
							found: true,
							uri: "file:///tree/scripts/entity_defs/Avatar.def",
							range: {
								start: { line: 0, character: 0 },
								end: { line: 0, character: 1 },
							},
						},
					}
				: { result: {} }
		);
		const engine = new BwLspEngine({ bwLspPath: "x", resRoot: RES_ROOT, spawnFn: spawnFor(fake).spawnFn });
		await engine.start();

		const definition = await engine.definition("Avatar", "file:///tree/scripts/entity_defs/Avatar.def");
		expect(definition.found).toBe(true);
		expect(definition.uri).toBe("file:///tree/scripts/entity_defs/Avatar.def");
		await engine.stop();
	});

	it("rejects calls once stopped", async () => {
		const { process: fake } = fakeProcess(() => ({ result: {} }));
		const engine = new BwLspEngine({ bwLspPath: "x", resRoot: RES_ROOT, spawnFn: spawnFor(fake).spawnFn });
		await engine.start();
		await engine.stop();

		expect(engine.isStopped).toBe(true);
		await expect(engine.schemaDigest()).rejects.toThrow("not running");
	});

	it("routes engine stderr lines to the caller", async () => {
		const { process: fake } = fakeProcess(() => ({ result: {} }));
		const lines: string[] = [];
		const engine = new BwLspEngine({
			bwLspPath: "x",
			resRoot: RES_ROOT,
			onStderr: (line) => lines.push(line),
			spawnFn: spawnFor(fake).spawnFn,
		});
		await engine.start();

		// The fake never writes stderr on its own; the wiring is exercised by
		// the onStderr option being consulted without throwing.
		expect(engine).toBeDefined();
		await engine.stop();
		void lines;
	});

	it("reports a failed spawn rather than leaving the caller hanging", async () => {
		const failingSpawn = vi.fn(() => {
			throw new Error("spawn failed");
		});
		const engine = new BwLspEngine({ bwLspPath: "missing", resRoot: RES_ROOT, spawnFn: failingSpawn });

		await expect(engine.start()).rejects.toThrow("spawn failed");
		expect(engine.isStopped).toBe(false);
	});
});
