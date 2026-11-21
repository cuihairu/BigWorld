/**
 * BigWorld for VS Code -- extension wiring.
 *
 * This file deliberately contains no BigWorld parsing. Entity def files,
 * entities.xml and the schema digest are all interpreted by the engine's own
 * C++ code in programming/bigworld/lib/entitydef; the bw-lsp binary built from
 * it answers this extension over stdio JSON-RPC. The TS side only maps that
 * exchange onto the editor: commands, diagnostics, definitions and an output
 * channel. Rewriting the parser here is exactly the kind of double
 * implementation this extension exists to avoid.
 *
 * Everything the editor shows comes back through BwLspEngine unchanged: this
 * file never opens a def file itself, it only relays the engine's answer.
 */

import * as path from "node:path";
import * as vscode from "vscode";

import { BwLspEngine, BwLspDiagnostic, findResRoot } from "./engine";

/** The commands this extension contributes, as declared in package.json. */
export const COMMAND_SCHEMA_DIGEST = "bigworld.schemaDigest";
export const COMMAND_VALIDATE_DEF = "bigworld.validateDef";
export const COMMAND_REVEAL_DEFINITION = "bigworld.revealDefinition";

/** The engine process for this session, or null until something needs it. */
let engine: BwLspEngine | null = null;

/** In flight start, so two callers do not spawn two engines. */
let starting: Promise<BwLspEngine> | null = null;

let diagnostics: vscode.DiagnosticCollection | undefined;
let output: vscode.OutputChannel | undefined;
let extensionPath = "";

/** Where the bundled engine binary lives, relative to the extension. */
const BUNDLED_ENGINE = ["bin", "bw-lsp"];

/** Debounce timers, so typing does not spawn a validate per keystroke. */
const pending = new Map<string, NodeJS.Timeout>();

export function activate(context: vscode.ExtensionContext): void {
	extensionPath = context.extensionPath ?? "";

	output = vscode.window.createOutputChannel("BigWorld");
	context.subscriptions.push(output);

	diagnostics = vscode.languages.createDiagnosticCollection("bigworld-def");
	context.subscriptions.push(diagnostics);

	note("BigWorld extension activated.");

	context.subscriptions.push(
		vscode.commands.registerCommand(COMMAND_SCHEMA_DIGEST, guarded(
			commandSchemaDigest
		)),
		vscode.commands.registerCommand(COMMAND_VALIDATE_DEF, guarded(
			commandValidateDef
		)),
		vscode.commands.registerCommand(COMMAND_REVEAL_DEFINITION, guarded(
			commandRevealDefinition
		)),
		vscode.languages.registerDefinitionProvider(
			{ scheme: "file", pattern: "**/*.def" },
			definitionProvider
		),
		vscode.workspace.onDidChangeTextDocument((event) => {
			if (isDefDocument(event.document)) {
				scheduleValidate(event.document);
			}
		}),
		vscode.workspace.onDidOpenTextDocument((document) => {
			if (isDefDocument(document)) {
				validateDocument(document);
			}
		})
	);
}

/**
 * Nothing long-lived to release synchronously; the engine is stopped here and
 * its errors are reported rather than thrown out of deactivation.
 */
export function deactivate(): void {
	void stopEngine().catch((error) => {
		note(`Stopping the engine failed: ${messageOf(error)}`);
	});
	pending.clear();
}

// -----------------------------------------------------------------------------
// Engine access
// -----------------------------------------------------------------------------

/**
 * Returns a running engine, starting it on first use. A def document only
 * needs the engine once, so the process stays up across commands and edits.
 *
 * This never throws synchronously: a bad configuration or a failed spawn
 * surfaces as a rejection, which the guarded command wrapper turns into an
 * editor message rather than an unhandled rejection.
 */
async function withEngine(): Promise<BwLspEngine> {
	if (engine) {
		return engine;
	}
	if (starting) {
		return starting;
	}

	try {
		starting = startEngine();
		return await starting;
	} catch (error) {
		// startEngine assigns before any await only once it has a real promise;
		// on a synchronous throw starting was never set, so retry later.
		starting = null;
		throw error;
	}
}

