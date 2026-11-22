# UE5 (C++) plugin — scoping review (CL-5)

Status: **review only — no UE code is written by this SDK.** This document
decides whether to start a UE5 client plugin, what shape it would take, what
it costs to keep CI-testable, and what is (and is not) reusable from the
Unity C# plugin already delivered in CL-1. The verdict is at the bottom:
**缓做 (defer)**, with a concrete split that would make it tractable later.

The precedent exists elsewhere: kbengine ships a UE4 client plugin, and its
`kbcmd` codegen has `unity` and `ue4` targets (see `client_sdk/README.md`
§Why not kbcmd). So a BigWorld UE target is not novel — it is a port of an
already-defined protocol to a second engine. The open question is cost and
testability, not feasibility.

## What we already have

| Surface | Delivered | Where |
| --- | --- | --- |
| C# protocol core | CL-1, `dotnet test` 76/76 | `client_sdk/unity` |
| Wire facts, source-mapped | CL-1 | `client_sdk/unity/PROTOCOL.md` (7 sections: STM/FRM/BND/LEN/ID/LGN/ERR, each `lib/network` file:line ↔ C# ↔ test) |
| TS plugin | CL-2, `pnpm test` 86/86 | `client_sdk/typescript` |
| def → code generator | CL-3, `unittest` 9/9 | `client_sdk/generator` (model/defparse/ids/typemap/naming + per-language emitters) |
| CI | CL-4, three legs + daily bundle | `.github/workflows/client-sdk.yml`, `daily-build.yml` |

The C# core is `netstandard2.1` with **no `UnityEngine` reference**
(`BigWorld.ClientSdk.csproj`): the transport is interface-driven so the core
embeds in "Unity, a headless host, or tests" — that engine-agnosticism was a
deliberate CL-1 design choice, and it is the property the UE plugin most
wants to preserve.

## Reuse boundary with the Unity plugin (the crux)

**Not reusable (code).** UE5 has **no official managed / C# game runtime**:
UE4's experimental Mono-based C# support was removed, and UE5 game code is
C++ and Blueprints. Third-party CLR plugins (e.g. UnrealCSharp) exist but are
community projects and out of this SDK's dependency surface. The
`netstandard2.1` assemblies therefore **cannot be hosted in a UE5 game** — the
UE plugin is a C++ reimplementation of the *same wire semantics*, not a
wrapper over the C# code. Same for the TypeScript plugin (different runtime).

**Reusable (facts, no code).** The protocol facts themselves are
language-independent and already tabulated with a source line each:
`PROTOCOL.md`'s 7 sections are the contract. A C++ core re-implements exactly
those facts, and the C# test cases (76) become the C++ test oracle.

**Reusable (pipeline, no code).** `client_sdk/generator` is stdlib Python
with a language-agnostic front half — `model`, `defparse`, `ids` (the joint
exposed-method id numbering), `typemap`, `naming` — and per-language back
halves (`emit_cs`, `emit_ts`). Adding `emit_cpp.py` (the kbcmd `ue4`-target
precedent) reuses the entire front half, so generated `{Entity}Base` /
`EntityCall<Entity>Base` classes and their wire ids match the C#/TS output
exactly. This is the single largest reuse: **the generator, not the runtime.**

## UE module form

```
client_sdk/unreal/Plugins/BigWorldClient/
  BigWorldClient.uplugin
  Source/
    BigWorldClientCore/        # UE-free protocol core — no CoreMinimal.h
      BigWorldClientCore.Build.cs
      Public/  bw_stream.h / bw_framing.h / bw_bundle.h / bw_entity.h
      Private/
    BigWorldClient/            # thin UE Runtime module (UObject wrappers)
      BigWorldClient.Build.cs
      Public/  IBigWorldClientModule / UBigWorldConnection / UBigWorldEntity
      Private/
```

* `BigWorldClientCore` — plain C++ static library, no `CoreMinimal.h`, no
  `UObject`, no engine coupling: the UE counterpart of CL-1's
  engine-agnostic `netstandard2.1` core. Implements `PROTOCOL.md` (streams,
  framing, bundle, codecs) plus the generated entity bases.
* `BigWorldClient` — the UE-facing module: `IModuleInterface`,
  `UBigWorldConnection` (login → createEntity state machine), transport over
  `ISocketSubsystem` / `FTcpSocket` / `FRunnable`, Blueprint-exposed
  `UPROPERTY`/`UFUNCTION` surface, and `UBigWorldEntity` wrappers around the
  generated bases.

The split mirrors how CL-1 kept the protocol core free of `UnityEngine`: the
UE-only surface is a thin shell, and the testable logic is engine-free.

## CI testability cost — the deciding factor

Three tiers, with the repo's own precedent for the first:

| Tier | Build/test host | Cost | CI today? |
| --- | --- | --- | --- |
| `BigWorldClientCore` (UE-free C++) | plain CMake + `ctest`, ubuntu-24.04 | **low** — the existing sample track (`ci.yml`: cstdmf/resmgr/network/entitydef, each `enable_testing()` + `add_test`) is this exact pattern | ✅ yes |
| `BigWorldClient` (UE Runtime module) | UE5 + UBT + Epic terms | **high** — GitHub-hosted runners ship no UE5; an install is tens of GB and needs a UE account/license; a self-hosted UE runner is a new infra commitment | ❌ no |
| Blueprint/editor-level (Automation Test Framework) | UE build | high (same host as above) | ❌ no |

CL-4 set the rule that every SDK surface is gated by CI and ships a daily
bundle. The UE layer cannot meet that rule without a UE build environment —
that is the single reason the plugin is deferred rather than started.

## Verdict: 缓做 (defer)

**Do not build the UE5 plugin now.** No UE5 environment exists in the repo or
CI; the C# core is not embeddable; the UE module is not CI-testable under the
CL-4 gate. Starting it would produce an untestable, ungated surface.

**If/when a UE product asks**, do it in two layers so the bulk is testable
without a UE install:

1. **Now-able without UE** — land `BigWorldClientCore` (UE-free C++ protocol
   core, `PROTOCOL.md`-driven, plain CMake + `ctest`) and `emit_cpp.py` in the
   generator. Both fit the existing `ci.yml`/`client-sdk.yml` pattern and are
   gated + bundled like CL-4. This is the de-risking step and is valuable on
   its own.
2. **UE-env-gated** — the thin `BigWorldClient` Runtime module and its
   Blueprint/Automation tests, only once a UE build host exists (self-hosted
   runner or a licensed install).

**Flip 缓做 → 做** on: a UE5 build/CI environment (the gate for layer 2). The
estimate is qualitative by design: the core is a port of an already-tested,
already-specified protocol (weeks); the UE module is wrapper + environment
setup (weeks, plus the UE host). No code is written by this SDK for either
layer until the flip.
