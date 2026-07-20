#!/usr/bin/env bash
# XCockpit
# Copyright (c) 2026 l2xl (l2xl/at/proton.me)
# Distributed under the Intellectual Property Reserve License, v2 (IPRL)

# Requirements-driven CI gate: structural validation + frozen-routine checks.
# Coverage joining happens in the workflow's report step once test coverage
# JSONL files exist; extra arguments (e.g. --coverage FILE) are passed through.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

PY="$ROOT/.venv-req/bin/python"
if [[ ! -x "$PY" ]]; then
    python3 -m venv "$ROOT/.venv-req"
    "$ROOT/.venv-req/bin/pip" install --quiet pyyaml pytest jinja2
fi

# GATE_STRICT=1 requires every item in the tree reviewed.
STRICT=()
if [[ "${GATE_STRICT:-0}" == 1 ]]; then
    STRICT=(--strict)
fi
"$PY" scripts/req.py validate "${STRICT[@]}" "$@"
echo "gate: OK"
