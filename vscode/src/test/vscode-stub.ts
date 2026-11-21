/**
 * A minimal vscode module for the vitest suite.
 *
 * The real vscode module only exists inside the editor's extension host, so a
 * unit test cannot import it directly. This file satisfies the exact surface
 * extension.ts uses and records what the extension did, so the tests assert on
 * registrations, messages and published diagnostics without a window. It is
 * wired in as an alias in vitest.config.ts and is never shipped.
 */

export class Position {
	constructor(
		public readonly line: number,
		public readonly character: number
	) {}
}

export class Range {
	readonly start: Position;
	readonly end: Position;

	constructor(
		startOrLine: Position | number,
		startOrCharacter?: Position | number,
		endLine?: number,
		endCharacter?: number
	) {
		if (typeof startOrLine === "number" && typeof startOrCharacter === "number") {
			this.start = new Position(startOrLine, startOrCharacter);
			this.end = new Position(endLine ?? startOrLine, endCharacter ?? startOrCharacter);
		} else {
			this.start = startOrLine as Position;
			this.end = (startOrCharacter as Position) ?? (startOrLine as Position);
		}
	}
}

export class Uri {
	constructor(
		public readonly scheme: string,
		public readonly fsPath: string
	) {}

	static file(fsPath: string): Uri {
		return new Uri("file", fsPath);
	}

	static parse(value: string): Uri {
		const match = /^file:\/\/(.+)$/.exec(value);
		if (match) {
			return new Uri("file", match[1]);
		}
		return new Uri("unknown", value);
	}

	toString(): string {
		return `file://${this.fsPath}`;
	}
}

export enum DiagnosticSeverity {
	Error = 0,
	Warning = 1,
	Information = 2,
	Hint = 3,
}

export class Diagnostic {
	code?: string;

	constructor(
		public readonly range: Range,
		public readonly message: string,
		public readonly severity: DiagnosticSeverity = DiagnosticSeverity.Error
	) {}
}

export class Location {
	constructor(
		public readonly uri: Uri,
		public readonly range: Range
	) {}
}

export type LocationLink = {
	targetUri: Uri;
	targetRange: Range;
	originSelectionRange?: Range;
	targetSelectionRange?: Range;
};

export type DefinitionLink = LocationLink;

export class Selection extends Range {
	constructor(
		startOrLine: Position | number,
		startOrCharacter?: Position | number,
		endLine?: number,
		endCharacter?: number
	) {
		super(startOrLine, startOrCharacter, endLine, endCharacter);
		this.anchor = this.start;
		this.active = this.end;
		this.isReversed = false;
	}

	anchor: Position;
	active: Position;
	isReversed: boolean;
}

// -----------------------------------------------------------------------------
// The shapes the real module provides, trimmed to what this extension uses
// -----------------------------------------------------------------------------

export interface TextDocument {
	uri: Uri;
	lineAt(line: number): { text: string };
	getText(): string;
}

export interface Selection {
	active: Position;
}

export interface TextEditor {
	document: TextDocument;
	selection: Selection;
	revealRange(range: Range, revealType: number): void;
}

export interface ExtensionContext {
	extensionPath?: string;
	subscriptions: Array<{ dispose(): void }>;
}

export interface DiagnosticCollection {
	set(uri: Uri, diagnostics: Diagnostic[]): void;
	delete(uri: Uri): void;
	clear(): void;
	dispose(): void;
}

export interface DefinitionProvider {
	provideDefinition(document: TextDocument, position: Position): unknown;
}

export interface OutputChannel {
	appendLine(line: string): void;
	dispose(): void;
}

// -----------------------------------------------------------------------------
// Recording singletons
// -----------------------------------------------------------------------------

