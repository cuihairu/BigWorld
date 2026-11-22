# BigWorld client SDK

Client plugin SDK for BigWorld servers: wire protocol, engine adapters and a
def-to-code generator, aligned with kbengine's client plugin capabilities
(Unity / UE / TypeScript). The SDK lives entirely under `client_sdk/` and
never modifies `programming/bigworld/`.

## Layout

| Path | Batch | What it is |
|---|---|---|
| `unity/` | CL-1 | C# protocol core (`netstandard2.1`), engine-agnostic, with `dotnet test` |
| `unity/PROTOCOL.md` | CL-1 | Source-mapped wire protocol table (`lib/network` file:line ↔ C# ↔ test) |
| `typescript/` | CL-2 | TypeScript plugin skeleton (vitest) |
| `generator/` | CL-3 | Python def → `{Entity}Base` generator (golden tests) |
| `unreal/` | CL-5 | UE5 plugin scoping review (`REVIEW.md`, no C++ written) |

## Engine roadmap

| Engine | Status | Transport | Notes |
|---|---|---|---|
| Unity (C#) | CL-1 done | `ITransport` / `TcpTransport` (raw TCP; WS gateway adapter later) | The SDK core is `netstandard2.1` with **no `UnityEngine` dependency**, so it embeds in Unity, a headless host, or tests unchanged. |
| TypeScript | CL-2 done | interface-driven; TCP-over-WS gateway | Mirrors kbengine's `kbengine.js` plugin semantics (`Entity`/`EntityCall`, Event fire/register). |
| Python generator | CL-3 done | generates C# + TS | kbcmd equivalent; reads section-based `entities.xml` + `*.def`. |
| UE5 (C++) | CL-5 planned | review only | No UE code written in this SDK; `unreal/REVIEW.md` gives the build/CI decision. |

## CI and daily bundles

* `.github/workflows/client-sdk.yml` — three legs on every push/PR touching
  `client_sdk/`: `dotnet test` (unity), pnpm `build` + `vitest` + typecheck
  (typescript), `python3 -m unittest` golden (generator).
* `.github/workflows/daily-build.yml` (client-sdk job) — the same three
  suites as a gate, then packages a daily bundle: nupkg (C# core), npm
  tarball (TypeScript plugin), wheel (`bwgen`), with a per-file
  SHA256/byte-count manifest, zipped as
  `bigworld-client-sdk-dailyYYYYMMDD.<sha>.zip` and uploaded as a workflow
  artifact (14-day retention). No tags, no releases (repo rule).

## Protocol authority

The wire format is implemented from the in-repo BigWorld source
(`programming/bigworld/lib/network`, read-only). Every fact is tabulated in
`unity/PROTOCOL.md` with a `source:line`, the C# member and the locking test.

Key facts:

- **Little-endian** wire byte order (`lib/cstdmf/binary_stream.ipp:8`).
- TCP framing: `[uint16 LE length][payload]`, `0xFFFF` → large form.
- The client's first send prepends the header frame `"TCP\r\n\r\n"`
  (`lib/network/tcp_channel.cpp:29`, `:414`).
- Bundle payload: `[flags][firstRequestOffset?][messages]` with a
  request-offset chain; message records `[id][length][replyId?][payload]`.
- Length fields are little-endian; oversized lengths use the all-ones +
  `uint32` form.

## Why not kbcmd?

kbengine's `kbcmd` only generates **unity** and **ue4** targets from a
20-entry datatype table. BigWorld has no C#/TypeScript codegen target, so the
SDK provides one itself (CL-3 `generator/`, a kbcmd analogue) and hand-authors
the TypeScript plugin (CL-2) following the `kbengine.js` precedent. Naming
conventions (`{Entity}Base`, `EntityBaseEntityCall_{Entity}Base`) align with
kbcmd so generated code is drop-in comparable.

## Deviations from BigWorld (tracked, awaiting decision)

- The server→client **login** message ids in `unity/` follow the **kbengine**
  login shape (`onLoginCheckStatus` / `createEntity`), not BigWorld's
  `BaseAppInterface` handshake, because the task aligns plugin capabilities
  with kbengine. These ids are overridable via `LoginMessageIds` and the
  divergence is documented in `unity/PROTOCOL.md` §6.
