# Open Trader
# Copyright (c) 2026 l2xl (l2xl/at/proton.me)
# Distributed under the Intellectual Property Reserve License, v2 (IPRL)

"""The syngate report is published even when the gate is red -- [INFRA-069]."""

import pytest

from workflow_doc import load

REPORT_STEPS = (
    "Compute recursive syngate status",
    "Write syngate report to the job summary",
    "Publish syngate tree as a Check Run",
)


def _named_steps(job):
    return {s["name"]: s for s in load()["jobs"][job]["steps"] if "name" in s}


@pytest.mark.syngate("INFRA-069")
def test_report_is_published_with_gate_diagnostics_when_validation_fails():
    steps = _named_steps("syngate")

    # A failed gate step must not skip the report: conditioning the report steps
    # on the gate's outcome is exactly what used to swallow the whole report.
    for name in REPORT_STEPS:
        condition = steps[name]["if"]
        assert "!cancelled()" in condition
        assert "steps.validate.outcome" not in condition

    # ...and the reason for the red gate travels into the report.
    assert "syngate_validate.log" in steps["Validate syngate tree"]["run"]
    assert "syngate_validate.log" in steps["Check coverage against the tree"]["run"]
    assert "--validation syngate_validate.log" in steps["Write syngate report to the job summary"]["run"]
    assert "--validation syngate_validate.log" in steps["Publish syngate tree as a Check Run"]["run"]

    # Missing C++ coverage (test job died before uploading) is not fatal either.
    assert steps["Download C++ coverage records"]["continue-on-error"] is True
