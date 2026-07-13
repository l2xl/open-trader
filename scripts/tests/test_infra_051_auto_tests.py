# XCockpit
# Copyright (c) 2026 l2xl (l2xl/at/proton.me)
# Distributed under the Intellectual Property Reserve License, v2 (IPRL)

"""The project's automated test suite runs in CI -- [INFRA-051]."""

from workflow_doc import load, steps


def test_ci_defines_a_test_job_that_runs_the_suite():
    assert "test" in load()["jobs"]
    assert "ctest" in " | ".join(steps("test"))
