#!/usr/bin/env python3
# Open Trader
# Copyright (c) 2026 l2xl (l2xl/at/proton.me)
# Distributed under the Intellectual Property Reserve License, v2 (IPRL)

"""Write the requirements-status report to the GitHub Actions job summary.

Renders the same navigable tree as the 'Requirements Status' check run so the
report is readable directly on the job report page.
"""

import json
import os
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "scripts"))

from publish_check_run import render_summary  # noqa: E402


def main():
    report = json.loads((ROOT / "req_status.json").read_text())
    summary = "# Requirements status\n\n" + render_summary(report) + "\n"
    summary_path = os.environ.get("GITHUB_STEP_SUMMARY")
    if summary_path:
        with open(summary_path, "a", encoding="utf-8") as f:
            f.write(summary)
    else:
        print(summary)
    return 0


if __name__ == "__main__":
    sys.exit(main())