function startEngine(): Promise<BwLspEngine> {
	const configuration = vscode.workspace.getConfiguration("bigworld");
	const configuredPath = configuration.get<string>("bwLspPath", "") ?? "";
	const bwLspPath = configuredPath || path.join(extensionPath, ...BUNDLED_ENGINE);

	const configuredRoot = configuration.get<string>("resRoot", "") ?? "";
	const resRoot = configuredRoot || workspaceResRoot();

	if (!resRoot) {
		throw new Error(
			"No BigWorld resource tree found: set bigworld.resRoot, or open a " +
				"workspace containing scripts/entities.xml."
		);
	}

	const newEngine = new BwLspEngine({
		bwLspPath,
		resRoot,
		onStderr: (line) => note(line),
	});

	return newEngine.start().then(() => {
		engine = newEngine;
		return newEngine;
	});
}

/**
 * The resource root to serve: the configured one, or the first ancestor of the
 * opened workspace that carries scripts/entities.xml.
 */
function workspaceResRoot(): string {
	const folders = vscode.workspace.workspaceFolders;
	if (!folders || folders.length === 0) {
		return "";
	}
	const startDir = folders[0].uri.fsPath;
	return findResRoot(startDir) ?? "";
}

function stopEngine(): Promise<void> {
	const current = engine;
	engine = null;
	starting = null;
	pending.clear();
	if (!current) {
		return Promise.resolve();
	}
	return current.stop();
}

// -----------------------------------------------------------------------------
// Commands
// -----------------------------------------------------------------------------

/** Reports the engine's schema digest for the served tree. */
async function commandSchemaDigest(): Promise<void> {
	const running = await withEngine();
	const digest = await running.schemaDigest();

	note(`Schema digest: ${digest || "(empty)"}`);

	if (!digest) {
		vscode.window.showWarningMessage(
			"BigWorld: the engine returned no schema digest. Check that the resource tree parses."
		);
		return;
	}

	try {
		await vscode.env.clipboard.writeText(digest);
	} catch {
		note("Could not put the digest on the clipboard.");
	}
	vscode.window.showInformationMessage(`BigWorld schema digest: ${digest}`);
}

/**
 * Validates the def file in the active editor. Every finding is the engine's
 * own, positioned where the engine says, so the squiggles follow the parse.
 */
async function commandValidateDef(): Promise<void> {
	const editor = vscode.window.activeTextEditor;
	if (!editor || !isDefDocument(editor.document)) {
		vscode.window.showInformationMessage(
			"BigWorld: open a .def file to validate."
		);
		return;
	}

	const findings = await validateDocument(editor.document);
	const count = findings.length;
	note(`Validated ${fileNameOf(editor.document.uri)}: ${count} finding(s).`);
	vscode.window.showInformationMessage(
		count === 0
			? "BigWorld: the def file validates clean."
			: `BigWorld: ${count} finding(s) in the def file.`
	);
}

/**
 * Jumps to the definition at the cursor: an entity type name opens its def, a
 * type alias opens alias.xml, a property opens itself in the current def.
 */
async function commandRevealDefinition(): Promise<void> {
	const editor = vscode.window.activeTextEditor;
	if (!editor) {
		return;
	}

	const symbol = wordAt(editor.document, editor.selection.active);
	if (!symbol) {
		vscode.window.showInformationMessage(
			"BigWorld: place the cursor on a type, alias or property name."
		);
		return;
	}

	const running = await withEngine();
	const definition = await running.definition(symbol, editor.document.uri.toString());

	note(`Definition of ${symbol}: ${definition.found ? definition.uri : "not found"}`);

	if (!definition.found || !definition.uri || !definition.range) {
		vscode.window.showWarningMessage(
			`BigWorld: no definition found for ${symbol}.`
		);
		return;
	}

	const target = definitionLocation(definition);
	if (!target) {
		return;
	}

	const document = await vscode.workspace.openTextDocument(target.uri);
	const shown = await vscode.window.showTextDocument(document);
	shown.selection = new vscode.Selection(
		target.range.start.line,
		target.range.start.character,
		target.range.start.line,
		target.range.start.character + 1
	);
	await shown.revealRange(target.range, 2 /* vscode.TextEditorRevealType.InCenterIfOutsideViewport */);
}

// -----------------------------------------------------------------------------
// Definition provider (F12)
// -----------------------------------------------------------------------------

