#!/usr/bin/env python3
"""Cross-process hot-reload orchestration, the caller half of the barrier.

Design P1 of docs/upgrade-plan/hot-reload-productionization.md keeps the barrier
on the caller and adds no RPC: the engine exposes only two primitives per
component,

  * BigWorld.reloadScript(full, digest, dryRun=True)  - the dry run probe,
  * BigWorld.reloadScript(full, digest)               - the real reload, whose
    expectedDigest is judged against the load's own schema digest,

and BigWorld.schemaDigest() to read back what a component actually loaded. This
module is the caller: it dry-runs every component, collects what each loaded,
and reloads for real only once every digest agrees.

A split is the thing the barrier protects against - some component read a
different entity-definitions revision than the others, so a real reload would
migrate entities across a version boundary (the base side talking to a cell side
on another schema). Failing closed means no component reloads at all.
"""

from __future__ import annotations

from dataclasses import dataclass
from typing import Sequence


@dataclass(frozen=True)
class ProbeResult:
    """What one component's dry run reported."""

    component: str
    loaded: bool
    digest: str


@dataclass(frozen=True)
class OrchestrationPlan:
    """The go/no-go decision for one fleet-wide reload attempt."""

    proceed: bool
    agreed_digest: str
    failures: tuple[str, ...]
    mismatches: tuple[str, ...]


def plan_orchestration(probes: Sequence[ProbeResult]) -> OrchestrationPlan:
    """Decide whether every component may reload for real.

    The contract is strict and fail-closed: every probe must have loaded and
    reported a digest, and every digest must be the same. A probe that loaded
    without a digest reports no schema version at all, so it cannot be part of
    the agreement - and an empty digest must never become the pinned digest,
    because an empty expectedDigest places no barrier constraint at all.
    """
    if not probes:
        return OrchestrationPlan(False, "", (), ())

    failures = tuple(p.component for p in probes if not p.loaded)
    loaded = [p for p in probes if p.loaded]

    digests = {p.digest for p in loaded}
    reference = loaded[0].digest if loaded else ""
    # Reporting is among the loaded probes, compared with the first one; a
    # failed probe is reported as a failure instead.
    mismatches = tuple(
        p.component for p in loaded if p.digest != reference
    )

    # all(digests) is the non-empty check: a set with one empty string is
    # non-empty, but an empty digest cannot be pinned, so it must fail closed.
    proceed = (
        not failures
        and bool(loaded)
        and len(digests) == 1
        and all(digests)
    )
    return OrchestrationPlan(
        proceed=proceed,
        agreed_digest=probes[0].digest if proceed else "",
        failures=failures,
        mismatches=mismatches,
    )


def orchestrate(components, full_reload: bool = True) -> OrchestrationPlan:
    """Dry-run every component, then reload for real only if all agree.

    A component is anything that reaches one of the two engine primitives:
    probe(full_reload) returns a ProbeResult, and reload(full_reload,
    expected_digest) issues the real reload. Only the components that agree are
    reloaded, and each is pinned to the agreed digest so the engine's own
    barrier re-checks the load inside that process.
    """
    probes = tuple(component.probe(full_reload) for component in components)
    plan = plan_orchestration(probes)
    if plan.proceed:
        for component in components:
            component.reload(full_reload, plan.agreed_digest)
    return plan


class LocalComponent:
    """The live half: one component reachable in the current process.

    This is the only wiring the primitives need, because BigWorld is the
    per-process module the caller already sits in - a fleet-wide orchestration
    assembles one of these per process however it reaches them. The dry run of
    a full reload still clears the registered data types, so it can only be a
    probe: a real reload has to follow it.
    """

    def __init__(self, name: str):
        self.name = name

    def probe(self, full_reload: bool = True) -> ProbeResult:
        import BigWorld

        loaded = BigWorld.reloadScript(full_reload, "", True)
        digest = BigWorld.schemaDigest()
        return ProbeResult(self.name, bool(loaded), "" if digest is None else digest)

    def reload(self, full_reload: bool, expected_digest: str) -> bool:
        import BigWorld

        return bool(BigWorld.reloadScript(full_reload, expected_digest))