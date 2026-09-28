#!/usr/bin/env python3
# Open Trader
# Copyright (c) 2026 l2xl (l2xl/at/proton.me)
# Distributed under the Intellectual Property Reserve License, v2 (IPRL)

"""`syngate` CLI of the AI-Scratcher toolkit, bound to this repository as the managed project."""

import sys
from pathlib import Path

from ai_scratcher import syngate

if __name__ == "__main__":
    sys.argv[1:1] = ["--root", str(Path(__file__).resolve().parent.parent)]
    sys.exit(syngate.main())
