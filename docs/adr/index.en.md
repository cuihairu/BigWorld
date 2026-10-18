# ADR Index

> **English** | [中文](index.md)

Architecture Decision Records: one page per major decision, recording only
**context / decision / trade-offs / consequences** — no process narrative.
Process detail and per-batch evidence live in the corresponding long-form
ledgers, linked at the end of each page. New decisions take new numbers;
accepted ADRs are never rewritten (the status field excepted).

| Number | Status | Decision |
|---|---|---|
| [0001](adr-0001-python-313-embedded-interpreter.en.md) | Accepted | Embedded interpreter Python 2.7 → 3.13 (vendored tree + official BWHooks interface) |
| [0002](adr-0002-vcpkg-third-party-deps.en.md) | Accepted | Third-party dependencies switch from vendored source trees to a vcpkg manifest (pinned baseline) |
| [0003](adr-0003-make-cmake-dual-track.en.md) | Accepted | Dual-track build: make main chain plus per-component CMake samples |

## Writing conventions

- File name: `adr-<four-digit-number>-<kebab-case-short-name>.md`.
- Required sections: **Status** (accepted/superseded/proposed + date),
  **Context** (the problem to solve and the constraints), **Decision**
  (a one-sentence actionable conclusion plus key points), **Trade-offs**
  (rejected alternatives and reasons), **Consequences**
  (positives / costs and risks / follow-ups triggered).
- Long-form evidence a decision refers to (migration ledgers, build analyses)
  is linked, not repeated.
