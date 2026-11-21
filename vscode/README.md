# BigWorld for VS Code

Def file support for the [BigWorld OSE](https://github.com/cuihairu/BigWorld) server: syntax highlighting,
snippets, validation and the schema digest — all of them served by the
engine's own C++ code, never by a second implementation.

## Architecture

One implementation, one version. Entity def files (`*.def`), `entities.xml`
and the schema digest are interpreted by `programming/bigworld/lib/entitydef`,
the same code the server runs, compiled into a small engine binary `bw-lsp`
that this extension talks to over stdio JSON-RPC.

```
vscode/extension  ──stdio JSON-RPC──►  bw-lsp  ──►  lib/entitydef
(TypeScript shell:                     (C++ executable built by
 languages, commands, diagnostics,     the CMake entitydef sample:
 definitions, output channel)         parse, validate, schema digest)
```

The TypeScript side is a shell only: language configuration, snippets, the
command palette and the mapping of the engine's replies onto the editor. It
holds no def or entities parsing, so there is no second copy of the engine
logic to keep in sync. This is the reason the extension exists in this shape
instead of the split-engine form (where a rewritten analysis engine and the
real one drift apart and disagree).

Language basics (configuration, snippets, textmate grammar) are plain VS Code
contributions and need no engine.

## Layout

| Path | What |
| --- | --- |
| `package.json` | extension manifest |
| `language-configuration.json` | brackets, comments for `.def` |
| `syntaxes/bigworld-def.tmLanguage.json` | def highlighting |
| `syntaxes/bigworld-entities-injection.json` | entity names in `entities.xml` |
| `snippets/bigworld-def.json` | def snippets |
| `l10n/bundle.l10n.json`, `l10n/bundle.l10n.zh-cn.json` | strings, en + zh-cn |
| `src/extension.ts` | the shell |
| `out/extension.js` | built entry point |

## Build the engine binary

`bw-lsp` is built from the engine sources through the CMake entitydef sample:

```sh
VCPKG_ROOT=/home/cui/vcpkg cmake -S programming/bigworld/cmake/samples/entitydef \
  -B programming/bigworld/build/cmake-entitydef \
  -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake \
  -DCMAKE_BUILD_TYPE=Release
cmake --build programming/bigworld/build/cmake-entitydef -j
```

The binary lands in `programming/bigworld/build/cmake-entitydef/`. Point the
extension at it with the `bigworld.bwLspPath` setting, or install it somewhere
on `PATH`.

## Install the extension

```sh
cd vscode && npx vsce package
code --install-extension bigworld-vscode-*.vsix
```

## Architecture constraint

This extension must never grow a second implementation of BigWorld def or
entities parsing. Any validation, index or digest work belongs in
`programming/bigworld/lib/entitydef` and its unit tests, exposed through
`bw-lsp`; the TypeScript shell only transports it. That is the whole point of
the extension and the reason it is a thin shell over a C++ binary.