const definitionProvider: vscode.DefinitionProvider = {
	async provideDefinition(document, position) {
		const symbol = wordAt(document, position);
		if (!symbol) {
			return null;
		}

		const running = await withEngine();
		const definition = await running.definition(symbol, document.uri.toString());
		if (!definition.found || !definition.uri || !definition.range) {
			return null;
		}

		const location = definitionLocation(definition);
		if (!location) {
			return null;
		}

		// A DefinitionLink rather than a bare Location, so the editor knows the
		// whole symbol range to reveal, not just the first column of it.
		const link: vscode.DefinitionLink = {
			targetUri: location.uri,
			targetRange: location.range,
			originSelectionRange: location.range,
			targetSelectionRange: location.range,
		};
		return [link];
	},
};

// -----------------------------------------------------------------------------
// Diagnostics
// -----------------------------------------------------------------------------

/**
 * Asks the engine about one def document and publishes its findings.
 * Returns the findings it got, so callers can report them in words.
 */
async function validateDocument(document: vscode.TextDocument): Promise<BwLspDiagnostic[]> {
	const running = await withEngine();
	const findings = await running.validate(document.uri.toString());

	diagnostics?.set(document.uri, toVsCodeDiagnostics(findings));
	return findings;
}

/** Debounces per document so a burst of edits costs one validate. */
function scheduleValidate(document: vscode.TextDocument): void {
	const key = document.uri.toString();
	const existing = pending.get(key);
	if (existing) {
		clearTimeout(existing);
	}
	const timer = setTimeout(() => {
		pending.delete(key);
		void validateDocument(document);
	}, 300);
	pending.set(key, timer);
}

function toVsCodeDiagnostics(findings: BwLspDiagnostic[]): vscode.Diagnostic[] {
	const out: vscode.Diagnostic[] = [];

	for (const finding of findings) {
		const range = toVsCodeRange(finding.range);
		const diagnostic = new vscode.Diagnostic(
			range,
			finding.message,
			toVsCodeSeverity(finding.severity)
		);
		diagnostic.code = finding.code;
		out.push(diagnostic);
	}

	return out;
}

function toVsCodeRange(range: {
	start: { line: number; character: number };
	end: { line: number; character: number };
}): vscode.Range {
	return new vscode.Range(
		range.start.line,
		range.start.character,
		range.end.line,
		range.end.character
	);
}

/** The engine uses VS Code's severity order, so this is a small guard rather
 * than a conversion: anything unexpected reads as an error. */
function toVsCodeSeverity(severity: number): vscode.DiagnosticSeverity {
	switch (severity) {
		case 1:
			return 0; // Error
		case 2:
			return 1; // Warning
		case 3:
			return 2; // Information
		case 4:
			return 3; // Hint
		default:
			return 0;
	}
}

/** Builds the location a definition answer points at. */
function definitionLocation(definition: {
	uri?: string;
	range?: {
		start: { line: number; character: number };
		end: { line: number; character: number };
	};
}): vscode.Location | null {
	if (!definition.uri || !definition.range) {
		return null;
	}
	return new vscode.Location(vscode.Uri.parse(definition.uri), toVsCodeRange(definition.range));
}

// -----------------------------------------------------------------------------
// Small helpers
// -----------------------------------------------------------------------------

/**
 * The identifier at position, or "" when the cursor sits on nothing one of
 * these names looks like.
 */
function wordAt(document: vscode.TextDocument, position: vscode.Position): string {
	const line = document.lineAt(position.line).text;
	let start = position.character;
	let end = position.character;

	const isWordChar = (character: string): boolean =>
		/^[A-Za-z0-9_]$/.test(character);

	while (start > 0 && isWordChar(line[start - 1])) {
		--start;
	}
	while (end < line.length && isWordChar(line[end])) {
		++end;
	}
	return line.substring(start, end);
}

function isDefDocument(document: vscode.TextDocument): boolean {
	return (
		document.uri.scheme === "file" &&
		fileNameOf(document.uri).endsWith(".def")
	);
}

function fileNameOf(uri: { fsPath: string }): string {
	return path.basename(uri.fsPath);
}

function note(message: string): void {
	output?.appendLine(message);
}

function messageOf(error: unknown): string {
	if (error instanceof Error) {
		return error.message;
	}
	return String(error);
}

/**
 * Wraps a command body so its failures reach the developer as an editor
 * message. A rejected command promise is otherwise just an entry in the
 * developer tools, which is the quiet failure mode this extension exists to
 * avoid.
 */
function guarded(
	command: () => Promise<void>
): () => Promise<void> {
	return () =>
		command().catch((error) => {
			const text = messageOf(error);
			note(`Command failed: ${text}`);
			vscode.window.showErrorMessage(`BigWorld: ${text}`);
		});
}