/** What the tests inspect after running the extension. */
export const __state = {
	commands: new Map<string, (...args: unknown[]) => unknown>(),
	definitionProviders: [] as DefinitionProvider[],
	config: {} as Record<string, unknown>,
	workspaceFolders: [] as Array<{ uri: Uri }>,
	channels: [] as Array<{ lines: string[] }>,
	collections: [] as Array<{
		sets: Array<{ uri: Uri; diagnostics: Diagnostic[] }>;
	}>,
	messages: [] as Array<{ kind: "info" | "warn" | "error"; text: string }>,
	clipboard: [] as string[],
	openDocuments: [] as TextDocument[],
	shownEditors: [] as TextEditor[],
	changeListeners: [] as Array<(event: { document: TextDocument }) => void>,
	openListeners: [] as Array<(document: TextDocument) => void>,
};

export function __reset(): void {
	__state.commands.clear();
	__state.definitionProviders.length = 0;
	__state.config = {};
	__state.workspaceFolders = [];
	__state.channels.length = 0;
	__state.collections.length = 0;
	__state.messages.length = 0;
	__state.clipboard.length = 0;
	__state.openDocuments.length = 0;
	__state.shownEditors.length = 0;
	__state.changeListeners.length = 0;
	__state.openListeners.length = 0;
}

export const window = {
	createOutputChannel(_name: string) {
		const channel = { lines: [] as string[], appendLine(line: string) { channel.lines.push(line); }, dispose() {} };
		__state.channels.push(channel);
		return channel;
	},
	showInformationMessage(text: string) {
		__state.messages.push({ kind: "info", text });
		return Promise.resolve();
	},
	showWarningMessage(text: string) {
		__state.messages.push({ kind: "warn", text });
		return Promise.resolve();
	},
	showErrorMessage(text: string) {
		__state.messages.push({ kind: "error", text });
		return Promise.resolve();
	},
	activeTextEditor: null as null | TextEditor,
	async showTextDocument(document: TextDocument, _options?: unknown): Promise<TextEditor> {
		const editor: TextEditor = {
			document,
			selection: new Selection(0, 0, 0, 0),
			async revealRange(_range: Range, _revealType: number) {},
		};
		__state.shownEditors.push(editor);
		return editor;
	},
};

export const workspace = {
	getConfiguration(_section: string) {
		return {
			get<T>(key: string, fallback?: T): T | undefined {
				const value = __state.config[key];
				return value === undefined ? fallback : (value as T);
			},
		};
	},
	get workspaceFolders() {
		return __state.workspaceFolders;
	},
	onDidChangeTextDocument(listener: (event: { document: TextDocument }) => void) {
		__state.changeListeners.push(listener);
		return { dispose() {} };
	},
	onDidOpenTextDocument(listener: (document: TextDocument) => void) {
		__state.openListeners.push(listener);
		return { dispose() {} };
	},
	async openTextDocument(uri: Uri): Promise<TextDocument> {
		const existing = __state.openDocuments.find(
			(d) => d.uri.toString() === uri.toString()
		);
		if (existing) {
			return existing;
		}
		const document: TextDocument = {
			uri,
			lineAt(_line: number) {
				return { text: "" };
			},
			getText() {
				return "";
			},
		};
		__state.openDocuments.push(document);
		return document;
	},
};

export const commands = {
	registerCommand(id: string, handler: (...args: unknown[]) => unknown) {
		__state.commands.set(id, handler);
		return { dispose() {} };
	},
};

export const languages = {
	createDiagnosticCollection(_name: string): DiagnosticCollection {
		const collection = {
			sets: [] as Array<{ uri: Uri; diagnostics: Diagnostic[] }>,
			set(uri: Uri, diagnostics: Diagnostic[]) {
				collection.sets.push({ uri, diagnostics });
			},
			delete(_uri: Uri) {},
			clear() {},
			dispose() {},
		};
		__state.collections.push(collection);
		return collection;
	},
	registerDefinitionProvider(_selector: unknown, provider: DefinitionProvider) {
		__state.definitionProviders.push(provider);
		return { dispose() {} };
	},
};

export const env = {
	clipboard: {
		async writeText(text: string) {
			__state.clipboard.push(text);
		},
	},
};
