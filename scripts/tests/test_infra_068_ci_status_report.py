# Open Trader
# Copyright (c) 2026 l2xl (l2xl/at/proton.me)
# Distributed under the Intellectual Property Reserve License, v2 (IPRL)

"""The CI pipeline renders the recursive syngate status on the run's report page and the commit's checks -- [INFRA-068]."""

import pytest

from workflow_doc import steps


@pytest.mark.syngate("INFRA-068")
def test_ci_renders_the_syngate_status_report_from_both_test_suites():
    syngate_steps = steps("syngate")
    joined = " | ".join(syngate_steps)
    coverage_step = next(s for s in syngate_steps if "Check coverage against the tree" in s)
    assert "gate.sh" in coverage_step
    assert "--coverage pytest-coverage.jsonl" in coverage_step
    assert "--coverage build-ci/syngate_coverage.jsonl" in coverage_step
    report_step = next(s for s in syngate_steps if "syngate.py report" in s)
    assert "--coverage pytest-coverage.jsonl" in report_step
    assert "--coverage build-ci/syngate_coverage.jsonl" in report_step
    assert "--out syngate_status.json" in report_step
    assert "download-artifact" in joined and "syngate-coverage-cpp" in joined
    assert "write_status_summary.py" in joined
    assert "publish_check_run.py --status syngate_status.json" in joined
