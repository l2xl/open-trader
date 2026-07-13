#!/usr/bin/env bash
# XCockpit
# Copyright (c) 2026 l2xl (l2xl/at/proton.me)
# Distributed under the Intellectual Property Reserve License, v2 (IPRL)

# Requirements-driven CI gate: doorstop validation + frozen-test and coverage checks.
# Build and test execution are explicit workflow steps (see validate.yml).
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

PY="$ROOT/.venv-req/bin/python"
DOORSTOP="$ROOT/.venv-req/bin/doorstop"
if [[ ! -x "$PY" ]]; then
    python3 -m venv "$ROOT/.venv-req"
    "$ROOT/.venv-req/bin/pip" install --quiet 'doorstop>=3.1,<4' pytest jinja2 junitparser pyyaml
fi

# Relaxed mode skips review/suspect checks while the tree is being adopted item by
# item (freeze semantics are enforced by the two scripts below on reviewed items).
# GATE_STRICT=1 requires the whole tree reviewed and all link stamps current.
if [[ "${GATE_STRICT:-0}" == 1 ]]; then
    "$DOORSTOP" --error-all
else
    "$DOORSTOP" -W -S --error-all
fi
"$PY" scripts/check_frozen_tests.py
"$PY" scripts/check_req_coverage.py
echo "gate: OK"
