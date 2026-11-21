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

## Cross-reference check

The `BigWorld: Cross-Reference Entities` command (`bigworld.crossReference`)
asks `bw-lsp` to compare `entities.xml` against the def files on disk. The one
thing it reports that nothing else does is a def file no entity type declares:
the server never loads such a def, so `validate` says nothing about it (it is
not declared, so nobody validates it) and the schema digest does not include
it either. It is usually a typo in `entities.xml` or a def left behind by a
rename.

The converse -- an entity type declared with no def file -- is not reported
here because it fails the `entities.xml` parse itself, which already surfaces
as an empty schema digest and as the validation findings on the affected defs.

The findings are about the tree rather than a spot in a file, so they carry no
range and the command lists them in the BigWorld output channel.

## FantasyDemo walkthrough

Every surface below was exercised on the sample tree, `programming/fantasydemo/res`,
and the outputs are the engine's own, straight from the binary that the
extension spawns. A def file with a bad type is shown to demonstrate the
squiggles, and it is created in a scratch copy so the sample tree stays intact.

**Schema digest** (the `BigWorld: Schema Digest` command):

```
$ bw-lsp schema-digest --res programming/fantasydemo/res
{"digest":"18D00DC69AC756433D091F537DA7030E"}
```

**Validation** (the red squiggles, via `bw/validate`). A good def is clean:

```
$ bw-lsp validate --res programming/fantasydemo/res \
    programming/fantasydemo/res/scripts/entity_defs/Avatar.def
{"diagnostics":[]}
```

Change one property's `Type` to `NOTATYPE` and the engine names the type, the
property and the file position of the offending token:

```
{"diagnostics":[
  {"code":"bw-lsp/parse-failed",
   "message":"DataType::buildDataType: Could not find MetaDataType 'NOTATYPE'\n",
   "range":{"end":{"character":18,"line":19},"start":{"character":10,"line":19}},
   "severity":1},
  ...]}
```

**Jump to definition** (F12 and the `BigWorld: Go to Definition` command). An
entity type name opens its def:

```
$ bw-lsp definition --res programming/fantasydemo/res Avatar \
    --from programming/fantasydemo/res/scripts/entity_defs/Account.def
{"found":true,
 "uri":"file:///home/cui/workspaces/BigWorld/programming/fantasydemo/res/scripts/entity_defs/Avatar.def",
 "range":{"end":{"character":1,"line":0},"start":{"character":0,"line":0}}}
```

A property name resolves to itself, inside the def it is written in:

```
$ bw-lsp definition --res programming/fantasydemo/res playerName \
    --from programming/fantasydemo/res/scripts/entity_defs/Avatar.def
{"found":true,
 "uri":"file:///home/cui/workspaces/BigWorld/programming/fantasydemo/res/scripts/entity_defs/Avatar.def",
 "range":{"end":{"character":3,"line":10},"start":{"character":2,"line":10}}}
```

**Cross-reference** (the `BigWorld: Cross-Reference Entities` command). On the
sample tree the cross-reference is clean, and the serve loop the extension
talks to answers the same request:

```
$ bw-lsp cross-ref --res programming/fantasydemo/res
{"findings":[]}
```

A def file nothing declares is named, by code and file:

```
$ bw-lsp cross-ref --res <tree carrying scripts/entity_defs/Orphan.def>
{"findings":[{"code":"bw-lsp/orphan-def","message":"Def file 'Orphan.def' is
not declared in entities.xml.","range":{"end":{"character":0,"line":0},
"start":{"character":0,"line":0}},"severity":2}]}
```

The serve loop carries the same request:

```
bw-lsp serve --res programming/fantasydemo/res
  initialize            -> {"capabilities":{}}
  bw/crossReference     -> {"findings":[]}
  shutdown              -> null
```
