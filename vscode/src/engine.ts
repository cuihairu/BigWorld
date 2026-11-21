/**
 * The engine process: spawn bw-lsp, keep one session over its stdio.
 *
 * Everything this file returns is a direct pass-through of what the engine
 * answered. No def file, entities.xml or type name is looked at here beyond
 * the plain filesystem probe findResRoot needs to locate a resource tree --
 * parsing is the engine's job and stays in programming/bigworld/lib/entitydef.
 */

import { spawn as nodeSpawn } from "node:child_process";
import * as fs from "node:fs";
import * as path from "node:path";

import { JsonRpcClient } from "./rpc";

/** A zero-based range in the file, in the editor's coordinate model. */
export interface BwLspRange {
	start: { line: number; character: number };
	end: { line: number; character: number };
}

/** One finding in a def file, as the engine reports it. */
export interface BwLspDiagnostic {
	/** 1 error, 2 warning, 3 information, 4 hint (VS Code convention). */
	severity: number;
	code?: string;
	message: string;
	range: BwLspRange;
}

/** The answer to bw/definition: where, if anywhere, the symbol lives. */
export interface BwLspDefinition {
	found: boolean;
	uri?: string;
	range?: BwLspRange;
}

/**
 * Walks up from startDir looking for a directory that carries
 * scripts/entities.xml and returns it, or null when none does.
 *
 * This is the same lookup the engine offers as findResTree; the extension
 * needs it before it can spawn anything, so it is done here with plain
 * filesystem access and never looks inside the file.
 */
export function findResRoot(startDir: string): string | null {
	let dir = path.resolve(startDir);

	for (let level = 0; level < 10; ++level) {
		if (fs.existsSync(path.join(dir, "scripts", "entities.xml"))) {
			return dir;
		}
		const parent = path.dirname(dir);
		if (parent === dir) {
			break;
		}
		dir = parent;
	}

	return null;
}

/**
 * The stdio pair the engine speaks on. child_process's pipes satisfy this;
 * the test suite hands in a fake with the same surface.
 */
export interface BwLspProcessLike {
	stdin: { write(chunk: Buffer): unknown };
	stdout: {
		on(event: "data", listener: (chunk: Buffer) => void): unknown;
	};
	on(event: "error", listener: (error: Error) => void): unknown;
	on(event: "exit", listener: () => void): unknown;
	kill(): void;
}

/** How a test can stand in for the real process. */
export type BwLspSpawnFn = (
	command: string,
	args: string[]
) => BwLspProcessLike;

/** What the caller must provide: where the binary is and which tree it serves. */
export interface BwLspEngineOptions {
	bwLspPath: string;
	resRoot: string;
	/** Lines from the engine's stderr, for the output channel. */
	onStderr?: (line: string) => void;
	/** Injection point for tests; defaults to the real spawn. */
	spawnFn?: BwLspSpawnFn;
}

/**
 * One bw-lsp session. start() spawns and completes the initialize handshake;
 * every method then rides the same pipe until stop() shuts it down.
 */
export class BwLspEngine {
	private client: JsonRpcClient | null = null;
	private process: BwLspProcessLike | null = null;
	private stopped = false;

	constructor(private readonly options: BwLspEngineOptions) {}

	/** True once the process is gone or stop() was called. */
	get isStopped(): boolean {
		return this.stopped;
	}

	/** Spawns bw-lsp serve and completes the initialize handshake. */
	async start(): Promise<void> {
		if (this.client) {
			return;
		}

		const spawnFn = this.options.spawnFn ?? ((cmd, args) =>
			nodeSpawn(cmd, args, { stdio: ["pipe", "pipe", "pipe"] }));

		const proc = spawnFn(this.options.bwLspPath, [
			"serve",
			"--res",
			this.options.resRoot,
		]);
		this.process = proc;

		// The engine prints its own INFO/DEBUG lines on stderr; route them to
		// the output channel so a developer can see what it said without the
		// extension inventing commentary of its own.
		const stderr = (proc as { stderr?: { on(event: string, l: (c: Buffer) => void): unknown } }).stderr;
		if (this.options.onStderr && stderr?.on) {
			let tail = "";
			stderr.on("data", (chunk: Buffer) => {
				tail += chunk.toString("utf8");
				const lines = tail.split(/\r?\n/);
				tail = lines.pop() ?? "";
				for (const line of lines) {
					this.options.onStderr?.(line);
				}
			});
		}

		this.client = new JsonRpcClient(proc.stdin, proc.stdout);
		await this.client.request("initialize", {
			processId: null,
			rootUri: null,
		});
	}

	/** The schema digest of the opened tree, as the engine computed it. */
	async schemaDigest(): Promise<string> {
		const reply = (await this.request("bw/schemaDigest", {})) as {
			digest?: string;
		};
		return reply.digest ?? "";
	}

	/** The engine's findings for one def file, located in the file. */
	async validate(uri: string): Promise<BwLspDiagnostic[]> {
		const reply = (await this.request("bw/validate", { uri })) as {
			diagnostics?: BwLspDiagnostic[];
		};
		return reply.diagnostics ?? [];
	}

	/** Where a symbol lives, or nothing when it is not one of these names. */
	async definition(symbol: string, fromUri: string): Promise<BwLspDefinition> {
		const reply = (await this.request("bw/definition", {
			symbol,
			uri: fromUri,
		})) as BwLspDefinition;
		return { found: reply.found === true, uri: reply.uri, range: reply.range };
	}

	/**
	 * Def files entities.xml does not declare, as tree-level findings. Their
	 * ranges are all zero: the findings are about the tree, not a spot in a
	 * file, so they are listed rather than drawn.
	 */
	async crossReference(): Promise<BwLspDiagnostic[]> {
		const reply = (await this.request("bw/crossReference", {})) as {
			findings?: BwLspDiagnostic[];
		};
		return reply.findings ?? [];
	}

	/** Shuts the engine down cleanly: shutdown, exit, then kill as a floor. */
	async stop(): Promise<void> {
		if (this.stopped) {
			return;
		}
		this.stopped = true;

		const client = this.client;
		if (client) {
			try {
				await client.request("shutdown");
			} catch {
				// Already gone or already shutting down; the kill below is
				// the guarantee either way.
			}
			client.notify("exit");
			client.close();
		}

		this.process?.kill();
		this.client = null;
		this.process = null;
	}

	private async request(method: string, params: unknown): Promise<unknown> {
		if (this.stopped || !this.client) {
			throw new Error("bw-lsp engine is not running");
		}
		return this.client.request(method, params);
	}
}
