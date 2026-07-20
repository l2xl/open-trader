# XCockpit
# Copyright (c) 2026 l2xl (l2xl/at/proton.me)
# Distributed under the Intellectual Property Reserve License, v2 (IPRL)

"""Shared loader for the Validate GitHub Actions workflow document."""

from pathlib import Path

import yaml

REPO_ROOT = Path(__file__).resolve().parent.parent.parent
WORKFLOW = REPO_ROOT / ".github" / "workflows" / "validate.yml"


def load():
    return yaml.safe_load(WORKFLOW.read_text())


def steps(job):
    result = []
    for s in load()["jobs"][job]["steps"]:
        env = " ".join(f"{k}={v}" for k, v in (s.get("env") or {}).items())
        with_args = " ".join(f"{k}={v}" for k, v in (s.get("with") or {}).items())
        result.append(f"{s.get('name', '')} {s.get('uses', '')} {env} {with_args} {s.get('run', '')}")
    return result
