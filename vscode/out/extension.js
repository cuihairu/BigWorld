var __create = Object.create;
var __defProp = Object.defineProperty;
var __getOwnPropDesc = Object.getOwnPropertyDescriptor;
var __getOwnPropNames = Object.getOwnPropertyNames;
var __getProtoOf = Object.getPrototypeOf;
var __hasOwnProp = Object.prototype.hasOwnProperty;
var __export = (target, all) => {
  for (var name in all)
    __defProp(target, name, { get: all[name], enumerable: true });
};
var __copyProps = (to, from, except, desc) => {
  if (from && typeof from === "object" || typeof from === "function") {
    for (let key of __getOwnPropNames(from))
      if (!__hasOwnProp.call(to, key) && key !== except)
        __defProp(to, key, { get: () => from[key], enumerable: !(desc = __getOwnPropDesc(from, key)) || desc.enumerable });
  }
  return to;
};
var __toESM = (mod, isNodeMode, target) => (target = mod != null ? __create(__getProtoOf(mod)) : {}, __copyProps(
  // If the importer is in node compatibility mode or this is not an ESM
  // file that has been converted to a CommonJS file using a Babel-
  // compatible transform (i.e. "__esModule" has not been set), then set
  // "default" to the CommonJS "module.exports" for node compatibility.
  isNodeMode || !mod || !mod.__esModule ? __defProp(target, "default", { value: mod, enumerable: true }) : target,
  mod
));
var __toCommonJS = (mod) => __copyProps(__defProp({}, "__esModule", { value: true }), mod);

// vscode/src/extension.ts
var extension_exports = {};
__export(extension_exports, {
  activate: () => activate,
  deactivate: () => deactivate
});
module.exports = __toCommonJS(extension_exports);
var vscode = __toESM(require("vscode"));
var COMMAND_SCHEMA_DIGEST = "bigworld.schemaDigest";
var COMMAND_VALIDATE_DEF = "bigworld.validateDef";
var COMMAND_REVEAL_DEFINITION = "bigworld.revealDefinition";
function note(extensionOutput, message) {
  extensionOutput.appendLine(message);
}
function activate(context) {
  const extensionOutput = vscode.window.createOutputChannel("BigWorld");
  context.subscriptions.push(extensionOutput);
  note(extensionOutput, "BigWorld extension activated.");
  const enginePending = (what) => () => {
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
function deactivate() {
}
// Annotate the CommonJS export names for ESM import in node:
0 && (module.exports = {
  activate,
  deactivate
});
