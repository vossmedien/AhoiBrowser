#!/usr/bin/env python3
"""Compatibility entrypoint for the general local HTTPS E2E fixture."""

from __future__ import annotations

import sys
from pathlib import Path


FIXTURE_DIRECTORY = Path(__file__).resolve().parents[1] / "fixtures" / "e2e"
sys.path.insert(0, str(FIXTURE_DIRECTORY))

from manage import main  # noqa: E402


if __name__ == "__main__":
    raise SystemExit(main())
