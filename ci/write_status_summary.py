#!/usr/bin/env python3
# Open Trader
# Copyright (c) 2026 l2xl (l2xl/at/proton.me)
# Distributed under the Intellectual Property Reserve License, v2 (IPRL)

"""Write the requirements-status report to the GitHub Actions job summary.

Renders the same navigable tree as the 'Requirements Status' check run so the
report is readable directly on the job report page.
"""

import argparse
import os
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "scripts"))

from publish_check_run import load_status, load_validation, render_summary  # noqa: E402


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--status", default=ROOT / "req_status.json")
    parser.add_argument("--validation", help="captured ci/gate.sh output to fold into the report")
    args = parser.parse_args()

    report = load_status(args.status)
    summary = "# Requirements status\n\n" + render_summary(report, load_validation(args.validation)) + "\n"
    summary_path = os.environ.get("GITHUB_STEP_SUMMARY")
    if summary_path:
        with open(summary_path, "a", encoding="utf-8") as f:
            f.write(summary)
    else:
        print(summary)
    return 0


if __name__ == "__main__":
    sys.exit(main())
