# XCockpit
# Copyright (c) 2026 l2xl (l2xl/at/proton.me)
# Distributed under the Intellectual Property Reserve License, v2 (IPRL)

"""The CI pipeline runs the automated test suite against the built artifacts -- [INFRA-067]."""

from workflow_doc import load, steps


def test_ci_runs_the_test_suite_against_the_built_tree_after_build():
    assert load()["jobs"]["test"]["needs"] == "build"
    build_steps = " | ".join(steps("build"))
    test_steps = " | ".join(steps("test"))
    assert "upload-artifact" in build_steps and "build-tree" in build_steps
    assert "download-artifact" in test_steps and "build-tree" in test_steps
    assert "ctest" in test_steps
    assert "-LE live" in test_steps
