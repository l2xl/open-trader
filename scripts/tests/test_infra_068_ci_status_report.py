# XCockpit
# Copyright (c) 2026 l2xl (l2xl/at/proton.me)
# Distributed under the Intellectual Property Reserve License, v2 (IPRL)

"""The CI pipeline renders the recursive requirement status on the run's report page and the commit's checks -- [INFRA-068]."""

import pytest

from workflow_doc import steps


@pytest.mark.req("INFRA-068")
def test_ci_renders_the_requirement_status_report_from_both_test_suites():
    requirements_steps = steps("requirements")
    joined = " | ".join(requirements_steps)
    coverage_step = next(s for s in requirements_steps if "Check coverage against the tree" in s)
    assert "gate.sh" in coverage_step
    assert "--coverage pytest-coverage.jsonl" in coverage_step
    assert "--coverage build-ci/req_coverage.jsonl" in coverage_step
    report_step = next(s for s in requirements_steps if "req.py report" in s)
    assert "--coverage pytest-coverage.jsonl" in report_step
    assert "--coverage build-ci/req_coverage.jsonl" in report_step
    assert "--out req_status.json" in report_step
    assert "download-artifact" in joined and "req-coverage-cpp" in joined
    assert "write_status_summary.py" in joined
    assert "publish_check_run.py --status req_status.json" in joined
