# Open Trader
# Copyright (c) 2026 l2xl (l2xl/at/proton.me)
# Distributed under the Intellectual Property Reserve License, v2 (IPRL)

"""The CI pipeline builds the project on every push -- [INFRA-066]."""

import pytest

from workflow_doc import load, steps


@pytest.mark.req("INFRA-066")
def test_ci_builds_the_project_on_every_push():
    # `on:` parses as the YAML boolean True, not the string "on".
    triggers = load()[True]
    assert "push" in triggers
    # A pull request's head branch is pushed, so a pull_request trigger would
    # only re-run the same commit; its absence is the requirement, not a gap.
    assert "pull_request" not in triggers
    build_steps = " | ".join(steps("build"))
    assert "cmake --build" in build_steps
    assert "unit_tests" in build_steps
