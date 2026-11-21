import path from "node:path";
import { defineConfig } from "vitest/config";

export default defineConfig({
	resolve: {
		alias: {
			// The vscode module only exists inside the editor's extension host,
			// so the unit suite stands in a minimal recording stub for it.
			vscode: path.resolve(__dirname, "src/test/vscode-stub.ts"),
		},
	},
	test: {
		environment: "node",
		include: ["src/**/*.test.ts"],
	},
});
