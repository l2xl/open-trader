#!/usr/bin/env python3
# Open Trader
# Copyright (c) 2026 l2xl (l2xl/at/proton.me)
# Distributed under the Intellectual Property Reserve License, v2 (IPRL)

"""Two-commit re-approval check of the AI-Scratcher toolkit (invoked by the Validate workflow)."""

import sys

from ai_scratcher import check_self_approval

if __name__ == "__main__":
    sys.exit(check_self_approval.main())
