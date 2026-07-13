# XCockpit
# Copyright (c) 2026 l2xl (l2xl/at/proton.me)
# Distributed under the Intellectual Property Reserve License, v2 (IPRL)

"""The CI pipeline renders the recursive requirement status on the run's report page and the commit's checks -- [INFRA-068]."""

from workflow_doc import steps


def test_ci_renders_the_requirement_status_report_from_both_test_suites():
    requirements_steps = steps("requirements")
    joined = " | ".join(requirements_steps)
    status_step = next(s for s in requirements_steps if "req_status.py" in s)
    assert "--junit pytest-results.xml" in status_step
    assert "--junit build-ci/ctest-results.xml" in status_step
    assert "write_status_summary.py" in joined
    assert "publish_check_run.py --status req_status.json" in joined
