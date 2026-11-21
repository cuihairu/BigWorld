/* Bundles the extension host entry to out/extension.js.
 *
 * The engine binary is not bundled: it is a separate C++ build the extension
 * either ships in bin/ or finds through bigworld.bwLspPath. The vscode module
 * is supplied by the editor's extension host and stays external. */
const esbuild = require("esbuild");

const production = process.argv.includes("--production");
const watch = process.argv.includes("--watch");

const options = {
	entryPoints: ["src/extension.ts"],
	bundle: true,
	format: "cjs",
	platform: "node",
	target: "node20",
	outfile: "out/extension.js",
	external: ["vscode"],
	minify: production,
	sourcemap: !production,
	logLevel: "info",
};

async function main() {
	if (watch) {
		const context = await esbuild.context(options);
		await context.watch();
		console.log("[esbuild] watching src/");
	} else {
		await esbuild.build(options);
	}
}

main().catch(() => process.exit(1));
