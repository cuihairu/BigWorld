#!/usr/bin/env python3
from __future__ import annotations

import importlib.util
import sys
from pathlib import Path


SCRIPT = Path(__file__).with_name("reload_orchestrate.py")
spec = importlib.util.spec_from_file_location("reload_orchestrate", SCRIPT)
reload_orchestrate = importlib.util.module_from_spec(spec)
assert spec.loader is not None
sys.modules[spec.name] = reload_orchestrate
spec.loader.exec_module(reload_orchestrate)


ProbeResult = reload_orchestrate.ProbeResult


class FakeComponent:
    """A component whose probe and reload results are scripted per test."""

    def __init__(self, name, loaded=True, digest="a" * 32):
        self.name = name
        self.probe_result = ProbeResult(name, loaded, digest if loaded else "")
        self.reloaded_with = None

    def probe(self, full_reload=True):
        self.probed_full = full_reload
        return self.probe_result

    def reload(self, full_reload, expected_digest):
        self.reloaded_with = (full_reload, expected_digest)
        return True


def test_empty_fleet_does_not_proceed():
    plan = reload_orchestrate.plan_orchestration([])

    assert not plan.proceed
    assert plan.agreed_digest == ""
    assert plan.failures == ()
    assert plan.mismatches == ()


def test_single_clean_probe_proceeds_with_its_digest():
    plan = reload_orchestrate.plan_orchestration(
        [ProbeResult("cellapp1", True, "abc123")]
    )

    assert plan.proceed
    assert plan.agreed_digest == "abc123"
    assert plan.failures == ()
    assert plan.mismatches == ()


def test_identical_digests_proceed():
    probes = [ProbeResult(f"cellapp{i}", True, "abc123") for i in range(3)]

    plan = reload_orchestrate.plan_orchestration(probes)

    assert plan.proceed
    assert plan.agreed_digest == "abc123"


def test_a_probe_that_did_not_load_blocks_the_fleet():
    plan = reload_orchestrate.plan_orchestration(
        [
            ProbeResult("cellapp1", True, "abc123"),
            ProbeResult("cellapp2", False, ""),
            ProbeResult("cellapp3", True, "abc123"),
        ]
    )

    assert not plan.proceed
    assert plan.agreed_digest == ""
    assert plan.failures == ("cellapp2",)
    assert plan.mismatches == ()


def test_a_digest_split_blocks_the_fleet_and_names_the_rogue():
    plan = reload_orchestrate.plan_orchestration(
        [
            ProbeResult("cellapp1", True, "abc123"),
            ProbeResult("cellapp2", True, "def456"),
        ]
    )

    assert not plan.proceed
    assert plan.agreed_digest == ""
    assert plan.failures == ()
    assert plan.mismatches == ("cellapp2",)


def test_a_probe_without_a_digest_cannot_join_the_agreement():
    # An empty agreed digest would mean an empty expectedDigest, which places
    # no barrier constraint at all - so a missing digest must fail closed.
    plan = reload_orchestrate.plan_orchestration(
        [
            ProbeResult("cellapp1", True, ""),
            ProbeResult("cellapp2", True, ""),
        ]
    )

    assert not plan.proceed
    assert plan.agreed_digest == ""


def test_orchestrate_reloads_every_component_pinned_to_the_agreed_digest():
    components = [FakeComponent(f"cellapp{i}") for i in range(3)]

    plan = reload_orchestrate.orchestrate(components)

    assert plan.proceed
    assert all(
        c.reloaded_with == (True, plan.agreed_digest) for c in components
    )


def test_orchestrate_reloads_nothing_when_one_component_disagrees():
    components = [
        FakeComponent("cellapp1"),
        FakeComponent("cellapp2", digest="def456"),
        FakeComponent("cellapp3"),
    ]

    plan = reload_orchestrate.orchestrate(components)

    assert not plan.proceed
    assert all(c.reloaded_with is None for c in components)


def test_orchestrate_passes_the_reload_mode_through_to_every_probe():
    components = [FakeComponent("cellapp1")]

    reload_orchestrate.orchestrate(components, full_reload=False)

    assert components[0].probed_full is False
    assert components[0].reloaded_with == (False, "a" * 32)