#!/usr/bin/env python3
# XCockpit
# Copyright (c) 2026 l2xl (l2xl/at/proton.me)
# Distributed under the Intellectual Property Reserve License, v2 (IPRL)

"""Publish the tail of a failed build/test log as a GitHub Check Run.

CI job logs are only downloadable with repo-admin auth, so a failing build is
opaque to anyone driving CI through the public REST API. This posts the last
lines of each captured log into a Check Run's Markdown summary (via the same
Checks API path as publish_check_run.py), which is readable unauthenticated.
"""

import os
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "scripts"))

from publish_check_run import publish  # noqa: E402

TAIL_LINES = 120
MAX_SUMMARY_BYTES = 60000


def _tail(path):
    p = Path(path)
    if not p.is_file():
        return None
    lines = p.read_text(encoding="utf-8", errors="replace").splitlines()
    return "\n".join(lines[-TAIL_LINES:])


def render_summary(log_paths):
    blocks = []
    for path in log_paths:
        tail = _tail(path)
        if tail is None:
            continue
        fenced = tail.replace("```", "` ` `")
        blocks.append(f"### `{path}` (last {TAIL_LINES} lines)\n\n```\n{fenced}\n```")
    summary = "\n\n".join(blocks) or "No build/test logs were captured."
    if len(summary.encode("utf-8")) > MAX_SUMMARY_BYTES:
        summary = summary.encode("utf-8")[:MAX_SUMMARY_BYTES].decode("utf-8", "ignore") + "\n\n...(truncated)"
    return summary


def main():
    repo = os.environ.get("GITHUB_REPOSITORY")
    sha = os.environ.get("GITHUB_SHA")
    token = os.environ.get("GITHUB_TOKEN")
    if not (repo and sha and token):
        print("missing GITHUB_REPOSITORY/GITHUB_SHA/GITHUB_TOKEN", file=sys.stderr)
        return 1

    body = {
        "status": "completed",
        "conclusion": "failure",
        "output": {"title": "Build or offline tests failed", "summary": render_summary(sys.argv[1:])},
    }
    try:
        result = publish(repo, sha, token, body, name="CI Build Failure")
    except RuntimeError as exc:
        print(f"FAILED to create check run: {exc}", file=sys.stderr)
        return 1
    print(f"created check run: {result['html_url']}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
