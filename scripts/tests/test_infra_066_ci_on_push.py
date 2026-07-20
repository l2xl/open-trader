# XCockpit
# Copyright (c) 2026 l2xl (l2xl/at/proton.me)
# Distributed under the Intellectual Property Reserve License, v2 (IPRL)

"""The CI pipeline builds the project on every push and pull request -- [INFRA-066]."""

import pytest

from workflow_doc import load, steps


@pytest.mark.req("INFRA-066")
def test_ci_builds_the_project_on_every_push_and_pull_request():
    doc = load()
    assert "push" in doc[True] and "pull_request" in doc[True]
    build_steps = " | ".join(steps("build"))
    assert "cmake --build" in build_steps
    assert "unit_tests" in build_steps
