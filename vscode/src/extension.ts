/**
 * BigWorld for VS Code — extension shell.
 *
 * This file deliberately contains no BigWorld parsing. Entity def files,
 * entities.xml and the schema digest are all interpreted by the engine's own
 * C++ code in programming/bigworld/lib/entitydef; the bw-lsp binary built from
 * it answers this extension over stdio JSON-RPC. The TS side only maps that
 * exchange onto the editor: languages, commands, diagnostics, definitions and
 * an output channel. Rewriting the parser here is exactly the kind of double
 * implementation this extension is meant to avoid.
 *
 * See README.md for the architecture constraint.
 */

import * as vscode from "vscode";

/**
 * The commands this shell contributes. They are declared in package.json and
 * each one is a thin delegation to the engine binary, so the list of names is
 * the only part of them that lives here.
 */
const COMMAND_SCHEMA_DIGEST = "bigworld.schemaDigest";
const COMMAND_VALIDATE_DEF = "bigworld.validateDef";
const COMMAND_REVEAL_DEFINITION = "bigworld.revealDefinition";

/**
 * Writes one line to the extension's output channel and returns it. The
 * channel is the only place this shell logs, so the operator can follow what
 * the engine reported without any of the shell's own commentary.
 */
function note(
	extensionOutput: vscode.OutputChannel,
	message: string
): void {
	extensionOutput.appendLine(message);
}

/**
 * Registers the commands. The engine binary is wired up by the build that
 * ships it; until then each command reports that plainly instead of guessing,
 * because a silent no-op in an editor is worse than an empty one.
 */
export function activate(context: vscode.ExtensionContext): void {
	const extensionOutput = vscode.window.createOutputChannel("BigWorld");
	context.subscriptions.push(extensionOutput);
	note(extensionOutput, "BigWorld extension activated.");

	const enginePending =
		(what: string): (() => void) =>
		() => {
			note(
				extensionOutput,
				`${what}: the bw-lsp engine binary is not wired into this build yet.`
			);
			vscode.window.showInformationMessage(
				`BigWorld: ${what} needs the bw-lsp binary built from the engine sources.`
			);
		};

	context.subscriptions.push(
		vscode.commands.registerCommand(
			COMMAND_SCHEMA_DIGEST,
			enginePending("Schema digest")
		),
		vscode.commands.registerCommand(
			COMMAND_VALIDATE_DEF,
			enginePending("Def validation")
		),
		vscode.commands.registerCommand(
			COMMAND_REVEAL_DEFINITION,
			enginePending("Definition")
		)
	);
}

/**
 * Nothing to release: the shell owns no long-lived resources of its own and
 * the engine process is torn down together with the extension context.
 */
export function deactivate(): void {
	// Intentionally empty.
}
