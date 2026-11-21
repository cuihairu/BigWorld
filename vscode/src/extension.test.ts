/**
 * Pins the thin shell: the commands are registered, each one delegates to the
 * engine and nothing more, and the editor-facing bits (diagnostics,
 * definition links) are built from what the engine answered. The engine itself
 * is a fake here, so nothing in this file parses a def.
 */

import { describe, expect, it, vi, beforeEach, afterEach } from "vitest";

import * as vscode from "vscode";

import {
	activate,
	deactivate,
	COMMAND_CROSS_REFERENCE,
	COMMAND_REVEAL_DEFINITION,
	COMMAND_SCHEMA_DIGEST,
	COMMAND_VALIDATE_DEF,
} from "./extension";
import { type BwLspDiagnostic, type BwLspRange } from "./engine";

const mocks = vi.hoisted(() => {
	const instances: Array<{
		options: unknown;
		started: boolean;
		stopped: boolean;
	}> = [];
	const calls: string[] = [];
	const DIGEST = "18D00DC69AC756433D091F537DA7030E";

	class FakeEngine {
		options: unknown;
		started = false;
		stopped = false;

		constructor(options: unknown) {
			this.options = options;
			instances.push(this);
		}

		async start(): Promise<void> {
			this.started = true;
		}

		async schemaDigest(): Promise<string> {
			calls.push("bw/schemaDigest");
			return DIGEST;
		}

		async validate(uri: string): Promise<BwLspDiagnostic[]> {
			calls.push("bw/validate");
			return [
				{
					severity: 1,
					code: "bw-lsp/parse-failed",
					message: "Could not find MetaDataType 'NOTATYPE'",
					range: {
						start: { line: 4, character: 9 },
						end: { line: 4, character: 16 },
					} satisfies BwLspRange,
				},
			];
		}

		async definition(symbol: string): Promise<{ found: boolean; uri?: string; range?: BwLspRange }> {
			calls.push(`bw/definition:${symbol}`);
			if (symbol !== "Avatar") {
				return { found: false };
			}
			return {
				found: true,
				uri: "file:///tree/scripts/entity_defs/Avatar.def",
				range: {
					start: { line: 0, character: 0 },
					end: { line: 0, character: 7 },
				},
			};
		}

		async crossReference(): Promise<BwLspDiagnostic[]> {
			calls.push("bw/crossReference");
			return [
				{
					severity: 2,
					code: "bw-lsp/orphan-def",
					message: "Def file 'Orphan.def' is not declared in entities.xml.",
					range: {
						start: { line: 0, character: 0 },
						end: { line: 0, character: 0 },
					},
				},
			];
		}

		async stop(): Promise<void> {
			this.stopped = true;
		}
	}

	return { FakeEngine, instances, calls, DIGEST };
});

vi.mock("./engine", () => ({
	BwLspEngine: mocks.FakeEngine,
	findResRoot: () => "/tree",
}));

/** A def document with "  <Avatar>" on the first line. */
function defDocument(fsPath = "/tree/scripts/entity_defs/Avatar.def") {
	return {
		uri: vscode.Uri.file(fsPath),
		lineAt(line: number) {
			return { text: line === 0 ? "  <Avatar>" : "" };
		},
		getText() {
			return "";
		},
	};
}

function context() {
	return { extensionPath: "/ext", subscriptions: [] as Array<{ dispose(): void }> };
}

function configureEngine(resRoot = "/tree"): void {
	vscode.__state.config["bwLspPath"] = "/ext/bin/bw-lsp";
	vscode.__state.config["resRoot"] = resRoot;
}

async function runCommand(id: string, ...args: unknown[]): Promise<void> {
	const handler = vscode.__state.commands.get(id);
	if (!handler) {
		throw new Error(`no handler registered for ${id}`);
	}
	await handler(...args);
}

beforeEach(() => {
	vscode.__reset();
	mocks.instances.length = 0;
	mocks.calls.length = 0;
	configureEngine();
});

afterEach(() => {
	deactivate();
});

describe("activate", () => {
	it("registers the four commands, the definition provider and diagnostics", () => {
		activate(context() as unknown as vscode.ExtensionContext);

		expect(vscode.__state.commands.has(COMMAND_SCHEMA_DIGEST)).toBe(true);
		expect(vscode.__state.commands.has(COMMAND_VALIDATE_DEF)).toBe(true);
		expect(vscode.__state.commands.has(COMMAND_REVEAL_DEFINITION)).toBe(true);
		expect(vscode.__state.commands.has(COMMAND_CROSS_REFERENCE)).toBe(true);
		expect(vscode.__state.definitionProviders).toHaveLength(1);
		expect(vscode.__state.collections).toHaveLength(1);
	});

	it("subscribes its disposables to the extension context", () => {
		const ctx = context();
		activate(ctx as unknown as vscode.ExtensionContext);
		expect(ctx.subscriptions.length).toBeGreaterThan(0);
	});

	it("starts the engine once for the first command that needs it", async () => {
		activate(context() as unknown as vscode.ExtensionContext);
		await runCommand(COMMAND_SCHEMA_DIGEST);
		await runCommand(COMMAND_VALIDATE_DEF);

		expect(mocks.instances).toHaveLength(1);
		expect(mocks.instances[0].started).toBe(true);
	});
});

describe("schema digest command", () => {
	it("asks the engine and shows the digest", async () => {
		activate(context() as unknown as vscode.ExtensionContext);
		await runCommand(COMMAND_SCHEMA_DIGEST);

		expect(mocks.calls).toContain("bw/schemaDigest");
		const shown = vscode.__state.messages.map((m) => m.text).join("\n");
		expect(shown).toContain(mocks.DIGEST);
	});

	it("puts the digest on the clipboard", async () => {
		activate(context() as unknown as vscode.ExtensionContext);
		await runCommand(COMMAND_SCHEMA_DIGEST);
		expect(vscode.__state.clipboard).toContain(mocks.DIGEST);
	});
});

describe("validate def command", () => {
	it("publishes the engine's findings on the open def", async () => {
		const document = defDocument();
		vscode.window.activeTextEditor = { document, selection: { active: new vscode.Position(0, 3) } };

		activate(context() as unknown as vscode.ExtensionContext);
		await runCommand(COMMAND_VALIDATE_DEF);

		const collection = vscode.__state.collections[0];
		expect(collection.sets).toHaveLength(1);
		expect(collection.sets[0].uri.toString()).toBe(document.uri.toString());
		expect(collection.sets[0].diagnostics).toHaveLength(1);

		const diagnostic = collection.sets[0].diagnostics[0];
		expect(diagnostic.message).toBe("Could not find MetaDataType 'NOTATYPE'");
		expect(diagnostic.code).toBe("bw-lsp/parse-failed");
		// Engine severity 1 is an error in VS Code terms.
		expect(diagnostic.severity).toBe(0);
		expect(diagnostic.range.start.line).toBe(4);
		expect(diagnostic.range.start.character).toBe(9);
	});

	it("says so when the active editor is not a def file", async () => {
		const document = defDocument("/tree/notes.txt");
		vscode.window.activeTextEditor = { document, selection: { active: new vscode.Position(0, 0) } };

		activate(context() as unknown as vscode.ExtensionContext);
		await runCommand(COMMAND_VALIDATE_DEF);

		const shown = vscode.__state.messages.map((m) => m.text).join("\n");
		expect(shown).toContain(".def");
		expect(mocks.calls).not.toContain("bw/validate");
	});
});

describe("definition command", () => {
	it("opens the def the engine pointed at", async () => {
		const document = defDocument();
		vscode.window.activeTextEditor = { document, selection: { active: new vscode.Position(0, 3) } };

		activate(context() as unknown as vscode.ExtensionContext);
		await runCommand(COMMAND_REVEAL_DEFINITION);

		expect(mocks.calls).toContain("bw/definition:Avatar");
		const opened = vscode.__state.shownEditors;
		expect(opened).toHaveLength(1);
		expect((opened[0].document as typeof document).uri.toString()).toBe(
			"file:///tree/scripts/entity_defs/Avatar.def"
		);
	});

	it("warns when the engine finds nothing", async () => {
		const document = defDocument();
		vscode.window.activeTextEditor = { document, selection: { active: new vscode.Position(0, 3) } };

		// The cursor is on the word Avatar, so force a miss by asking for it
		// as a name the fake does not know.
		const originalDefinition = mocks.FakeEngine.prototype.definition;
		mocks.FakeEngine.prototype.definition = async () => ({ found: false });

		activate(context() as unknown as vscode.ExtensionContext);
		await runCommand(COMMAND_REVEAL_DEFINITION);
		mocks.FakeEngine.prototype.definition = originalDefinition;

		expect(vscode.__state.shownEditors).toHaveLength(0);
		const shown = vscode.__state.messages.map((m) => m.text).join("\n");
		expect(shown).toContain("no definition found");
	});
});

describe("definition provider (F12)", () => {
	it("returns a DefinitionLink for an entity type name", async () => {
		const document = defDocument();
		activate(context() as unknown as vscode.ExtensionContext);

		const provider = vscode.__state.definitionProviders[0] as {
			provideDefinition: (d: unknown, p: vscode.Position) => unknown;
		};
		const links = (await provider.provideDefinition(
			document,
			new vscode.Position(0, 3)
		)) as vscode.DefinitionLink[];

		expect(links).toHaveLength(1);
		expect(links[0].targetUri.toString()).toBe("file:///tree/scripts/entity_defs/Avatar.def");
		expect(links[0].targetRange.start.line).toBe(0);
	});

	it("returns nothing when the engine knows no such symbol", async () => {
		const document = defDocument();
		activate(context() as unknown as vscode.ExtensionContext);

		const provider = vscode.__state.definitionProviders[0] as {
			provideDefinition: (d: unknown, p: vscode.Position) => unknown;
		};
		const link = await provider.provideDefinition(
			document,
			new vscode.Position(1, 0)
		);

		expect(link).toBeNull();
	});
});

describe("cross-reference command", () => {
	it("lists the engine's findings in the output channel", async () => {
		activate(context() as unknown as vscode.ExtensionContext);
		await runCommand(COMMAND_CROSS_REFERENCE);

		expect(mocks.calls).toContain("bw/crossReference");
		const channelLines = vscode.__state.channels
			.map((channel) => channel.lines.join("\n"))
			.join("\n");
		expect(channelLines).toContain("bw-lsp/orphan-def");
		expect(channelLines).toContain("Orphan.def");
		expect(channelLines).toContain("1 finding(s)");
	});

	it("reports agreement when the tree is consistent", async () => {
		const original = mocks.FakeEngine.prototype.crossReference;
		mocks.FakeEngine.prototype.crossReference = async () => [];

		activate(context() as unknown as vscode.ExtensionContext);
		await runCommand(COMMAND_CROSS_REFERENCE);
		mocks.FakeEngine.prototype.crossReference = original;

		const shown = vscode.__state.messages.map((m) => m.text).join("\n");
		expect(shown).toContain("agree");
	});
});

describe("deactivate", () => {
	it("stops the engine it had started", async () => {
		activate(context() as unknown as vscode.ExtensionContext);
		await runCommand(COMMAND_SCHEMA_DIGEST);
		const engine = mocks.instances[0];

		deactivate();
		await vi.waitFor(() => expect(engine.stopped).toBe(true));
	});
});
